"""Schema-driven structural INI analyzer for SAGE (BFME1) INI files.

It does NOT parse values (types); it walks the block structure the way the engine does:
  top-level token -> block parser; inside a block each line's first token is a field looked up in the
  block's table; fields of kind 'block' open a nested block closed by END; 'module' fields
  (Behavior/Draw/Body/ClientUpdate/...) read "ModuleType ModuleTag" and open the module's block.
Unknown blocks/fields are recorded and skipped with a depth heuristic (see opener()).
"""
import re
from collections import Counter, defaultdict

from .schema import Schema, BFME, ZH

SPLIT = re.compile(r'[ \t\r=]+')
COMMENT = re.compile(r';|//|--')


def strip_comment(line):
    m = COMMENT.search(line)
    return line[:m.start()] if m else line


OBJ_TABLE = 'ObjectDefinition.FieldParseTable'
WRAPPERS = ('AddModule', 'ReplaceModule', 'InheritableModule', 'OverrideableByLikeKind')
_MODULE_OPS = {w.lower() for w in WRAPPERS} | {'removemodule'}   # module operations, not object fields


class Ctx:
    __slots__ = ('kind', 'tid', 'label', 'level', 'file', 'line', 'arbitrary', 'path')

    def __init__(self, kind, tid, label, level, file, line, arbitrary=False, path=()):
        self.kind, self.tid, self.label, self.level = kind, tid, label, level
        self.file, self.line, self.arbitrary, self.path = file, line, arbitrary, path


class Usage:
    """Counter with first example location."""

    def __init__(self):
        self.count = Counter()
        self.example = {}
        self.meta = {}

    def add(self, key, where, meta=None):
        self.count[key] += 1
        if key not in self.example:
            self.example[key] = where
            if meta is not None:
                self.meta[key] = meta

    def items(self, n=None):
        return [(k, c, self.example[k], self.meta.get(k)) for k, c in self.count.most_common(n)]


def norm_value(v):
    return ' '.join(v.split())


