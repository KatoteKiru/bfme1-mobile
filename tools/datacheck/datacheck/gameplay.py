"""Gameplay-oriented extras: mechanics finder (file:line), skirmish reachability, cross-check with gp-scratch.

Writes out/gameplay.md + out/gameplay.json. Re-run after the 2.22 patch archives arrive: everything is computed
from the winning VFS files, so patch versions are picked up automatically.
"""
import json
import os
import re
from collections import Counter, defaultdict

from . import recruit

FEATURES = [
    ('sell', 'Продажа построек и юнитов', [
        r'\bCommand\s*=\s*SELL\b', r'\bSell\w*\s*=', r'\bCommand_Sell\w*', r'\bSellableCommandSet\b', r'\w*Sell\w*Behavior\b',
        r'\bRefund\w*\s*=']),
    ('stance', 'Стойки (stances) и связанные команды', [
        r'(?-i:Stance|STANCE)', r'\bCommand\s*=\s*(GUARD|GUARD_\w+|TOGGLE_NO_AUTO_ACQUIRE|HORDE_TOGGLE_FORMATION|HOLD\w*|AGGRESSIVE\w*|DEFENSIVE\w*)\b']),
    ('replenish', 'Пополнение батальонов (horde replenish)', [
        r'\bReplenish\w*', r'\bBannerCarrierUpdate\b', r'\bScanHordeDistance\b', r'\bIdleSpawnRate\b',
        r'\bMeleeFree\w*SpawnTime\b', r'\bDiedRespawnTime\b', r'\bReinforce\w*Horde\w*']),
    ('farm_income', 'Доход ферм и его зависимость от соседних построек', [
        r'\bAutoDepositUpdate\b', r'\bDeposit(Amount|Timing)\b', r'\bResourceModifier\w*', r'\bTerrainResource\w*',
        r'\bSupplyBuildBorder\b', r'#define\s+\w*(FARM|FURNACE|SLAUGHTERHOUSE|LUMBERMILL)\w*MONEY\w*', r'\bModifier\s*=\s*PRODUCTION\b',
        r'\bUpgradeBonusPercent\b', r'\bResourceMultiplier\w*', r'\bResourceBonus\w*', r'\bIncome\w*', r'\bNeighbo\w*',
        r'\bMaxIncome\b']),
]

TOP_RX = re.compile(r'^([A-Za-z]\w*)(?:[ \t]+([^=\s;]+))?(?:[ \t]+([^=\s;]+))?[ \t]*(?:;.*)?$')
MODULE_RX = re.compile(r'^\s*(Behavior|Body|Draw|ClientUpdate|ClientBehavior)\b[\s=]+(\w+)\s+([^\s=]+)', re.I)


def _strip(line):
    for c in (';', '//'):
        i = line.find(c)
        if i >= 0:
            line = line[:i]
    return line


def find_features(vfs, texts, top_names=None, max_where=12):
    res = {}
    rx = {fid: re.compile('|'.join(f'(?:{p})' for p in pats), re.I) for fid, _, pats in FEATURES}
    hits = {fid: defaultdict(list) for fid, _, _ in FEATURES}
    for k, text in texts.items():
        path = vfs.files[k].path
        owner, module = None, None
        for i, line in enumerate(text.splitlines(), 1):
            s = _strip(line).rstrip()
            if not s.strip():
                continue
            if s[0] not in ' \t' and not s.lower().startswith(('end', '#')):
                m = TOP_RX.match(s)
                if m and (top_names is None or m.group(1).lower() in top_names):
                    owner, module = f'{m.group(1)} {m.group(2) or ""}'.strip() + f' ({path}:{i})', None
            mm = MODULE_RX.match(s)
            if mm:
                module = f'{mm.group(2)} {mm.group(3)}'
            for fid, rs in rx.items():
                if rs.search(s):
                    key = re.sub(r'\s+', ' ', s.strip())
                    key = re.sub(r'\s*=\s*', ' = ', key)
                    hits[fid][key].append({'where': f'{path}:{i}', 'owner': owner, 'module': module,
                                           'source': vfs.files[k].source})
    for fid, title, pats in FEATURES:
        rows = sorted(hits[fid].items(), key=lambda kv: (-len(kv[1]), kv[0].lower()))
        res[fid] = {'title': title, 'patterns': pats, 'distinct': len(rows),
                    'hits': [{'text': t, 'count': len(w), 'where': w[:max_where]} for t, w in rows]}
    return res


