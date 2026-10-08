"""BFME INI syntax beyond Zero Hour: embedded Lua (BeginScript ... EndScript) and other extensions, counted
separately for the base game and for patch 2.22, with what the ZH INI parser (repo/Core/GameEngine/Source/Common/INI)
would do with each of them.

Sets: "base" = for every INI path the oldest version from a non-patch archive (ini.big, maps.big);
"2.22" = the winning version of every INI path (what the game loads with the patch).

Lua blocks are read as raw bytes: the body is everything after the end of the BeginScript line up to the start of
the first following line whose first token is EndScript (case-insensitive), exactly as stored in the archive.
"""
import re
from collections import Counter, defaultdict

ZH_INI_MAX_CHARS_PER_LINE = 1028   # repo/Core/GameEngine/Include/Common/INI.h:60
MODULE_FIELDS = ('draw', 'behavior', 'body', 'clientupdate', 'clientbehavior')
WRAPPERS = ('replacemodule', 'addmodule', 'inheritablemodule', 'overrideablebylikekind')
TOK_RX = re.compile(rb'^[ \t]*([A-Za-z_#][A-Za-z0-9_]*)')
EOL_RX = re.compile(rb'\r\n|\n|\r')


def split_lines(data):
    """[(start offset, line bytes without EOL, eol bytes)] - splits on CRLF, LF and lone CR."""
    out, p = [], 0
    for m in EOL_RX.finditer(data):
        out.append((p, data[p:m.start()], m.group()))
        p = m.end()
    if p < len(data):
        out.append((p, data[p:], b''))
    return out


ZH_SEPS = re.compile(rb'[ \t\r\n=]+')     # INI::getSeps() " \n\r\t=" (repo/Core/GameEngine/Include/Common/INI.h)


def zh_first_token(line):
    """First token as ZH INI::initFromINIMulti sees it: readLine() cuts at ';', then strtok(seps)."""
    toks = [t for t in ZH_SEPS.split(line.split(b';', 1)[0]) if t]
    return toks[0] if toks else None


def first_token(line):
    m = TOK_RX.match(line)
    return (m.group(1), m.end()) if m else (None, 0)


def lua_blocks(data):
    """Raw BeginScript ... EndScript blocks of one INI file (bytes)."""
    lines = split_lines(data)
    res, i = [], 0
    while i < len(lines):
        off, line, eol = lines[i]
        tok, end = first_token(line)
        if tok is None or tok.lower() != b'beginscript':
            i += 1
            continue
        blk = {'line': i + 1, 'begin_token': tok.decode('latin-1'), 'after_begin': line[end:].decode('latin-1'),
               'end_line': None, 'end_token': None, 'after_end': None, 'nested_begin': [], 'lua_end_lines': 0,
               'semicolon_lines': 0, 'endscript_substring_lines': [], 'max_line_len': 0, 'max_line_no': None, 'lines': 0,
               'eol': Counter(), 'zh_end_lines': 0}
        body_start = off + len(line) + len(eol)
        tok_end = off + end                       # first byte after the BeginScript token
        j = i + 1
        while j < len(lines):
            o2, l2, e2 = lines[j]
            t2, end2 = first_token(l2)
            if t2 is not None and t2.lower() == b'endscript':
                blk.update(end_line=j + 1, end_token=t2.decode('latin-1'), after_end=l2[end2:].decode('latin-1'))
                blk['body'] = data[body_start:o2]
                blk['token_body'] = data[tok_end:o2 + end2 - len(t2)]   # up to the first byte of the EndScript token
                break
            if t2 is not None and t2.lower() == b'beginscript':
                blk['nested_begin'].append(j + 1)
            if t2 is not None and t2.lower() == b'end':
                blk['lua_end_lines'] += 1         # first identifier is "end" (also "end)" etc.)
            zt = zh_first_token(l2)
            if zt is not None and zt.lower() == b'end':
                blk['zh_end_lines'] += 1          # ZH: stricmp(strtok(line), "End") == 0 -> closes the block (INI.cpp:1642)
            if b';' in l2:
                blk['semicolon_lines'] += 1       # ZH readLine() cuts the line at ';'
            if b'endscript' in l2.lower():
                blk['endscript_substring_lines'].append(j + 1)
            if len(l2) > blk['max_line_len']:
                blk['max_line_len'], blk['max_line_no'] = len(l2), j + 1
            blk['lines'] += 1
            blk['eol'][e2.decode('latin-1') or 'EOF'] += 1
            j += 1
        else:
            blk['body'] = data[body_start:]
            blk['token_body'] = data[tok_end:]
        res.append(blk)
        i = j + 1
    return res