class IniAnalyzer:
    def __init__(self, schema: Schema, vfs=None):
        self.s = schema
        self.vfs = vfs
        self.defines = {}
        self.files = {}               # path -> dict(lines, blocks, warnings, source)
        self.top_blocks = Usage()     # name -> count
        self.unknown_blocks = Usage()
        self.unknown_fields = Usage()  # (table class, field)
        self.unknown_modules = Usage()  # (field e.g. Behavior, module type)
        self.modules = Usage()         # (module table, module type) known
        self.fields = Usage()          # (table id, field) known
        self.schema_gaps = Usage()     # known block without table (opaque)
        self.warnings = []
        self.n_blocks = 0
        self.n_nested = 0
        self.n_lines = 0
        self.n_field_lines = 0
        self.n_unknown_ctx_lines = 0
        self.n_scripts = 0
        self.objects = []      # Object/ChildObject/ObjectReskin definitions with module ops (see objects.py)
        self.cur_obj = None
        self.unknown_nested = Usage()  # unknown fields treated as nested blocks (heuristic)
        self.trace = False
        self.bfme_only = Usage()       # entities tagged AddedIn(Bfme) by OpenSAGE (kind, name)
        self.later_game = Usage()      # entities tagged AddedIn(Bfme2+) used in BFME1 data
        self.not_in_zh_cpp = Usage()   # (kind, name) used in data, absent from GeneralsMD C++ name sets
        self.openers = Usage()         # (context, table class, key, how) -> a key line that opens a nested block
        self.n_define_lines = 0        # #define lines seen by collect_defines
        self.n_define_redefined = 0    # ... of which redefine an existing name (first definition wins)
        self.n_define_redefined_diff = 0   # ... with a different value
        self.substitutions = Counter()     # macro name -> tokens substituted in field values (resolve())

    # ---------- defines ----------
    def collect_defines(self, path, text):
        for i, raw in enumerate(text.splitlines(), 1):
            s = strip_comment(raw).strip()
            if s[:7].lower() == '#define':
                parts = s.split(None, 2)
                if len(parts) >= 2:
                    self.n_define_lines += 1
                    name, val = parts[1].upper(), (parts[2].strip() if len(parts) > 2 else '')
                    if name in self.defines:
                        self.n_define_redefined += 1
                        self.n_define_redefined_diff += self.defines[name][0] != val
                    self.defines.setdefault(name, (val, f'{path}:{i}'))

    def resolve(self, value):
        if not self.defines:
            return value
        out = []
        for t in value.split():
            d = self.defines.get(t.upper())
            if d:
                self.substitutions[t.upper()] += 1
            out.append(d[0] if d else t)
        return ' '.join(out)

    # ---------- heuristics ----------
    def opener(self, first, has_eq, ntok, indent_grows=False):
        """Does an UNKNOWN field line open a nested block? Schema names first, then layout heuristics:
        no '=' (SAGE block headers rarely have values) or the next code line is indented deeper."""
        lf = first.lower()
        if lf in self.s.always_block:
            return 'schema_block_elsewhere'
        if lf in self.s.value_names:
            return None
        if re.match(r'^[-+.\d]', first):
            return None
        return 'no_equals' if not has_eq else ('indent' if indent_grows else None)

    @staticmethod
    def _indent(raw):
        return len(raw.expandtabs(4)) - len(raw.expandtabs(4).lstrip())

    def _indent_grows(self, lines, i):
        cur = self._indent(lines[i])
        for j in range(i + 1, min(i + 40, len(lines))):
            t = strip_comment(lines[j]).strip()
            if t:
                return self._indent(lines[j]) > cur and t.lower() != 'end'
        return False

    def _entity(self, kind, name, game, where, zh_known):
        lvl = self.s.level(game)
        if lvl == BFME:
            self.bfme_only.add((kind, name), where)
        elif lvl > BFME:
            self.later_game.add((kind, name), where, game)
        if not zh_known:
            self.not_in_zh_cpp.add((kind, name), where)

    # ---------- parsing ----------
    def parse_text(self, path, text, records=None, source=None, _stack=None, _depth=0):
        """Parse one INI text. If records is a list, append (key, field, value, resolved, where)."""
        finfo = self.files.setdefault(path, {'lines': 0, 'blocks': 0, 'warnings': 0, 'source': source})
        top = Ctx('top', None, '', 0, path, 0)
        stack = _stack if _stack is not None else [top]
        occ = defaultdict(Counter)   # id(ctx path) -> field occurrence counter
        lines = text.splitlines()
        finfo['lines'] += len(lines)
        self.n_lines += len(lines)
        script = None   # (start line, ctx path) while inside BeginScript ... EndScript (embedded Lua, raw)
        for ln, raw in enumerate(lines, 1):
            if script is not None:
                if raw.split(None, 1)[:1] and raw.split(None, 1)[0].lower().split(';')[0] == 'endscript':
                    if records is not None:
                        records.append((script[1], 'BeginScript', ' | '.join(script[2]), ' | '.join(script[2]), f'{path}:{script[0]}'))
                    script = None
                else:
                    t = strip_comment(raw).strip()
                    if t:
                        script[2].append(' '.join(t.split()))
                continue
            s = strip_comment(raw).strip()
            if not s:
                continue
            where = f'{path}:{ln}'
            toks = [t for t in SPLIT.split(s) if t]
            if not toks:
                continue
            first = toks[0]
            has_eq = '=' in s
            rest = s[len(first):].lstrip(' \t=').strip()
            lf = first.lower()
            if lf == '#define':
                parts = s.split(None, 2)
                if len(parts) >= 2:
                    self.defines[parts[1].upper()] = (parts[2].strip() if len(parts) > 2 else '', where)
                continue
            if lf == '#include':
                self._include(path, rest.strip('"'), stack, records, source, where, _depth)
                continue
            ctx = stack[-1]
            if self.trace:
                print(f'{ln:6d} d={len(stack)-1} [{ctx.label}|{ctx.tid}] {s[:60]}')
            if lf == 'beginscript' and ctx.kind != 'top':
                self.n_scripts += 1
                if ctx.tid:
                    fk, f = self.s.field(ctx.tid, first)
                    if f is not None:
                        self.fields.add((ctx.tid, fk), where)
                    elif not ctx.arbitrary:
                        tclass = self.s.tables[ctx.tid]['class']
                        self.unknown_fields.add((tclass, first), where)
                self.openers.add(('table' if ctx.tid else ctx.kind, self._tclass(ctx), 'BeginScript', 'raw_until_EndScript'), where)
                o = occ[ctx.path]['beginscript']
                occ[ctx.path]['beginscript'] += 1
                script = (ln, ctx.path + (('beginscript', o),), [])
                continue
            if lf == 'end':
                if len(stack) > 1:
                    stack.pop()
                else:
                    self._warn(path, where, 'stray END at top level')
                continue
            if ctx.kind == 'top':
                self.n_blocks += 1
                finfo['blocks'] += 1
                name = toks[1] if len(toks) > 1 else ''
                label = f'{first} {name}'.strip()
                key = ((first, ' '.join(toks[1:2]).lower()),)
                k, b = self.s.top_block(first)
                if b is None:
                    self.unknown_blocks.add(first, where)
                    self.not_in_zh_cpp.add(('block', first), where) if lf not in self.s.zh_blocks else None
                    stack.append(Ctx('unknown', None, label, 0, path, ln, path=key))
                    if records is not None:
                        records.append((key, '<block>', rest, rest, where))
                    continue
                self.top_blocks.add(k, where)
                self._entity('block', k, b.get('game'), where, lf in self.s.zh_blocks)
                self.cur_obj = None
                if b.get('table') == OBJ_TABLE:
                    self.cur_obj = {'kind': k, 'name': toks[1] if len(toks) > 1 else '',
                                    'parent': toks[2] if len(toks) > 2 and k != 'Object' else None,
                                    'where': where, 'file': path, 'source': source, 'ops': [],
                                    'fields': Counter()}   # object-level keys (lower case) -> lines
                    self.objects.append(self.cur_obj)
                if records is not None:
                    records.append((key, '<block>', rest, rest, where))
                if b['kind'] == 'block':
                    if b.get('table') or b.get('arbitrary'):
                        stack.append(Ctx('table', b.get('table'), label, self.s.level(b.get('game')), path, ln,
                                         arbitrary=b.get('arbitrary', False), path=key))
                    else:
                        self.schema_gaps.add(k, where)
                        stack.append(Ctx('unknown', None, label, 0, path, ln, path=key))
                continue
            # inside a block
            self.n_field_lines += 1
            if ctx.kind == 'unknown':
                self.n_unknown_ctx_lines += 1
                how = self.opener(first, has_eq, len(toks), has_eq and self._indent_grows(lines, ln - 1))
                if how:
                    self.openers.add(('unknown', self._tclass(ctx), first, how), where)
                    self.n_nested += 1
                    stack.append(Ctx('unknown', None, first, 0, path, ln, path=ctx.path + ((lf, rest.lower()),)))
                elif records is not None:
                    o = occ[ctx.path][lf]
                    occ[ctx.path][lf] += 1
                    records.append((ctx.path + ((lf, o),), first, rest, self.resolve(rest), where))
                continue
            fk, f = self.s.field(ctx.tid, first) if ctx.tid else (None, None)
            tclass = self.s.tables[ctx.tid]['class'] if ctx.tid else ctx.label.split()[0]
            if self.cur_obj is not None and ctx.tid == OBJ_TABLE and len(stack) == 2:
                key = (fk or first).lower()
                if key not in _MODULE_OPS and not (f is not None and f['kind'] == 'module'):
                    self.cur_obj['fields'][key] += 1
            if f is None:
                if ctx.arbitrary:
                    if records is not None:
                        records.append((ctx.path + ((lf, 0),), first, rest, self.resolve(rest), where))
                    continue
                self.unknown_fields.add((tclass, first), where)
                if lf not in self.s.zh_fields:
                    self.not_in_zh_cpp.add(('field', f'{tclass}.{first}'), where)
                how = self.opener(first, has_eq, len(toks), has_eq and self._indent_grows(lines, ln - 1))
                if how:
                    self.openers.add(('table', tclass, first, how), where)
                    self.n_nested += 1
                    self.unknown_nested.add((tclass, first), where)
                    stack.append(Ctx('unknown', None, first, 0, path, ln, path=ctx.path + ((lf, rest.lower()),)))
                elif records is not None:
                    o = occ[ctx.path][lf]
                    occ[ctx.path][lf] += 1
                    records.append((ctx.path + ((lf, o),), first, rest, self.resolve(rest), where))
                continue
            self.fields.add((ctx.tid, fk), where)
            lvl = max(ctx.level, self.s.level(f.get('game')))
            g = f.get('game') if self.s.level(f.get('game')) >= ctx.level else None
            if lvl >= BFME and ctx.level < BFME:
                self._entity('field', f'{tclass}.{fk}', f.get('game'), where, lf in self.s.zh_fields)
            elif lvl < BFME:
                if lf not in self.s.zh_fields:
                    self.not_in_zh_cpp.add(('field', f'{tclass}.{fk}'), where)
            elif ctx.level >= BFME:
                pass  # inside a BFME-only block: counted at block level
            del g
            kind = f['kind']
            if self.cur_obj is not None and ctx.tid == OBJ_TABLE:
                if fk in ('RemoveModule', 'ReplaceModule') and len(toks) > 1:
                    self.cur_obj['ops'].append(('remove', toks[1], where))
            if kind == 'block' and f.get('min_values') and len(toks) - 1 < f['min_values']:
                kind = 'value'
                self.openers.add(('table', tclass, fk, 'schema_block_but_value_by_token_count'), where)
            if kind == 'module':
                mtype = toks[1] if len(toks) > 1 else ''
                tag = toks[2] if len(toks) > 2 else ''
                mk, m = self.s.module(f['module_table'], mtype)
                mpath = ctx.path + ((lf, f'{mtype.lower()}:{tag.lower()}'),)
                self.openers.add(('table', tclass, fk, 'schema_module'), where)
                if records is not None:
                    records.append((mpath, '<module>', rest, rest, where))
                self.n_nested += 1
                if m is None:
                    self.unknown_modules.add((fk, mtype), where)
                    if self.cur_obj is not None and ctx.tid == OBJ_TABLE:
                        mode = ctx.label if ctx.label in WRAPPERS else 'Default'
                        self.cur_obj['ops'].append(('add', fk, mtype, tag, (), mode, where))
                    if mtype.lower() not in self.s.zh_modules:
                        self.not_in_zh_cpp.add(('module', mtype), where)
                    stack.append(Ctx('unknown', None, mtype, 0, path, ln, path=mpath))
                else:
                    self.modules.add((f['module_table'], mk), where)
                    if self.cur_obj is not None and ctx.tid == OBJ_TABLE:
                        mode = ctx.label if ctx.label in WRAPPERS else 'Default'
                        self.cur_obj['ops'].append(('add', fk, mk, tag, tuple(m.get('kinds') or ()), mode, where))
                    mlvl = max(lvl, self.s.level(m.get('game')))
                    if ctx.level < BFME:
                        self._entity('module', mk, m.get('game'), where, mk.lower() in self.s.zh_modules)
                    if m.get('table') or m.get('arbitrary'):
                        stack.append(Ctx('table', m.get('table'), mk, mlvl, path, ln,
                                         arbitrary=m.get('arbitrary', False), path=mpath))
                    else:
                        self.schema_gaps.add(f'module {mk}', where)
                        stack.append(Ctx('unknown', None, mk, mlvl, path, ln, path=mpath))
            elif kind == 'block':
                self.n_nested += 1
                self.openers.add(('table', tclass, fk, 'schema_block'), where)
                bpath = ctx.path + ((lf, ' '.join(rest.lower().split())),)
                if records is not None:
                    records.append((bpath, '<block>', rest, rest, where))
                if f.get('table') or f.get('arbitrary'):
                    stack.append(Ctx('table', f.get('table'), fk, lvl, path, ln,
                                     arbitrary=f.get('arbitrary', False), path=bpath))
                else:
                    self.schema_gaps.add(f'{tclass}.{fk}', where)
                    stack.append(Ctx('unknown', None, fk, lvl, path, ln, path=bpath))
            else:
                if records is not None:
                    o = occ[ctx.path][lf]
                    occ[ctx.path][lf] += 1
                    records.append((ctx.path + ((lf, o),), first, rest, self.resolve(rest), where))
        if script is not None:
            self._warn(path, f'{path}:{script[0]}', 'BeginScript without EndScript')
        if _stack is None and len(stack) > 1:
            for c in stack[1:]:
                self._warn(path, f'{c.file}:{c.line}', f'block "{c.label}" not closed by END before EOF')
        return records

    def _tclass(self, ctx):
        return self.s.tables[ctx.tid]['class'] if ctx.tid else (ctx.label.split() or [''])[0]

    def _include(self, path, inc, stack, records, source, where, depth):
        if self.vfs is None or depth > 8:
            self._warn(path, where, f'#include {inc} not resolved')
            return
        base = path.replace('/', '\\').rsplit('\\', 1)[0]
        target = inc.replace('/', '\\')
        parts = base.split('\\')
        for p in target.split('\\'):
            if p == '..':
                parts.pop()
            elif p and p != '.':
                parts.append(p)
        tp = '\\'.join(parts)
        data = self.vfs.read(tp)
        if data is None:
            self._warn(path, where, f'#include {inc} -> {tp} not found')
            return
        self.parse_text(tp, data.decode('latin-1'), records, source, _stack=stack, _depth=depth + 1)

    def _warn(self, path, where, msg):
        self.warnings.append((where, msg))
        if path in self.files:
            self.files[path]['warnings'] += 1
