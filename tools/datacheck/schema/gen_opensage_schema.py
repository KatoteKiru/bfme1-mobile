#!/usr/bin/env python3
"""Generate a machine-readable INI schema from OpenSAGE C# sources (static analysis, no .NET needed).

Extracts from src/**/*.cs:
  * top-level INI blocks: IniParser.BlockParsers dictionary
  * field tables: IniParseTable<T> X = [Base.Table.Concat(]new IniParseTable<T> { {"Field", lambda}, ... }
  * module dispatch tables: Dictionary<string, Func<IniParser, XModuleData>> T = { {"ModuleName", X.Parse}, ... }
  * [AddedIn(SageGame.X)] on classes and properties -> "game" tag per entity
Each field is classified: value (single line) | block (nested ... END, with child table) | module
("Behavior = ModuleType Tag" dispatch). Usage: gen_opensage_schema.py <OpenSAGE root> <out.json>
"""
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cslex import strip_comments, match_brace, find_statement_end, parse_entries  # noqa: E402

GAMES = ["CncGenerals", "CncGeneralsZeroHour", "Bfme", "Bfme2", "Bfme2Rotwk",
         "Cnc3", "Cnc3KanesWrath", "Ra3", "Ra3Uprising", "Cnc4"]
GAME_LEVEL = {g: i for i, g in enumerate(GAMES)}

CLASS_RE = re.compile(r'\b(class|struct|record)\s+(\w+)\s*(<[^>{;]*>)?\s*(\([^)]*\))?\s*(?::\s*([^{;]+?))?\s*(where[^{;]*)?\{')
ADDEDIN_RE = re.compile(r'AddedIn\(\s*SageGame\.(\w+)\s*\)')
PROP_RE = re.compile(r'\[\s*AddedIn\(\s*SageGame\.(\w+)\s*\)\s*\](?:\s*\[[^\]]*\])*\s*'
                     r'(?:(?:public|internal|private|protected|static|readonly|virtual|override|new|required)\s+)+'
                     r'[\w<>\[\],.?\s]+?\b(\w+)\s*(?=\{|=|;)')
TABLE_DECL_RE = re.compile(r'\b(IniParseTable(?:Child)?)<\s*(\w+)\s*(?:,\s*(\w+)\s*)?>\s+(\w+)\s*=')
MODDICT_RE = re.compile(r'Dictionary<\s*string\s*,\s*Func<\s*IniParser\s*,\s*(\w+)\s*>\s*>\s+(\w+)\s*=')
BLOCKPARSERS_RE = re.compile(r'Dictionary<\s*string\s*,\s*Action<\s*IniParser\s*,\s*AssetStore\s*>\s*>\s+BlockParsers\s*=')
METHOD_RE = re.compile(r'\b(\w+)\s*(?:<[^>()]*>)?\s*\(\s*(?:this\s+)?IniParser\s+(\w+)[^)]*\)\s*(?:where[^{=;]*)?(=>|\{)')
BLOCK_CALL_RE = re.compile(r'\b(ParseBlock|ParseNamedBlock|ParseIndexedBlock|ParseTopLevelBlock|ParseBlockContent)\s*(?:<[^>()]*>)?\s*\(')
TABLE_REF_RE = re.compile(r'\b(?:(\w+)\s*\.\s*)?(\w*ParseTable\w*)\b')
CALL_RE = re.compile(r'\b(\w+)\s*\.\s*(\w+)\s*(?:<[^>()]*>)?\s*\(')


def max_game(*gs):
    gs = [g for g in gs if g]
    return max(gs, key=lambda g: GAME_LEVEL.get(g, 99)) if gs else None


class CsClass:
    def __init__(self, name, file, start, end, body_start, bases, game):
        self.name, self.file, self.start, self.end, self.body_start = name, file, start, end, body_start
        self.bases, self.game = bases, game
        self.props, self.tables, self.methods = {}, {}, {}
        self.text, self.parent = '', None