def gp_crosscheck(gp_dir, an, vfs, texts, records):
    """Compare with the gameplay engineer's raw-regex counts (gp-scratch/counts.json, freq.tsv)."""
    cj = os.path.join(gp_dir, 'counts.json')
    if not os.path.exists(cj):
        return None
    gp = json.load(open(cj)).get('mods', {})
    obj_slots = {'behavior', 'body', 'draw', 'clientupdate', 'clientbehavior'}
    mine, particle, tables = Counter(), Counter(), defaultdict(set)
    for key, field, raw, val, where in records:
        if field != '<module>' or not where.lower().startswith('data\\ini'):
            continue
        mtype = (raw.split() or [''])[0]
        lf = key[-1][0]
        top = key[0][0].lower()
        tables[mtype].add(f'{key[0][0]}.{lf}')
        if top == 'fxparticlesystem' or lf not in obj_slots:
            particle[mtype] += 1
        else:
            mine[mtype] += 1
    # their regex (bigcount.py) requires "=": count module lines written without "=" (engine accepts them)
    no_eq = Counter()
    no_eq_where = defaultdict(list)
    nested_obj = []
    data_keys = [k for k in texts if k.startswith('data\\ini')]
    obj_rx = re.compile(r'^\s+(Object|ChildObject|ObjectReskin)\s+\w+')
    for k in data_keys:
        for i, line in enumerate(texts[k].splitlines(), 1):
            s = _strip(line)
            m = re.match(r'^\s*(Behavior|Body|Draw|ClientUpdate|ClientBehavior)([\s=]+)(\w+)', s)
            if m and m.group(2).count('=') != 1:
                no_eq[m.group(3)] += 1
                no_eq_where[m.group(3)].append(f'{vfs.files[k].path}:{i} `{s.strip()[:60]}`')
            if obj_rx.match(s):
                nested_obj.append(f'{vfs.files[k].path}:{i} {s.strip()}')
    rows = []
    for name in sorted(set(mine) | set(gp) | set(particle)):
        a, b, p = mine.get(name, 0), gp.get(name, 0), particle.get(name, 0)
        if a + p == b:
            continue
        why = []
        if p:
            why.append(f'модуль частиц FXParticleSystem ({p}); их регулярка ищет только Behavior/Body/Draw/ClientUpdate/ClientBehavior')
        if no_eq.get(name):
            why.append(f'{no_eq[name]} строк с разделителем не «=» (без «=» или «==», движок принимает: '
                       f'разделители токенайзера пробел, «=», таб), напр. {no_eq_where[name][0]}')
        if a - no_eq.get(name, 0) + p != b and not p:
            why.append('не объяснено')
        rows.append({'module': name, 'datacheck': a + p, 'gp': b, 'why': '; '.join(why)})
    freq = os.path.join(gp_dir, 'freq.tsv')
    cls_rows = []
    if os.path.exists(freq):
        for line in open(freq, encoding='utf-8'):
            parts = line.rstrip('\n').split('\t')
            if len(parts) < 3:
                continue
            name, cls = parts[1], parts[2]
            in_os = name in mine or name in particle
            if cls == 'НЕТ_В_OS' and in_os:
                tbl = [mt for (mt, n) in an.modules.count if n == name] or sorted(tables[name])
                cls_rows.append({'module': name, 'gp': cls, 'datacheck': f'есть в OpenSAGE: {", ".join(sorted(set(tbl)))}'})
    return {'module_rows': rows, 'class_rows': cls_rows, 'nested_object_lines': nested_obj,
            'gp_distinct': len(gp), 'gp_sum': sum(gp.values())}


def sell_holders(records, final, objects):
    """Objects whose (final) CommandSet or CommandSetUpgrade sets contain a button with Command = SELL, by class."""
    r = recruit.Recruit(records, final, objects)
    sell_btn = {b for b, f in r.blocks['commandbutton'].items()
                if any(x[0].lower() == 'command' and x[1].strip().lower() == 'sell' for x in f)}
    out = defaultdict(list)
    for ln in r.final:
        if ln == 'defaultthingtemplate':
            continue
        sets = [t for k, t, w in r.object_edges(ln) if k == 'commandset']
        if any(e[1].lower() in sell_btn for cs in sets for e in r.commandset_edges(cs)):
            name = r.obj_by_name[ln]['name'] if ln in r.obj_by_name else ln
            out[r.classify(ln)].append(name)
    return {k: sorted(v) for k, v in out.items()}