def _eol_kind(data):
    c = Counter(m.group() for m in EOL_RX.finditer(data))
    return {'CRLF': c[b'\r\n'], 'LF': c[b'\n'], 'CR': c[b'\r']}


def version_sets(vfs):
    """{'base': {path: (label, bytes)}, '2.22': {...}} for INI files."""
    from .patchdiff import is_patch_archive
    base, final = {}, {}
    for k, vf in vfs.files.items():
        if not k.endswith('.ini') or k.rsplit('\\', 1)[-1] == 'desktop.ini':   # Windows Explorer files, not game data
            continue
        final[vf.path] = (vf.source, vf.read())
        if not is_patch_archive(vf.source):
            base[vf.path] = (vf.source, vf.read())
        else:
            olds = [o for o in vf.overridden if not is_patch_archive(o[0])]
            if olds:
                label, entry, _ = olds[-1]
                base[vf.path] = (label, entry.read() if entry is not None else vf.read(entry))
    return {'base': base, '2.22': final}


class _View:
    """Minimal VFS-like view (read(path) -> bytes) over one version set, for #include resolution."""

    def __init__(self, files):
        self._f = {k.lower(): v[1] for k, v in files.items()}

    def read(self, path):
        return self._f.get(path.lower().replace('/', '\\'))


def analyze_set(schema, files):
    """Run the structural analyzer over one version set; returns (analyzer, records)."""
    from .ini import IniAnalyzer
    an = IniAnalyzer(schema, _View(files))
    texts = {p: d.decode('latin-1') for p, (lab, d) in files.items()}
    for p, t in texts.items():
        an.collect_defines(p, t)
    recs = []
    for p, t in texts.items():
        an.parse_text(p, t, records=recs, source=files[p][0])
    return an, recs


def _context(path_key, text_lines, line, objnames):
    """Module type/tag/wrapper/owner field/parent object of a BeginScript record path, with original spelling
    recovered from the raw lines above the block."""
    ctx = {'parent_kind': None, 'parent': None, 'wrapper': None, 'field': None, 'module_type': None,
           'module_tag': None, 'module_line': None, 'owner': None, 'owner_line': None}
    if not path_key:
        return ctx
    ctx['parent_kind'], pname = path_key[0][0], path_key[0][1]
    ctx['parent'] = objnames.get(pname, pname)
    mod_i = None
    for i, (k, v) in enumerate(path_key[1:], 1):
        if k in WRAPPERS and ctx['wrapper'] is None:
            ctx['wrapper'] = k
        if k in MODULE_FIELDS and ':' in v and mod_i is None:
            mod_i = i
            ctx['field'] = k
            mtype, mtag = v.split(':', 1)
            rx = re.compile(r'^\s*' + re.escape(k) + r'\s*=?\s*(' + re.escape(mtype) + r')\s+(' + re.escape(mtag) + r')\b', re.I)
            for ln in range(line - 1, 0, -1):
                m = rx.match(text_lines[ln - 1])
                if m:
                    ctx.update(module_type=m.group(1), module_tag=m.group(2), module_line=ln)
                    break
            else:
                ctx.update(module_type=mtype, module_tag=mtag)
    if ctx['wrapper']:
        for ln in range(line - 1, 0, -1):
            m = re.match(r'^\s*(' + re.escape(ctx['wrapper']) + r')\b', text_lines[ln - 1], re.I)
            if m:
                ctx['wrapper'] = m.group(1)
                break
    owner = path_key[-2] if len(path_key) >= 2 and path_key[-1][0] == 'beginscript' else None
    if owner and (mod_i is None or path_key.index(owner) > mod_i):
        for ln in range(line - 1, 0, -1):
            m = re.match(r'^\s*(' + re.escape(owner[0]) + r')\b', text_lines[ln - 1], re.I)
            if m:
                ctx.update(owner=m.group(1), owner_line=ln)
                break
    return ctx