class Extractor:
    def __init__(self, root):
        self.root = root
        self.classes = defaultdict(list)
        self.block_parsers = []
        self.module_tables = {}
        self.module_index = {}

    def find_module_table(self, name, ctx):
        keys = self.module_index.get(name, [])
        c = ctx
        while c:
            for cc in self.chain(c.name):
                k = f'{cc.name}.{name}'
                if k in self.module_tables:
                    return k
            c = c.parent
        return keys[0] if len(keys) == 1 else None

    def load(self):
        src_root = os.path.join(self.root, 'src')
        for dp, dn, fn in os.walk(src_root):
            dn[:] = [d for d in dn if d not in ('bin', 'obj') and 'Tests' not in d and 'Benchmarks' not in d]
            for f in sorted(fn):
                if f.endswith('.cs'):
                    p = os.path.join(dp, f)
                    with open(p, encoding='utf-8-sig', errors='replace') as fh:
                        self._scan(os.path.relpath(p, self.root), strip_comments(fh.read()))

    def _scan(self, rel, text):
        spans = []
        for m in CLASS_RE.finditer(text):
            bstart = m.end() - 1
            bend = match_brace(text, bstart)
            if bend < 0:
                continue
            k = m.start()
            ps = max(text.rfind(';', 0, k), text.rfind('}', 0, k), text.rfind('{', 0, k))
            gm = ADDEDIN_RE.search(text[ps + 1:k])
            bases = [b.strip().split('<')[0].strip() for b in (m.group(5) or '').split(',') if b.strip()]
            c = CsClass(m.group(2), rel, m.start(), bend, bstart, bases, gm.group(1) if gm else None)
            c.text = text[bstart:bend + 1]
            spans.append(c)

        def owner(pos):
            best = None
            for c in spans:
                if c.body_start <= pos <= c.end and (best is None or c.body_start > best.body_start):
                    best = c
            return best
        for c in spans:
            c.parent = owner(c.start)
            self.classes[c.name].append(c)
        for m in PROP_RE.finditer(text):
            o = owner(m.start())
            if o:
                o.props[m.group(2)] = m.group(1)
        for m in TABLE_DECL_RE.finditer(text):
            o = owner(m.start())
            if o:
                end = find_statement_end(text, m.end())
                o.tables[m.group(4)] = self._table_expr(text[m.end():end], m.group(2), m.group(3))
        for m in MODDICT_RE.finditer(text):
            o = owner(m.start())
            end = find_statement_end(text, m.end())
            expr = text[m.end():end]
            b = expr.find('{')
            if b < 0:
                continue
            mods = {}
            for key, val in parse_entries(expr[b + 1:match_brace(expr, b)]):
                cm = re.search(r'\b([A-Z]\w*)\s*\.\s*(Parse\w*)', val)
                mods[key] = (cm.group(1), cm.group(2)) if cm else (None, None)
            key = f"{o.name if o else '?'}.{m.group(2)}"
            self.module_tables[key] = {'base': m.group(1), 'owner': o.name if o else None,
                                       'modules': mods, 'file': rel}
            self.module_index.setdefault(m.group(2), []).append(key)
        m = BLOCKPARSERS_RE.search(text)
        if m:
            end = find_statement_end(text, m.end())
            expr = text[m.end():end]
            b = expr.find('{')
            self.block_parsers = parse_entries(expr[b + 1:match_brace(expr, b)])
        for m in METHOD_RE.finditer(text):
            o = owner(m.start())
            if not o:
                continue
            if m.group(3) == '=>':
                body = text[m.end():find_statement_end(text, m.end())]
            else:
                bs = m.end() - 1
                body = text[bs:match_brace(text, bs) + 1]
            o.methods.setdefault(m.group(1), []).append(body)

    @staticmethod
    def _table_expr(expr, tparam, tchild):
        t = {'type': tparam, 'child_type': tchild, 'refs': [], 'entries': []}
        # literal initializers, incl. target-typed "new() { ... }"; skip nested "new X {}" inside entries
        lit = re.compile(r'new(?:\s+IniParseTable(?:Child)?\s*(?:<[^>]*>)?)?\s*(?:\([^{;]*?\))?\s*\{')
        rest, pos = [], 0
        while True:
            nm = lit.search(expr, pos)
            if not nm:
                rest.append(expr[pos:])
                break
            b = nm.end() - 1
            e = match_brace(expr, b)
            rest.append(expr[pos:nm.start()])
            t['entries'].extend(parse_entries(expr[b + 1:e]))
            pos = e + 1
        for rm in TABLE_REF_RE.finditer(' '.join(rest)):
            t['refs'].append((rm.group(1), rm.group(2)))
        return t

    def prop_type(self, cls, prop):
        for c in (self.chain(cls.name) if cls else []):
            m = re.search(r'\b([A-Z]\w*)\s*(?:<[^>]*>)?\??\s+' + re.escape(prop) + r'\s*(?:\{|=|;)', c.text)
            if m:
                return self.get_class(m.group(1))
        return None

    def get_class(self, name):
        cs = self.classes.get(name)
        if not cs:
            return None
        for c in cs:
            if c.tables:
                return c
        return cs[0]

    def chain(self, name):
        seen, out, c = set(), [], self.get_class(name)
        while c and c.name not in seen:
            seen.add(c.name)
            out.append(c)
            c = next((self.get_class(b) for b in c.bases if self.get_class(b)), None)
        return out

    def find_table(self, cls_name, table_name, ctx):
        cands = []
        if cls_name and self.get_class(cls_name):
            cands = self.chain(cls_name)
        elif ctx:
            c = ctx
            while c:
                cands.extend(self.chain(c.name))
                c = c.parent
        for c in cands:
            if table_name in c.tables:
                return c, table_name
        hits = [c for cl in self.classes.values() for c in cl if table_name in c.tables]
        if len(hits) == 1 and not cls_name:
            return hits[0], table_name
        return None, None

    def find_method(self, cls, name):
        for c in (self.chain(cls.name) if cls else []):
            if name in c.methods:
                return c, c.methods[name]
        return None, None

    def prop_game(self, tname, prop):
        for c in self.chain(tname):
            if prop in c.props:
                return c.props[prop]
        return None

    def module_kinds(self, name):
        """ModuleKinds flags (Body/Update/Die/...) via 'override ModuleKinds ModuleKinds => ...' in the class chain."""
        for c in self.chain(name):
            m = re.search(r'ModuleKinds\s+ModuleKinds\s*=>\s*([^;]+);', c.text)
            if m:
                return sorted(set(re.findall(r'ModuleKinds\.(\w+)', m.group(1))) - {'None'})
        return []

    def class_game(self, name):
        c = self.get_class(name)
        return c.game if c else None