def conclusions(feat, sells):
    def n(fid, rx):
        return sum(h['count'] for h in feat[fid]['hits'] if re.search(rx, h['text']))
    c = {}
    nonstruct = {k: len(v) for k, v in sells.items() if k != 'structure'}
    c['sell'] = (f"Кнопка `Command = SELL` есть у {len(sells.get('structure', []))} построек; у не-построек: "
                 f"{nonstruct or 'нет'}. Полей `Sell*` вне GameData.SellPercentage: {n('sell', r'^Sell(?!Percentage)')}.")
    c['stance'] = (f"Упоминаний `Stance`/`STANCE`, кроме анимаций: {n('stance', r'(?-i:Stance|STANCE)') - n('stance', r'^Animation')}. "
                   f"Близкие команды: GUARD {n('stance', r'= GUARD$')}, TOGGLE_NO_AUTO_ACQUIRE {n('stance', r'TOGGLE_NO_AUTO_ACQUIRE')}, "
                   f"HORDE_TOGGLE_FORMATION {n('stance', r'HORDE_TOGGLE_FORMATION')}.")
    c['replenish'] = (f"BannerCarrierUpdate: {n('replenish', r'BannerCarrierUpdate')}, ReplenishNearbyHorde: {n('replenish', r'ReplenishNearbyHorde')}, "
                      f"ReplenishUnitsBehavior: {n('replenish', r'ReplenishUnitsBehavior')}.")
    c['farm_income'] = (f"AutoDepositUpdate: {n('farm_income', r'AutoDepositUpdate')}; `ResourceModifier*`: {n('farm_income', r'(?i)ResourceModifier')}; "
                        f"`TerrainResource*`: {n('farm_income', r'(?i)TerrainResource')}; `SupplyBuildBorder`: {n('farm_income', r'SupplyBuildBorder')}; "
                        f"`Modifier = PRODUCTION`: {n('farm_income', r'Modifier = PRODUCTION')}.")
    return c


def build(vfs, an, final, records, texts, gp_dir, top_names=None):
    data_texts = {k: t for k, t in texts.items() if k.endswith('.ini')}
    feat = find_features(vfs, data_texts, top_names)
    sells = sell_holders(records, final, an.objects)
    return {'features': feat, 'conclusions': conclusions(feat, sells), 'sell_holders': sells,
            'skirmish': recruit.build(records, final, an.objects, recruit.read_bases(vfs, final.keys())),
            'gp_crosscheck': gp_crosscheck(gp_dir, an, vfs, data_texts, records) if gp_dir else None}


def _fmt_mods(pairs, n):
    return ', '.join(f'{f}={t} {c}' for (f, t), c in pairs[:n])