def lua_report(files, recs, objects):
    """Per-block table + summary for one version set."""
    rec_at = {}
    for key, field, val, res, where in recs:
        if field == 'BeginScript':
            rec_at[where.lower()] = key
    objnames = {o['name'].lower(): o['name'] for o in objects}
    blocks, files_eol, max_line_any = [], {}, Counter()
    for p, (label, data) in sorted(files.items()):
        files_eol[p] = dict(_eol_kind(data), source=label)
        bl = lua_blocks(data)
        if not bl:
            continue
        tl = data.decode('latin-1').splitlines()
        for b in bl:
            key = rec_at.get(f'{p}:{b["line"]}'.lower())
            c = _context(key, tl, b['line'], objnames) if key else _context(None, tl, b['line'], objnames)
            b.update(c, file=p, source=label, in_analyzer=key is not None,
                     outside_module=c['module_type'] is None)
            blocks.append(b)
    return blocks, files_eol


def lua_summary(blocks, files_eol, gp_expected=None, zh_modules=(), zh_fields=()):
    def cnt(pred):
        return sum(1 for b in blocks if pred(b))
    by_type = Counter((b['field'] or '—', b['module_type'] or '—') for b in blocks)
    by_wrapper = Counter(b['wrapper'] or 'напрямую' for b in blocks)
    by_owner = Counter(b['owner'] or '—' for b in blocks)
    by_file = Counter(b['file'] for b in blocks)
    by_parent_kind = Counter(b['parent_kind'] or '—' for b in blocks)
    eol_files = Counter()
    for p, e in files_eol.items():
        kinds = [k for k in ('CRLF', 'LF', 'CR') if e[k]]
        eol_files['+'.join(kinds) or 'без переводов строк'] += 1
    lua_files = {b['file'] for b in blocks}
    lua_eol = Counter()
    for p in lua_files:
        e = files_eol[p]
        lua_eol['+'.join(k for k in ('CRLF', 'LF', 'CR') if e[k])] += 1
    mx = max(blocks, key=lambda b: b['max_line_len']) if blocks else None
    s = {
        'blocks': len(blocks), 'files': len(lua_files), 'modules': len({(b['file'], b['module_line']) for b in blocks if b['module_line']}),
        'direct_in_module': cnt(lambda b: b['module_type'] and not b['wrapper']),
        'by_module_type': [{'field': f, 'module_type': t, 'blocks': c} for (f, t), c in by_type.most_common()],
        'by_wrapper': dict(by_wrapper.most_common()), 'by_owner_field': dict(by_owner.most_common()),
        'by_parent_kind': dict(by_parent_kind.most_common()),
        'top_files': by_file.most_common(15),
        'after_begin_nonempty': cnt(lambda b: b['after_begin'].strip()),
        'after_begin_examples': sorted({b['after_begin'].strip() for b in blocks if b['after_begin'].strip()})[:10],
        'after_end_nonempty': cnt(lambda b: (b['after_end'] or '').strip()),
        'after_end_examples': sorted({(b['after_end'] or '').strip() for b in blocks if (b['after_end'] or '').strip()})[:10],
        'begin_spellings': dict(Counter(b['begin_token'] for b in blocks)),
        'end_spellings': dict(Counter(b['end_token'] for b in blocks if b['end_token'])),
        'eof_before_end': [f"{b['source']}:{b['file']}:{b['line']}" for b in blocks if b['end_line'] is None],
        'nested_begin': [f"{b['source']}:{b['file']}:{b['line']} (вложенный :{b['nested_begin'][0]})" for b in blocks if b['nested_begin']],
        'endscript_substring': [f"{b['source']}:{b['file']}:{x}" for b in blocks for x in b['endscript_substring_lines']],
        'outside_module': [f"{b['source']}:{b['file']}:{b['line']} ({b['parent_kind'] or 'верхний уровень'} {b['parent'] or ''})".strip()
                           for b in blocks if b['outside_module']],
        'not_in_analyzer': [f"{b['source']}:{b['file']}:{b['line']}" for b in blocks if not b['in_analyzer']],
        'max_line_len': mx['max_line_len'] if mx else 0,
        'max_line_at': f"{mx['source']}:{mx['file']}:{mx['max_line_no']}" if mx else None,
        'lines_over_zh_limit': sum(1 for b in blocks if b['max_line_len'] > ZH_INI_MAX_CHARS_PER_LINE),
        'zh_ini_max_chars_per_line': ZH_INI_MAX_CHARS_PER_LINE,
        'lua_lines': sum(b['lines'] for b in blocks),
        'body_eol': dict(sum((b['eol'] for b in blocks), Counter())),
        'files_with_lua_eol': dict(lua_eol), 'all_ini_eol': dict(eol_files),
        'blocks_with_lua_end_line': cnt(lambda b: b['lua_end_lines']),
        'blocks_with_zh_end_token': cnt(lambda b: b['zh_end_lines']),   # strtok-exact, what ZH takes for End
        'zh_end_token_lines': sum(b['zh_end_lines'] for b in blocks),
        'blocks_with_semicolon': cnt(lambda b: b['semicolon_lines']),
        'gameplay_expected': gp_expected,
        # ZH outcome per block: host module unknown to ZH -> crash (null ModuleData); module known but the owner
        # field (AnimationState/IdleAnimationState) unknown -> silently skipped, Lua 'end' closes the module early
        'zh_module_unknown': cnt(lambda b: b['module_type'] and b['module_type'].lower() not in zh_modules),
        'zh_module_known_owner_unknown': cnt(lambda b: b['module_type'] and b['module_type'].lower() in zh_modules
                                             and (b['owner'] or '').lower() not in zh_fields),
        'zh_module_known_owner_known': cnt(lambda b: b['module_type'] and b['module_type'].lower() in zh_modules
                                           and (b['owner'] or '').lower() in zh_fields),
    }
    return s