GEN_ONLY = re.compile(r'SageGame\.(CncGenerals|CncGeneralsZeroHour)\b')
BFME_PLUS = re.compile(r'SageGame\.(Bfme|Bfme2|Bfme2Rotwk)\b|>=\s*SageGame\.(CncGeneralsZeroHour|Bfme)\b')


def bfme_branch(body):
    """Keep only the code path taken when parser.SageGame == Bfme in simple if/else on SageGame."""
    out, pos = [], 0
    for m in re.finditer(r'\bif\s*\(', body):
        if m.start() < pos:
            continue
        ce = match_brace(body, m.end() - 1, '(', ')')
        cond = body[m.end():ce]
        if 'SageGame' not in cond:
            continue
        j = ce + 1
        while j < len(body) and body[j].isspace():
            j += 1
        if j >= len(body) or body[j] != '{':
            continue
        be = match_brace(body, j)
        k = be + 1
        em = re.match(r'\s*else\s*\{', body[k:])
        else_span = None
        if em:
            es = k + em.end() - 1
            else_span = (es, match_brace(body, es))
        gen_only = GEN_ONLY.search(cond) and not BFME_PLUS.search(cond) and '!=' not in cond
        bfme = BFME_PLUS.search(cond) and '<' not in cond.replace('<=', '')
        if gen_only:
            out.append(body[pos:m.start()])
            pos = (else_span[0] if else_span else be + 1)
        elif bfme and else_span:
            out.append(body[pos:be + 1])
            pos = else_span[1] + 1
    out.append(body[pos:])
    return ''.join(out)