def write(g, out, top=20):
    with open(os.path.join(out, 'gameplay.json'), 'w') as f:  # noqa
        json.dump(g, f, ensure_ascii=False, indent=1, default=lambda o: dict(o) if isinstance(o, Counter) else str(o))
    L = ['# datacheck: данные для геймплея', '',
         'Генерируется `python3 -m datacheck`. Всё считается по итоговым (выигравшим в VFS) файлам, поэтому после '
         'копирования `_patch222*.big` достаточно перезапустить.', '']
    L += ['## 1. Механики: где они в данных (файл:строка)', '']
    for fid, d in g['features'].items():
        L += [f'### {d["title"]}', '', f'Шаблоны: `' + '`, `'.join(d['patterns']) + '`', '',
              f'**Итог по данным:** {g["conclusions"].get(fid, "")}', '']
        if not d['hits']:
            L += ['Совпадений нет.', '']
            continue
        L += ['| строка (нормализовано) | раз | где (первые) | объект/блок | модуль |', '|---|---|---|---|---|']
        for h in d['hits'][:40]:
            w = h['where'][0]
            L.append(f"| `{h['text'][:80]}` | {h['count']} | {', '.join(x['where'] for x in h['where'][:3])} | "
                     f"{w['owner'] or ''} | {w['module'] or ''} |")
        if len(d['hits']) > 40:
            L.append(f'| … ещё {len(d["hits"]) - 40} различных строк, см. gameplay.json | | | | |')
        L.append('')
    L += ['## 2. Скирмиш: что фракция реально может получить', '',
          'Граф: PlayerTemplate (PlayableSide=Yes) → BuildableHeroesMP / StartingBuilding / StartingUnit* и объекты из '
          '`CastleToUnpackForFaction = <Side> X` → CommandSet (с наследованием, плюс CommandSet/CommandSetAlt модулей '
          'CommandSetUpgrade) → CommandButton (UNIT_BUILD, FOUNDATION_CONSTRUCT, DOZER_CONSTRUCT, CASTLE_UNPACK_EXPLICIT_OBJECT, '
          'CASTLE_UPGRADE, REVIVE) → Object, а также HordeContain `RankInfo UnitType:` и `InitialPayload`. Рекурсивно. '
          'Дополнительные корни: участки и флаги (объекты, у которых CommandSet содержит FOUNDATION_CONSTRUCT/CASTLE_UNPACK*). '
          'Объекты чужой стороны (`Side` не совпадает) отбрасываются. Раскладки крепостей и лагерей (`Camp_*`, `Castle_*` из '
          '`CastleToUnpackForFaction`) — не INI, а `bases\\<имя>\\<имя>.bse` (есть только в `_patch222bases.big`): если они '
          'смонтированы, в граф идут объекты, расставленные в них (эвристика: имена шаблонов в распакованном файле). '
          'Не прослеживаются: спавн через OCL, призыв заклинаниями, объекты карт.', '']
    L += ['| PlayerTemplate | Side | достижимо | герои | орды | юниты | постройки | прочее | отброшено (чужая сторона) | не найдено ссылок |',
          '|---|---|---|---|---|---|---|---|---|---|']
    for pt, d in g['skirmish'].items():
        bc = d['by_class']
        L.append(f"| {pt} | {d['side']} | {d['reachable']} | {bc.get('hero', 0)} | {bc.get('horde', 0)} | {bc.get('unit', 0)} | "
                 f"{bc.get('structure', 0)} | {bc.get('other', 0)} | {d['other_side_dropped']} | {len(d['missing_refs'])} |")
    L.append('')
    for pt, d in g['skirmish'].items():
        L += [f'**{pt}**: герои: {", ".join(d["heroes"]) or "—"}.', '',
              f'Модули у юнитов, орд и героев (число объектов): {_fmt_mods(d["units_and_heroes_modules"], top)}.', '']
        if d.get('base_layouts_used'):
            L += [f'Раскладки .bse в графе: {len(d["base_layouts_used"])} ({", ".join(d["base_layouts_used"][:8])}…).', '']
        miss = ', '.join(f'{k}' for k in list(d['missing_refs'])[:10])
        if miss:
            L += [f'Ссылки на отсутствующие объекты: {miss}.', '']
    L += ['Частоты по объектам карт 1v1 2.22 будут посчитаны, когда приедет `_patch222maps.big` (пока есть только базовый '
          '`maps.big`, а список карт 1v1 2.22 надо взять из патча).', '']
    gc = g.get('gp_crosscheck')
    L += ['## 3. Сверка с gp-scratch (--gp-scratch / BFME_GP_SCRATCH)', '']
    if not gc:
        L += ['gp-scratch не найден.', '']
    else:
        L += [f'gp-scratch (`bigcount.py`, регулярка по сырому тексту): {gc["gp_distinct"]} модулей, {gc["gp_sum"]} объявлений. '
              'datacheck: сырые объявления в data\\ini (до наследования), включая модули частиц.', '',
              '| модуль | datacheck | gp | причина |', '|---|---|---|---|']
        for r in gc['module_rows']:
            L.append(f"| {r['module']} | {r['datacheck']} | {r['gp']} | {r['why']} |")
        L.append('')
        if gc['class_rows']:
            L += ['Классификация в `freq.tsv`, с которой datacheck не согласен:', '']
            for r in gc['class_rows']:
                L.append(f"- {r['module']}: gp = {r['gp']}, datacheck: {r['datacheck']}")
            L.append('')
        L += [f'Счёт объектов: регулярка gp без привязки к верхнему уровню находит ещё {len(gc["nested_object_lines"])} строк '
              f'`Object X` внутри других блоков (не игровые объекты), напр. ' +
              '; '.join(gc['nested_object_lines'][:3]) + '.', '']
    with open(os.path.join(out, 'gameplay.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')