# ------------------------------------------------------------------------------------------------------------
# other BFME syntax extensions vs the ZH INI parser
ZH = {  # how repo/ (ZH, GeneralsMD + Core) handles it, with file:line
    'include': ('падает', 'на верхнем уровне `#include` — неизвестный блок: `throw INI_UNKNOWN_TOKEN` '
                '(Core/GameEngine/Source/Common/INI/INI.cpp:503–507); директив препроцессора в INI.cpp нет'),
    'define': ('падает', '`#define` на верхнем уровне — неизвестный блок (INI.cpp:503–507); макросы не подставляются'),
    'undef': ('падает', 'то же, что `#define` (INI.cpp:503–507)'),
    'other_directive': ('падает', 'неизвестный блок (INI.cpp:503–507)'),
    'arith': ('падает', '`#MULTIPLY(` и т.п. в числовом поле: `scanReal`/`scanInt` → sscanf не читает число → '
              '`throw INI_INVALID_DATA` (INI.cpp:1834–1835, 1872–1873) → `INIException` (INI.cpp:1697–1706)'),
    'macro_numeric': ('падает', 'имя макроса вместо числа: sscanf → `INI_INVALID_DATA` (INI.cpp:1872–1873)'),
    'macro_other': ('молча ошибается', 'имя макроса читается как литерал (имя шаблона/строка), ошибка — '
                    'только при поиске по имени позже или никогда'),
    'slash_comment': ('молча ошибается / падает', 'комментарий только `;` (INI.cpp:602–606). `//` в начале строки — '
                      'неизвестное поле, в release пропускается (INI.cpp:1715–1719, DEBUG_CRASH = no-op, Debug.h:205); '
                      'на верхнем уровне — неизвестный блок → throw (INI.cpp:503–507); в конце строки — лишние токены: '
                      'парсеры списков (KindOf, флаги) бросают `INI_UNKNOWN_TOKEN`/`INI_INVALID_DATA`, остальные игнорируют'),
    'dash_comment': ('молча ошибается', '`--` не комментарий для ZH: становится токеном значения/поля'),
    'quotes': ('частично', 'кавычки понимает только `parseQuotedAsciiString`/`getNextQuotedAsciiString` '
               '(INI.cpp:812–815, 862–933); в остальных полях `"` входит в токен'),
    'unbalanced_quotes': ('молча ошибается', 'строка с нечётным числом `"`: многострочных значений в ZH нет, '
                          'readLine() читает до `\\n` (INI.cpp:578–598)'),
    'continuation': ('молча ошибается', 'продолжения строки `\\` нет (INI.cpp:578–598)'),
    'comma_values': ('зависит от поля', 'запятая — разделитель только в отдельных парсерах (например parseSoundsList, '
                     'INI.cpp:2101); в остальных входит в токен'),
    'no_equals': ('поддерживает', '`=` — обычный разделитель strtok (INI.h:253 `" \\n\\r\\t="`)'),
    'tabs': ('поддерживает (release)', 'таб → пробел (INI.cpp:609–611); в debug — DEBUG_ASSERTCRASH (INI.cpp:600)'),
    'crlf': ('поддерживает', '`\\r` → пробел (INI.cpp:609–611), строки делятся только по `\\n` (INI.cpp:594)'),
    'lone_cr': ('молча ошибается', 'одиночный `\\r` не переводит строку (INI.cpp:594): две строки склеиваются'),
    'long_lines': ('молча ошибается', f'строка длиннее INI_MAX_CHARS_PER_LINE = {ZH_INI_MAX_CHARS_PER_LINE} '
                   '(INI.h:60) обрезается, хвост читается как следующая строка (INI.cpp:580, 623–626; DEBUG_CRASH)'),
    'end_case': ('поддерживает', '`End` сравнивается stricmp (INI.cpp:1642)'),
    'missing_end': ('падает', 'EOF до End: `throw INI_MISSING_END_TOKEN` (INI.cpp:1726–1732)'),
    'stray_end': ('падает', 'лишний End на верхнем уровне — неизвестный блок `End` (INI.cpp:503–507)'),
    'unknown_top_block': ('падает', 'блок верхнего уровня, которого нет в ZH: `throw INI_UNKNOWN_TOKEN` (INI.cpp:503–507)'),
    'unknown_module': ('падает', 'модуль, которого нет в ZH: `newModuleDataFromINI` вернёт nullptr '
                       '(GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp:585–594), затем '
                       '`data->isAiModuleData()` — разыменование nullptr (ThingTemplate.cpp:623)'),
    'unknown_nested_block': ('молча ошибается, потом падает', 'поле-блок, которого нет в ZH: строка пропускается '
                             '(INI.cpp:1715–1719), поля вложенного блока уходят в родителя, его `End` закрывает '
                             'родителя (INI.cpp:1642), лишний End в итоге даёт throw на верхнем уровне (INI.cpp:503–507)'),
    'unknown_field': ('молча ошибается', 'неизвестное поле пропускается без ошибки в release (INI.cpp:1715–1719, Debug.h:205)'),
    'lua_block': ('падает / молча ошибается', 'поля BeginScript в ZH нет. Если модуль-хозяин не из ZH '
                  '(W3DScriptedModelDraw, W3DHordeModelDraw, W3DQuadrupedDraw), ZH падает на модуле (см. «модуль, которого нет в ZH»). '
                  'Если модуль есть в ZH (W3DTruckDraw), строки BeginScript и Lua — неизвестные поля и молча пропускаются '
                  '(INI.cpp:1715–1719), но Lua-строка `end` совпадает с токеном End и закрывает блок раньше (INI.cpp:1642), '
                  'а `;` обрезает строку (INI.cpp:602–606). Разбивка по блокам — в 8.1'),
    'non_ascii': ('поддерживает', 'байты ≥ 0x80 копируются как есть (INI.cpp:609: заменяются только 1..31)'),
}