class Builder:
    def __init__(self, ex):
        self.ex, self.tables, self.modules = ex, {}, {}

    def analyze(self, expr, ctx, depth=0):
        res = {'kind': 'value', 'table': None, 'module_table': None, 'arbitrary': False, 'classes': [],
               'min_values': None}
        if depth > 5:
            return res
        if 'SageGame' in expr:
            expr = bfme_branch(expr)
        mm = re.search(r'\bParseModule\s*(?:<[^>()]*>)?\s*\(\s*\w+\s*,\s*(\w+)', expr)
        if mm:
            mk = self.ex.find_module_table(mm.group(1), ctx)
            if mk:
                res.update(kind='module', module_table=mk)
                return res
        bc = BLOCK_CALL_RE.search(expr)
        if bc:
            res['kind'] = 'block'
            pre = expr[:bc.start()]
            # e.g. Emotion.Parse: "AddEmotion = X" is a one-liner, "AddEmotion = TYPE X" opens a block:
            # the block call sits inside "if (tok.HasValue) { ... }"
            for im in re.finditer(r'\bif\s*\(([^()]*\.HasValue[^()]*)\)\s*\{', pre):
                ie = match_brace(expr, im.end() - 1)
                if ie > bc.start() and 'GetNextTokenOptional' in pre:
                    res['min_values'] = len(re.findall(r'GetNextToken\w*\s*\(', pre[:im.start()]))
            if 'IniArbitraryFieldParserProvider' in expr:
                res['arbitrary'] = True
            for scope in (expr[bc.end():], expr):
                for rm in TABLE_REF_RE.finditer(scope):
                    c, tn = self.ex.find_table(rm.group(1), rm.group(2), ctx)
                    if c:
                        res['table'] = self.ensure_table(c, tn)
                        break
                if res['table']:
                    break
            return res
        for cm in CALL_RE.finditer(expr):
            obj, meth = cm.group(1), cm.group(2)
            if obj in ('parser', 'iniParser', 'Parser'):
                continue
            argend = match_brace(expr, cm.end() - 1, '(', ')')
            args = expr[cm.end():argend]
            if not re.search(r'\b(parser|iniParser|p)\b', args):
                continue
            pre = expr[:cm.start()].rstrip()
            if pre.endswith('.'):
                cls = self.ex.prop_type(ctx, obj)        # x.Prop.Method(parser) -> type of Prop
            else:
                cls = self.ex.get_class(obj) or (ctx if obj[:1].islower() else None)
            if cls is None:
                continue
            res['classes'].append(cls.name)
            oc, bodies = self.ex.find_method(cls, meth)
            for body in bodies or []:
                sub = self.analyze(body, oc, depth + 1)
                if sub['kind'] != 'value':
                    sub['classes'] = res['classes'] + sub['classes']
                    return sub
        return res

    def ensure_table(self, cls, name):
        tid = f'{cls.name}.{name}'
        if tid in self.tables:
            return tid
        t = cls.tables[name]
        out = {'class': t['type'], 'owner': cls.name, 'file': cls.file, 'fields': {},
               'game': self.ex.class_game(t['type']) or cls.game}
        self.tables[tid] = out
        for rc, rn in t['refs']:
            if rn == name and rc in (None, cls.name):
                continue
            c2, tn2 = self.ex.find_table(rc, rn, cls)
            if not c2:
                continue
            bid = self.ensure_table(c2, tn2)
            for k, v in self.tables[bid]['fields'].items():
                out['fields'].setdefault(k, dict(v, inherited_from=bid))
        ttype = t['child_type'] or t['type']
        tcls = self.ex.get_class(ttype) or cls
        for key, expr in t['entries']:
            lm = re.match(r'\(?\s*\w+\s*,\s*(\w+)\s*\)?\s*=>', expr)
            var = lm.group(1) if lm else 'x'
            pm = re.search(r'\b' + re.escape(var) + r'\s*\.\s*(\w+)', expr)
            pgame = self.ex.prop_game(ttype, pm.group(1)) if pm else None
            info = self.analyze(expr, tcls)
            cgame = max_game(*[self.ex.class_game(c) for c in info['classes']])
            fld = {'kind': info['kind'], 'game': pgame or cgame}
            if info['table']:
                fld['table'] = info['table']
                fld['game'] = fld['game'] or self.tables[info['table']]['game']
            if info['module_table']:
                fld['module_table'] = info['module_table']
                self.ensure_modules(info['module_table'])
            if info['arbitrary']:
                fld['arbitrary'] = True
            if info.get('min_values'):
                fld['min_values'] = info['min_values']
            out['fields'][key] = fld   # literal entries override inherited
        return tid

    def module_game(self, mname, cname):
        # Only classes whose name matches the module name (case-insensitive) count: OpenSAGE maps e.g.
        # W3DModelDraw -> W3dScriptedModelDrawModuleData (a BFME class), which must not make W3DModelDraw BFME-only.
        want = {(mname + suf).lower() for suf in ('', 'Behavior', 'Module', 'Update', 'ModuleData', 'BehaviorModuleData', 'UpdateModuleData')}
        gs = [c.game for cl in self.ex.classes.values() for c in cl if c.name.lower() in want]
        return max_game(*gs)

    def ensure_modules(self, mt):
        if mt in self.modules:
            return
        out = {}
        self.modules[mt] = out
        for mname, (cname, meth) in self.ex.module_tables[mt]['modules'].items():
            info = {'class': cname, 'game': self.module_game(mname, cname), 'table': None,
                    'kinds': self.ex.module_kinds(cname) if cname else []}
            c = self.ex.get_class(cname) if cname else None
            oc, bodies = self.ex.find_method(c, meth or 'Parse') if c else (None, None)
            if bodies:
                r = self.analyze(bodies[0], oc)
                info['table'] = r['table']
                if r['arbitrary']:
                    info['arbitrary'] = True
            out[mname] = info

    def build(self):
        top = {}
        for key, expr in self.ex.block_parsers:
            info = self.analyze(expr, None)
            b = {'kind': info['kind'], 'game': max_game(*[self.ex.class_game(c) for c in info['classes']])}
            if info['table']:
                b['table'] = info['table']
                b['game'] = max_game(b['game'], self.tables[info['table']]['game'])
            if info['arbitrary']:
                b['arbitrary'] = True
            top[key] = b
        return top


def main():
    root, out = sys.argv[1], sys.argv[2]
    ex = Extractor(root)
    ex.load()
    b = Builder(ex)
    top = b.build()
    try:
        rev = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True).strip()
    except Exception:
        rev = None
    schema = {'source': 'OpenSAGE https://github.com/OpenSAGE/OpenSAGE', 'revision': rev,
              'generator': 'tools/datacheck/schema/gen_opensage_schema.py', 'games': GAMES,
              'note': 'game = AddedIn(SageGame.X) of property/class; null = untagged (since Generals)',
              'top_level': top, 'tables': b.tables, 'module_tables': b.modules}
    with open(out, 'w') as f:
        json.dump(schema, f, indent=1, sort_keys=True)
    kinds = defaultdict(int)
    for v in top.values():
        kinds[v['kind']] += 1
    print(f"top-level {len(top)} {dict(kinds)}; tables {len(b.tables)}; "
          f"fields {sum(len(t['fields']) for t in b.tables.values())}; module tables {len(b.modules)}; "
          f"modules {sum(len(m) for m in b.modules.values())}")


if __name__ == '__main__':
    main()
