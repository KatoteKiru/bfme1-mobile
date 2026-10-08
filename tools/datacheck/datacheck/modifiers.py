"""ModifierList 'Modifier = TYPE value' lines of attributemodifier.ini (every version), with #define resolution.

Used to answer how BFME percentages such as "PRODUCTION 1.2 %" are meant: compare each modifier type's value range.
"""
import re
from collections import defaultdict

DEF_RX = re.compile(r'^\s*#define\s+(\w+)\s+([^;\r\n]*)', re.I)
BLK_RX = re.compile(r'^\s*ModifierList\s+(\S+)', re.I)
MOD_RX = re.compile(r'^\s*Modifier\s*=?\s*(\w+)\s+([^;\r\n]*?)\s*(?:;(.*))?$', re.I)


def _defines(files):
    d = {}
    for p, (label, data) in files.items():
        for i, line in enumerate(data.decode('latin-1').splitlines(), 1):
            m = DEF_RX.match(line)
            if m:
                d[m.group(1).upper()] = (m.group(2).strip(), f'{label}:{p}:{i}')
    return d


def scan(sets, name='attributemodifier.ini'):
    """sets: syntax.version_sets(); returns {set: [modifier rows]}."""
    out = {}
    for sname, files in sets.items():
        defs = _defines(files)
        rows = []
        for p, (label, data) in sorted(files.items()):
            if not p.lower().endswith(name):
                continue
            cur = None
            for i, line in enumerate(data.decode('latin-1').splitlines(), 1):
                m = BLK_RX.match(line)
                if m:
                    cur = m.group(1)
                    continue
                m = MOD_RX.match(line)
                if not m or cur is None:
                    continue
                mtype, raw = m.group(1).upper(), m.group(2).strip()
                toks = [t for t in re.split(r'[ \t=]+', raw) if t]   # split like the ZH tokenizer default seps " \n\r\t="
                tok = toks[0] if toks else ''
                macro = defs.get(tok.upper())
                val = macro[0].split()[0] if macro and macro[0].split() else tok
                try:
                    num = float(val.rstrip('%'))
                except ValueError:
                    num = None
                pct_attached = '%' in tok                # hypothesis from the data: '%' attached to the token -> /100
                rows.append({'list': cur, 'type': mtype, 'raw': raw, 'token': tok, 'number': num,
                             'macro': (tok, macro[0], macro[1]) if macro else None, 'at': f'{label}:{p}:{i}',
                             'comment': (m.group(3) or '').strip(),
                             'hypothesis': (num / 100 if pct_attached else num) if num is not None else None,
                             'opensage': num / 100 if num is not None else None})
        out[sname] = rows
    return out


def by_type(rows):
    g = defaultdict(list)
    for r in rows:
        g[r['type']].append(r)
    res = {}
    for t, rs in sorted(g.items()):
        nums = sorted({r['number'] for r in rs if r['number'] is not None})
        res[t] = {'count': len(rs), 'values': nums, 'min': nums[0] if nums else None, 'max': nums[-1] if nums else None,
                  'below_5': sum(1 for r in rs if r['number'] is not None and abs(r['number']) < 5),
                  'example': rs[0]['at']}
    return res


RULE = ('Как читается `%`. Это наблюдение по данным, **не подтверждено, ждёт замера в игре**. '
        'OpenSAGE (`Data/Ini/IniParser.cs` ParsePercentage, `Logic/ModifierList.cs`) всегда делит число на 100. '
        'Для записей вида `200%` это даёт ×2, что сходится с комментариями в самих данных. '
        'Для записей, где `%` стоит отдельным токеном после числа или макроса (`X %`), деление на 100 дало бы множитель около 0,01, '
        'а комментарии в данных говорят о мультипликативном модификаторе порядка X. '
        'Поэтому колонка «гипотеза» берёт значение как есть, если `%` отделён пробелом, и делит на 100, если `%` слит с числом. '
        'Колонка «OpenSAGE» показывает, что даёт OpenSAGE.')


def md_summary(mods):
    names = list(mods)
    L = ['## 9. `Modifier = PRODUCTION …` в attributemodifier.ini: как читается процент', '', RULE, '',
         'Сравнение шкал по типам модификаторов (число до `%`/после подстановки макроса; «<5» — сколько строк со значением '
         'по модулю меньше 5, то есть без `%`-шкалы):', '',
         '| тип | ' + ' | '.join(f'{n}: строк / <5 / значения' for n in names) + ' |', '|---|' + '---|' * len(names)]
    types = sorted({r['type'] for n in names for r in mods[n]})
    bt = {n: by_type(mods[n]) for n in names}
    for t in types:
        L.append(f'| {t} | ' + ' | '.join(
            (f"{bt[n][t]['count']} / {bt[n][t]['below_5']} / " + ', '.join(f'{v:g}' for v in bt[n][t]['values'][:10]) +
             ('…' if len(bt[n][t]['values']) > 10 else '')) if t in bt[n] else '—' for n in names) + ' |')
    for n in names:
        rows = [r for r in mods[n] if r['type'] == 'PRODUCTION']
        L += ['', f'PRODUCTION, набор {n} ({len(rows)} строк): значение по гипотезе (не подтверждено) и по OpenSAGE (всегда /100):', '',
              '| ModifierList | запись | макрос | гипотеза | OpenSAGE | где | комментарий |', '|---|---|---|---|---|---|---|']
        for r in rows:
            mac = f"`{r['macro'][0]}` = {r['macro'][1]} (`{r['macro'][2]}`)" if r['macro'] else '—'
            L.append(f"| {r['list']} | `{r['raw']}` | {mac} | ×{r['hypothesis']:g} | ×{r['opensage']:g} | `{r['at']}` | {r['comment'] or ''} |"
                     if r['hypothesis'] is not None else f"| {r['list']} | `{r['raw']}` | {mac} | ? | ? | `{r['at']}` | {r['comment'] or ''} |")
    L.append('')
    return L