TITLES = [
    ('include', '#include'), ('define', '#define'), ('undef', '#undef'), ('other_directive', 'другие #-директивы'),
    ('arith', 'арифметика в значениях (#ADD/#MULTIPLY/… )'), ('macro_numeric', 'макрос с числовым значением в значении поля'),
    ('macro_other', 'макрос с нечисловым значением в значении поля'), ('lua_block', 'Lua BeginScript…EndScript'),
    ('slash_comment', 'комментарий //'), ('dash_comment', 'комментарий -- (вне Lua)'), ('quotes', 'кавычки "…"'),
    ('unbalanced_quotes', 'нечётное число кавычек в строке'), ('continuation', 'продолжение строки \\'),
    ('comma_values', 'запятая в значении'), ('tabs', 'табуляция'),
    ('crlf', 'CRLF'), ('lone_cr', 'одиночный CR'), ('long_lines', f'строка > {ZH_INI_MAX_CHARS_PER_LINE} символов'),
    ('non_ascii', 'байты ≥ 0x80'), ('end_case', 'End в другом регистре (END/end)'),
    ('missing_end', 'блок без End до EOF'), ('stray_end', 'лишний End на верхнем уровне'),
    ('unknown_top_block', 'блок верхнего уровня, которого нет в ZH'), ('unknown_module', 'модуль, которого нет в ZH'),
    ('unknown_nested_block', 'вложенный блок, которого нет в ZH'), ('unknown_field', 'поле, которого нет в ZH'),
]

ARITH_RX = re.compile(r'#\s*([A-Z_]+)\s*\(', re.I)
WORD_RX = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
NUM_RX = re.compile(r'^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?%?$')


def extensions(files, an, recs, schema):
    """Counts and first example of every syntax extension in one version set."""
    c, ex = Counter(), {}

    def hit(k, where, n=1):
        c[k] += n
        if k not in ex:
            fp = where.rsplit(':', 1)[0]
            ex[k] = f'{files[fp][0]}:{where}' if fp in files and not where.startswith(files[fp][0] + ':') else where
    defines = {k.upper(): v[0] for k, v in an.defines.items()}
    for p, (label, data) in sorted(files.items()):
        lua_lines = set()
        for b in lua_blocks(data):
            hit('lua_block', f'{label}:{p}:{b["line"]}')
            lua_lines.update(range(b['line'], (b['end_line'] or b['line'] + b['lines']) + 1))
        e = _eol_kind(data)
        if e['CRLF']:
            hit('crlf', f'{label}:{p}', e['CRLF'])
        if e['CR']:
            hit('lone_cr', f'{label}:{p}', e['CR'])
        for i, (off, raw, eol) in enumerate(split_lines(data), 1):
            if len(raw) > ZH_INI_MAX_CHARS_PER_LINE:
                hit('long_lines', f'{label}:{p}:{i}')
            if i in lua_lines:
                continue
            line = raw.decode('latin-1')
            where = f'{label}:{p}:{i}'
            if '\t' in line:
                hit('tabs', where)
            if any(ord(ch) >= 0x80 for ch in line):
                hit('non_ascii', where)
            code = line.split(';', 1)[0]            # ZH comment rule
            s = code.strip()
            if not s:
                continue
            tok = s.split()[0]
            tl = tok.lower()
            if tl == '#include':
                hit('include', where); continue
            if tl == '#define':
                hit('define', where)
                if ARITH_RX.search(s):
                    hit('arith', where)
                continue
            if tl == '#undef':
                hit('undef', where); continue
            if tok.startswith('#') and not ARITH_RX.match(s):
                hit('other_directive', where); continue
            if '//' in code:
                hit('slash_comment', where)
            if '--' in code:
                hit('dash_comment', where)
            if '"' in code:
                hit('quotes', where)
                if code.count('"') % 2:
                    hit('unbalanced_quotes', where)
            if s.endswith('\\'):
                hit('continuation', where)
            if tl == 'end' and tok != 'End':
                hit('end_case', where)
            rest = s[len(tok):]
            if ARITH_RX.search(rest):
                hit('arith', where)
            if ',' in rest:
                hit('comma_values', where)
            for w in WORD_RX.findall(rest):
                v = defines.get(w.upper())
                if v is None:
                    continue
                hit('macro_numeric' if NUM_RX.match(v.split()[0] if v.split() else '') else 'macro_other', where)
    for where, msg in an.warnings:
        if 'not closed by END' in msg:
            hit('missing_end', where)
        elif 'stray END' in msg:
            hit('stray_end', where)
    zh_fields = schema.zh_fields
    for (kind, name), n, where, _m in an.not_in_zh_cpp.items():
        if kind == 'block':
            hit('unknown_top_block', where, n)
        elif kind == 'module':
            hit('unknown_module', where, n)
    # field names are compared with the set of all field names in ZH parse tables (generals_zh_schema.json)
    for (tid, fk), n, where, _m in an.fields.items():
        if fk.lower() in zh_fields:
            continue
        f = schema.tables.get(tid, {}).get('fields', {}).get(fk) or {}
        if f.get('kind') == 'block':
            hit('unknown_nested_block', where, n)
        elif f.get('kind') != 'module':
            hit('unknown_field', where, n)
    nested = {k for k, *_ in an.unknown_nested.items()}
    for key, n, where, _m in an.unknown_fields.items():
        if key[1].lower() in zh_fields:
            continue
        hit('unknown_nested_block' if key in nested else 'unknown_field', where, n)
    return {k: {'count': c[k], 'example': ex.get(k), 'zh': ZH[k][0], 'zh_detail': ZH[k][1]} for k, _ in TITLES}


def build(vfs, schema, gp_expected=None):
    sets = version_sets(vfs)
    if all(sets['2.22'][p][0] == sets['base'].get(p, (None,))[0] for p in sets['2.22']) and len(sets['2.22']) == len(sets['base']):
        sets = {'base': sets['base']}           # no patch archives mounted
    res, blocks = {}, {}
    for name, files in sets.items():
        an, recs = analyze_set(schema, files)
        bl, eol = lua_report(files, recs, an.objects)
        res[name] = {'files': len(files), 'lua': lua_summary(bl, eol, gp_expected if name == '2.22' else None,
                                                             schema.zh_modules, schema.zh_fields),
                     'extensions': extensions(files, an, recs, schema)}
        blocks[name] = bl
    return {'sets': res, 'blocks': blocks}


def write(sx, out):
    import json
    import os
    with open(os.path.join(out, 'beginscript.tsv'), 'w') as f:
        f.write('set\tfield\tmodule_type\tmodule_tag\twrapper\tparent_kind\tparent_object\tmodule_at\tfirst_beginscript_line\tblocks\n')
        for name, bl in sx['blocks'].items():
            agg = {}
            for b in bl:
                k = (b['field'], b['module_type'], b['module_tag'], b['wrapper'], b['parent_kind'], b['parent'],
                     f"{b['source']}:{b['file']}:{b['module_line']}")
                if k not in agg:
                    agg[k] = [b['line'], 0]
                agg[k][1] += 1
            for k, (ln, n) in agg.items():
                f.write('\t'.join([name] + [str(x or '') for x in k] + [str(ln), str(n)]) + '\n')
    with open(os.path.join(out, 'beginscript_blocks.tsv'), 'w') as f:
        cols = ['at', 'end_line', 'field', 'module_type', 'module_tag', 'wrapper', 'owner', 'parent_kind', 'parent',
                'begin_token', 'after_begin', 'end_token', 'after_end', 'lines', 'max_line_len', 'eol',
                'lua_end_lines', 'semicolon_lines', 'nested_begin', 'endscript_substring_lines']
        f.write('set\t' + '\t'.join(cols) + '\n')
        for name, bl in sx['blocks'].items():
            for b in bl:
                row = dict(b, at=f"{b['source']}:{b['file']}:{b['line']}",
                           eol='+'.join(f'{k.encode("unicode_escape").decode()}×{v}' for k, v in b['eol'].items()),
                           nested_begin=','.join(map(str, b['nested_begin'])),
                           endscript_substring_lines=','.join(map(str, b['endscript_substring_lines'])))
                f.write(name + '\t' + '\t'.join(str(row.get(c) if row.get(c) is not None else '').replace('\t', ' ') for c in cols) + '\n')
    with open(os.path.join(out, 'syntax.json'), 'w') as f:
        json.dump(sx['sets'], f, ensure_ascii=False, indent=1)


def md_summary(sx):
    S = sx['sets']
    names = list(S)
    L = ['## 8. Встроенный Lua и синтаксис BFME, которого нет в ZH (E1-56)', '',
         'Наборы: «base» — для каждого INI самая старая версия из непатчевого архива (ini.big, maps.big); «2.22» — '
         'итоговые версии всех INI (то, что грузит игра с патчем). Файлы `desktop.ini` (служебные файлы Windows) не считаются. '
         'Таблицы: `beginscript.tsv` (по модулям), `beginscript_blocks.tsv` (по блокам), `syntax.json`.', '',
         '### 8.1. BeginScript…EndScript', '',
         '| | ' + ' | '.join(names) + ' |', '|---|' + '---|' * len(names)]

    def row(t, f):
        return f'| {t} | ' + ' | '.join(str(f(S[n]['lua'])) for n in names) + ' |'
    L += [row('блоков', lambda x: x['blocks']), row('файлов', lambda x: x['files']),
          row('модулей с блоками', lambda x: x['modules']),
          row('напрямую в модуле', lambda x: x['direct_in_module']),
          row('по обёрткам', lambda x: ', '.join(f'{k} {v}' for k, v in x['by_wrapper'].items())),
          row('по типам модулей', lambda x: ', '.join(f"{r['module_type']} {r['blocks']}" for r in x['by_module_type'])),
          row('по полям-владельцам', lambda x: ', '.join(f'{k} {v}' for k, v in x['by_owner_field'].items())),
          row('по родителям', lambda x: ', '.join(f'{k} {v}' for k, v in x['by_parent_kind'].items())),
          row('текст после BeginScript', lambda x: f"{x['after_begin_nonempty']} (все — комментарии `;`)" if all(e.startswith(';') for e in x['after_begin_examples']) else x['after_begin_nonempty']),
          row('текст после EndScript', lambda x: x['after_end_nonempty']),
          row('написание токенов', lambda x: ', '.join(f'{k} {v}' for k, v in {**x['begin_spellings'], **x['end_spellings']}.items())),
          row('переводы строк в Lua', lambda x: ', '.join(f"{k.encode('unicode_escape').decode()} {v}" for k, v in x['body_eol'].items())),
          row('переводы строк во всех INI (файлов)', lambda x: ', '.join(f'{k} {v}' for k, v in x['all_ini_eol'].items())),
          row(f'макс. длина строки Lua (лимит ZH {ZH_INI_MAX_CHARS_PER_LINE})', lambda x: f"{x['max_line_len']} (`{x['max_line_at']}`)"),
          row('строк Lua длиннее лимита ZH', lambda x: x['lines_over_zh_limit']),
          row('вложенный BeginScript', lambda x: len(x['nested_begin'])), row('EOF до EndScript', lambda x: len(x['eof_before_end'])),
          row('«endscript» внутри строки Lua', lambda x: len(x['endscript_substring'])),
          row('BeginScript вне модулей', lambda x: len(x['outside_module'])),
          row('блоков с Lua-строкой, первый идентификатор которой `end`', lambda x: x['blocks_with_lua_end_line']),
          row('… из них первый токен ZH (strtok по `" \\t\\r\\n="`, до `;`) ровно `end` → ZH закроет блок', lambda x: x['blocks_with_zh_end_token']),
          row('блоков с `;` в Lua (ZH обрежет строку)', lambda x: x['blocks_with_semicolon']),
          row('ZH: модуль-хозяин неизвестен ZH → падение', lambda x: x['zh_module_unknown']),
          row('ZH: модуль есть, поле-владелец (AnimationState…) нет → молча пропуск', lambda x: x['zh_module_known_owner_unknown']),
          row('ZH: модуль и поле-владелец есть (TransitionState) → Lua как неизвестные поля', lambda x: x['zh_module_known_owner_known'])]
    if '2.22' in S:
        x = S['2.22']['lua']
        gp = x.get('gameplay_expected')
        if gp:
            L += ['', f"Сверка с Геймплеем ({gp} в 2.22): всего блоков {x['blocks']}. Ровно {x['direct_in_module']} из них стоят "
                  f"напрямую в Draw-модуле, остальные {x['blocks'] - x['direct_in_module']} внутри `ReplaceModule`/`AddModule` "
                  f"({', '.join(f'{k} {v}' for k, v in x['by_wrapper'].items() if k != 'напрямую')}). "
                  + ('Число Геймплея совпадает с блоками напрямую в модуле: обёртки они, судя по всему, не считали.'
                     if gp == x['direct_in_module'] else 'Число Геймплея не совпадает ни с одним из этих разрезов.')]
    L += ['', 'Lua-текст хранится побайтно: тело блока — байты от конца строки BeginScript до начала строки EndScript '
          '(`syntax.lua_blocks`, проверено тестом). Конец блока — первая строка, где первый токен `EndScript` без учёта регистра.', '',
          '### 8.2. Расширения синтаксиса и что с ними делает парсер ZH (repo/, только чтение)', '',
          '| конструкция | ' + ' | '.join(names) + ' | пример | ZH | как именно |', '|---|' + '---|' * len(names) + '---|---|---|']
    for k, t in TITLES:
        e = S[names[-1]]['extensions'][k]
        exm = next((S[n]['extensions'][k]['example'] for n in reversed(names) if S[n]['extensions'][k]['example']), None)
        L.append(f'| {t} | ' + ' | '.join(str(S[n]['extensions'][k]['count']) for n in names) +
                 f" | {('`' + exm + '`') if exm else '—'} | {e['zh']} | {e['zh_detail']} |")
    L += ['', 'Счёт «нет в ZH» для полей, вложенных блоков и модулей сделан по именам из таблиц разбора ZH '
          '(`schema/generals_zh_schema.json`, собран из исходников). Поле считается известным ZH, если имя есть хотя бы в одной '
          'таблице ZH, так что это нижняя оценка. Макросы ищутся по словам в значении, совпадающим с именами `#define` этого набора.', '']
    return L
