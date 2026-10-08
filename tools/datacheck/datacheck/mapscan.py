"""Map contents for gameplay: placed objects, script action/condition names, start positions, mapcache.

* ObjectsList/Object (ZH WorldHeightMap/MapObject, chunk v3): x, y, z, angle (f32), flags (i32), template name
  (u16 + ascii), then a Dict.  Dict = u16 count, then per pair u32 keyAndType (key = TOC id = value >> 8,
  type = value & 0xff: 0 bool u8, 1 int i32, 2 real f32, 3 ascii u16+bytes, 4 unicode u16+2*len) - ZH DataChunk.cpp
  readDict().
* Start positions: waypoint objects (template "*Waypoints/Waypoint") whose dict waypointName is Player_N_Start.
  ZH counts them like WaypointMap::update() (Core/GameEngine/Source/GameClient/MapUtil.cpp:325-341: consecutive
  Player_1_Start.. up to MAX_SLOTS, at least 1) and sets numPlayers = that, isMultiplayer = numPlayers >= 2 (:672-673).
* Scripts: names of Condition / ScriptAction / ScriptActionFalse = TOC[nameKey >> 8] from the first 8 bytes of the
  chunk body (<iI = type, nameKey), found with the heuristic sub-chunk walk of datacheck/chunks.py inside the
  top-level PlayerScriptsList chunk (same as the docs/formats/data/bfme1_script_names_maps_big.tsv generator).
* maps\\mapcache.ini: MapCache blocks, block name = map path with "_XX" hex escapes; numPlayers, isMultiplayer,
  fileSize, Player_N_Start.
"""
import os
import re
import struct
from collections import Counter, defaultdict

from .maps import probe

START_RX = re.compile(r'^Player_(\d+)_Start$', re.I)
WAYPOINT = '*waypoints/waypoint'
MAX_SLOTS = 8   # ZH NetworkDefs.h:60 MAX_SLOTS = MAX_PLAYER+1
ROAD_FLAGS = 0x2 | 0x4   # FLAG_ROAD_POINT1 | FLAG_ROAD_POINT2, Core/GameEngine/Include/Common/MapObject.h:63-64


def read_dict(d, p):
    n = struct.unpack_from('<H', d, p)[0]; p += 2
    out = {}
    for _ in range(n):
        kt = struct.unpack_from('<I', d, p)[0]; p += 4
        t, k = kt & 0xff, kt >> 8
        if t == 0:
            val = d[p]; p += 1
        elif t == 1:
            val = struct.unpack_from('<i', d, p)[0]; p += 4
        elif t == 2:
            val = struct.unpack_from('<f', d, p)[0]; p += 4
        elif t == 3:
            ln = struct.unpack_from('<H', d, p)[0]
            val = d[p + 2:p + 2 + ln].decode('latin-1'); p += 2 + ln
        elif t == 4:
            ln = struct.unpack_from('<H', d, p)[0]
            val = d[p + 2:p + 2 + 2 * ln].decode('utf-16-le', 'replace'); p += 2 + 2 * ln
        else:
            raise ValueError(f'dict type {t}')
        out[k] = val
    return out, p


def objects(d, toc, b, s):
    """[(template, flags, {key name: value})] of ObjectsList body."""
    res, q, end = [], b, b + s
    while q < end:
        cid, ver, size = probe.hdr(d, q)
        o = q + 10
        if toc.get(cid) == 'Object':
            flags, ln = struct.unpack_from('<iH', d, o + 16)
            name = d[o + 22:o + 22 + ln].decode('latin-1')
            dd, e = read_dict(d, o + 22 + ln)
            if e != o + size:
                raise ValueError(f'Object {name}: dict ends at {e - o} of {size}')
            res.append((name, flags, {toc.get(k, k): v for k, v in dd.items()}))
        q = o + size
    return res


def script_names(d, toc, b, s):
    stats = defaultdict(Counter)
    probe.walk(d, toc, b - 10, b + s, [], stats)
    return Counter(stats.get('__names__', {}))


def content(d, toc, tops):
    """tops: probe.top_chunks(); returns objects Counter, start waypoints, script names Counter."""
    objs, roads, starts, scripts, waypoints = Counter(), Counter(), set(), Counter(), 0
    for name, v, s, b in tops:
        if name == 'ObjectsList':
            for t, flags, dd in objects(d, toc, b, s):
                if flags & ROAD_FLAGS:      # road segment end points refer to Road blocks (roads.ini), not Objects
                    roads[t] += 1
                    continue
                objs[t] += 1
                if t.lower() == WAYPOINT:
                    waypoints += 1
                    m = START_RX.match(str(dd.get('waypointName', '')))
                    if m:
                        starts.add(int(m.group(1)))
        elif name == 'PlayerScriptsList':
            scripts += script_names(d, toc, b, s)
    n = 0
    while n < MAX_SLOTS and (n + 1) in starts:   # MapUtil.cpp:325-341: consecutive Player_1.._N_Start, min 1
        n += 1
    sc = {'ACT': {}, 'COND': {}}
    for (kind, nm), c in sorted(scripts.items()):
        sc[kind][nm] = c
    return {'objects': dict(sorted(objs.items())), 'road_points': dict(sorted(roads.items())), 'starts': sorted(starts), 'num_start_spots': max(1, n),
            'waypoints': waypoints, 'scripts': sc}


def _unescape(name):
    return re.sub(r'_([0-9A-Fa-f]{2})', lambda m: chr(int(m.group(1), 16)), name)


def mapcache(text):
    """{lower map path: {...}} from maps\\mapcache.ini text."""
    res, cur = {}, None
    for i, line in enumerate(text.splitlines(), 1):
        t = line.split(';', 1)[0].strip()
        if not t:
            continue
        m = re.match(r'MapCache\s+(\S+)', t)
        if m:
            cur = {'line': i, 'starts': []}
            res[_unescape(m.group(1)).lower()] = cur
            continue
        if t.lower() == 'end':
            cur = None
            continue
        if cur is None or '=' not in t:
            continue
        k, v = (x.strip() for x in t.split('=', 1))
        kl = k.lower()
        if kl in ('numplayers', 'filesize', 'filecrc'):
            cur[kl] = int(v)
        elif kl in ('ismultiplayer', 'isofficial', 'isscenariomp'):
            cur[kl] = v.lower() == 'yes'
        elif START_RX.match(k):
            cur['starts'].append(int(START_RX.match(k).group(1)))
    return res


# ---------------------------------------------------------------------------------------------------------------
# summary for gameplay: 1v1 maps, frequencies, script names vs ZH and vs the base game
SITE_KINDS = ('BASE_SITE', 'BASE_FOUNDATION', 'BASE_DEFENSE_FOUNDATION')
DOCS_TSV = os.path.join('docs', 'formats', 'data', 'bfme1_script_names_maps_big.tsv')


def kindof_map(records, objects):
    """lower object name -> KindOf string (own or inherited through ChildObject/ObjectReskin parents), data\\ini only."""
    own, parent = {}, {}
    for o in objects:
        if o['file'].lower().startswith('data\\ini'):
            parent[o['name'].lower()] = (o['parent'] or '').lower()
    for key, field, val, res, where in records:
        if (len(key) == 2 and key[0][0] in ('Object', 'ChildObject', 'ObjectReskin') and field.lower() == 'kindof'
                and where.lower().startswith('data\\ini')):
            own[key[0][1]] = res
    out = {}
    for n in parent:
        k, seen = n, set()
        while k and k not in own and k in parent and k not in seen:
            seen.add(k)
            k = parent[k]
        out[n] = own.get(k)
    return out


def _agg(items):
    inst, maps = Counter(), Counter()
    for d in items:
        for k, c in d.items():
            inst[k] += c
            maps[k] += 1
    return inst, maps


def _script_sets(contents):
    res = {}
    for kind in ('ACT', 'COND'):
        res[kind] = _agg(c['scripts'][kind] for c in contents)
    return res


def read_docs_tsv(path):
    if not os.path.isfile(path):
        return None
    out = {'ACT': {}, 'COND': {}}
    with open(path, encoding='utf-8') as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) == 3 and p[0] in out:
                out[p[0]][p[1]] = int(p[2])
    return out


def summarize(m, cache, cache_where, kindof, final, zh, docs_tsv=None):
    maps_ = {k: r for k, r in m['maps'].items() if r['group'] == 'maps' and 'content' in r}
    agree = sum(1 for k, r in maps_.items() if k.lower() in cache and cache[k.lower()].get('numplayers') == r['content']['num_start_spots'])
    in_cache = sum(1 for k in maps_ if k.lower() in cache)
    one, excluded = [], []
    for k, r in sorted(maps_.items()):
        if r['content']['num_start_spots'] != 2:
            continue
        e = cache.get(k.lower())
        if e is None:
            excluded.append((k, 'нет в mapcache.ini'))
        elif not e.get('ismultiplayer'):
            excluded.append((k, 'isMultiplayer = no'))
        elif not e.get('isofficial'):
            excluded.append((k, 'isOfficial = no'))
        else:
            one.append(k)
    cont = [maps_[k]['content'] for k in one]
    oi, om = _agg(c['objects'] for c in cont)
    ti, tm = _agg(Counter(maps_[k]['texture_classes']) for k in one)
    ri, rm = _agg(c.get('road_points', {}) for c in cont)
    spawn = {n.lower() for n, mods in final.items() if any(x['type'] == 'SpawnBehavior' for x in mods.values())}
    fin = {n.lower() for n in final}
    objs = []
    for t, c in oi.most_common():
        kd = kindof.get(t.lower())
        cat = ('стартовые/служебные' if t.lower() == WAYPOINT else
               'участок застройки' if kd and any(x in kd.split() for x in SITE_KINDS) else
               'логово (SpawnBehavior)' if t.lower() in spawn else '')
        objs.append({'template': t, 'instances': c, 'maps': om[t], 'kindof': kd, 'resource': cat,
                     'defined_in_data_ini': t.lower() in fin or t.lower() == WAYPOINT})
    sc_one = _script_sets(cont)
    sc_all = _script_sets([r['content'] for r in maps_.values()])
    sc_allgroups = _script_sets([r['content'] for r in m['maps'].values() if 'content' in r])
    base = list(m.get('base_contents', {}).values())
    sc_base = _script_sets(base)
    zhs = {'ACT': set(zh.get('script_actions', [])), 'COND': set(zh.get('script_conditions', []))}
    docs = read_docs_tsv(docs_tsv) if docs_tsv else None
    cmp = {}
    for kind in ('ACT', 'COND'):
        base_names = set(sc_base[kind][0])
        gp = set(docs[kind]) - zhs[kind] if docs else set(base_names) - zhs[kind]
        final_names, one_names = set(sc_allgroups[kind][0]), set(sc_one[kind][0])
        new_all = final_names - base_names
        cmp[kind] = {
            'zh_total': len(zhs[kind]), 'base_names': len(base_names),
            'base_not_in_zh': len(base_names - zhs[kind]),
            'gameplay_set': len(gp), 'gameplay_set_source': 'docs TSV' if docs else 'base maps',
            'docs_tsv_equal_to_regenerated': (docs[kind] == dict(sc_base[kind][0])) if docs else None,
            'gameplay_used_on_222_maps': len(gp & final_names), 'gameplay_used_on_222_1v1': len(gp & one_names),
            'gameplay_not_used_on_222': sorted(gp - final_names),
            'final_names': len(final_names), 'final_1v1_names': len(one_names),
            'new_in_222': sorted(new_all), 'new_in_222_not_in_zh': sorted(new_all - zhs[kind]),
            'new_in_222_used_on_1v1': sorted((new_all - zhs[kind]) & one_names),
            '1v1_not_in_zh': sorted(one_names - zhs[kind]),
        }
    blend = {k: sum(maps_[x][k] for x in one) for k in
             ('dangling_tiles', 'dangling_blend', 'dangling_3way', 'dangling_cliff', 'flags_outside_0x3',
              'cells_using_flags_outside_0x3', 'dir_bytes_not_01', 'edge_class_without_classes',
              'edge_class_out_of_range', 'flag_val_mismatch')}
    return {
        'method': 'num_start_spots == 2 (Player_1_Start, Player_2_Start waypoints in ObjectsList, counted as ZH '
                  'MapUtil.cpp:325-341) AND maps\\mapcache.ini isMultiplayer = yes AND isOfficial = yes',
        'mapcache': cache_where, 'maps_total': len(maps_), 'maps_in_cache': in_cache,
        'cache_numplayers_agree': agree, 'cache_size_mismatch': sum(
            1 for k, r in maps_.items() if k.lower() in cache and cache[k.lower()].get('filesize') != r.get('size')),
        'start_spot_hist': dict(sorted(Counter(r['content']['num_start_spots'] for r in maps_.values()).items())),
        'maps_1v1': [{'map': k, 'source': maps_[k]['source'], 'cache_line': cache[k.lower()]['line'],
                      'objects': sum(maps_[k]['content']['objects'].values()),
                      'scripts': sum(sum(x.values()) for x in maps_[k]['content']['scripts'].values())} for k in one],
        'excluded_2_start': excluded, 'objects_1v1': objs,
        'road_points_1v1': [{'road': t, 'points': c, 'maps': rm[t]} for t, c in ri.most_common()],
        'textures_1v1': [{'texture': t, 'entries': c, 'maps': tm[t]} for t, c in ti.most_common()],
        'scripts_1v1': {k: [{'name': n, 'count': c, 'maps': v[1][n], 'in_zh': n in zhs[k]} for n, c in v[0].most_common()]
                        for k, v in sc_one.items()},
        'scripts_all_final': {k: dict(v[0]) for k, v in sc_allgroups.items()},
        'scripts_all_final_maps': {k: dict(v[1]) for k, v in sc_allgroups.items()},
        'scripts_base': {k: dict(v[0]) for k, v in sc_base.items()},
        'script_compare': cmp, 'blend_1v1': blend,
        '_zh': {k: sorted(v) for k, v in zhs.items()},
    }


def write(s, out, top=30):
    import json
    with open(os.path.join(out, 'map_content.json'), 'w') as f:
        json.dump(s, f, ensure_ascii=False, indent=1)
    # script names of the base-game maps, same format as docs/formats/data/bfme1_script_names_maps_big.tsv
    with open(os.path.join(out, 'bfme1_script_names_maps_big.tsv'), 'w') as f:
        for kind in ('ACT', 'COND'):
            for n, c in sorted(s['scripts_base'][kind].items()):
                f.write(f'{kind}\t{n}\t{c}\n')
    one = {k: {r['name']: r for r in v} for k, v in s['scripts_1v1'].items()}
    zh_ = {k: set(v) for k, v in s['_zh'].items()}
    with open(os.path.join(out, 'script_names.tsv'), 'w') as f:
        f.write('kind\tname\tcount_final_maps\tmaps_final\tcount_1v1\tmaps_1v1\tcount_base_maps\tin_zh\n')
        for kind in ('ACT', 'COND'):
            names = set(s['scripts_all_final'][kind]) | set(s['scripts_base'][kind])
            for n in sorted(names):
                o = one[kind].get(n)
                f.write(f"{kind}\t{n}\t{s['scripts_all_final'][kind].get(n, 0)}\t{s['scripts_all_final_maps'][kind].get(n, 0)}\t"
                        f"{o['count'] if o else 0}\t{o['maps'] if o else 0}\t{s['scripts_base'][kind].get(n, 0)}\t"
                        f"{'yes' if n in zh_[kind] else 'no'}\n")
    with open(os.path.join(out, 'objects_1v1.tsv'), 'w') as f:
        f.write('template\tinstances\tmaps\tresource\tdefined_in_data_ini\tkindof\n')
        for o in s['objects_1v1']:
            f.write(f"{o['template']}\t{o['instances']}\t{o['maps']}\t{o['resource']}\t{'yes' if o['defined_in_data_ini'] else 'no'}\t{o['kindof'] or ''}\n")
    L = md_summary(s, top)
    L[0] = '# Карты 1v1: объекты, скрипты, ресурсы'
    L += ['', '## Список карт 1v1', '', '| карта | архив | строка mapcache.ini | объектов | вызовов скриптов |', '|---|---|---|---|---|']
    L += [f"| `{x['map']}` | {x['source']} | {x['cache_line']} | {x['objects']} | {x['scripts']} |" for x in s['maps_1v1']]
    L += ['', '## Карты с 2 стартовыми позициями, не вошедшие в 1v1', '']
    L += [f'- `{k}`: {why}' for k, why in s['excluded_2_start']]
    with open(os.path.join(out, 'map_content.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')


def md_summary(s, top=30):
    n1 = len(s['maps_1v1'])
    L = ['## 7. Карты 1v1: объекты, скрипты, ресурсы', '',
         f"Как определялись 1v1: в каждой итоговой `.map` из `maps\\` считаются waypoint-объекты `Player_N_Start` "
         f"(подряд с 1, как `WaypointMap::update`, ZH `Core/GameEngine/Source/GameClient/MapUtil.cpp:325–341`, "
         f"numPlayers = это число, :672). Затем это сверяется с `{s['mapcache']}`: из {s['maps_total']} карт там есть "
         f"{s['maps_in_cache']}, numPlayers совпадает у {s['cache_numplayers_agree']}. fileSize в кэше не совпадает с файлом "
         f"у {s['cache_size_mismatch']} карт: кэш собран по другим версиям файлов, поэтому число игроков берётся из самой карты. "
         f"В 1v1 входят карты с 2 стартовыми позициями, у которых в mapcache `isMultiplayer = yes` и `isOfficial = yes`. "
         f"Так отсекаются кооп-кампании, тестовые и служебные карты. Распределение стартовых позиций: {s['start_spot_hist']}. "
         f"Получилось **{n1} карт 1v1**, ещё {len(s['excluded_2_start'])} карт с 2 позициями исключены "
         f"(список в `map_content.md`).", '']
    res = Counter()
    for o in s['objects_1v1']:
        if o['resource']:
            res[o['resource']] += o['instances']
    undef = [o for o in s['objects_1v1'] if not o['defined_in_data_ini']]
    L += [f"Объекты: {len(s['objects_1v1'])} шаблонов, {sum(o['instances'] for o in s['objects_1v1'])} экземпляров. "
          f"Не определены в `data\\ini`: {len(undef)} шаблонов, {sum(o['instances'] for o in undef)} экз. "
          f"({', '.join('`' + (o['template'] or '<пусто>') + '`' for o in undef[:12])}…): в данных этого прогона нет такого Object, "
          f"ZH такие объекты молча пропускает (`GameLogic.cpp:2025–2026`: `if( thingTemplate == nullptr ) continue;`). Полный список в `objects_1v1.tsv`. "
          f"Точки дорог (флаги FLAG_ROAD_POINT1/2) посчитаны отдельно и в объекты не входят: "
          f"{sum(r['points'] for r in s['road_points_1v1'])} точек, {len(s['road_points_1v1'])} типов Road.", '',
          '| шаблон | экз. | карт | ресурс |', '|---|---|---|---|']
    L += [f"| `{o['template'] or '<пусто>'}` | {o['instances']} | {o['maps']} | {o['resource']} |" for o in s['objects_1v1'][:top]]
    L += ['', 'Ресурсы. Участки застройки: KindOf содержит ' + '/'.join(SITE_KINDS) +
          '. На них строятся фермы, лагеря и т.п. Логова: у итогового объекта есть модуль `SpawnBehavior`. '
          'Отдельно учтены текстуры рельефа (классы текстур BlendTileData).', '',
          '| ресурс | шаблон | экз. | карт |', '|---|---|---|---|']
    L += [f"| {o['resource']} | `{o['template']}` | {o['instances']} | {o['maps']} |" for o in s['objects_1v1']
          if o['resource'] and o['resource'] != 'стартовые/служебные']
    L += ['', f"Итого: " + ', '.join(f'{k} {v}' for k, v in res.items()) + '.', '',
          f"Текстуры рельефа на 1v1: {len(s['textures_1v1'])} классов. Чаще всего (класс: карт): " +
          ', '.join(f"`{t['texture']}` {t['maps']}" for t in s['textures_1v1'][:15]) + '.', '']
    c = s['script_compare']
    calls = {k: sum(r['count'] for r in v) for k, v in s['scripts_1v1'].items()}
    with_scripts = sum(1 for x in s['maps_1v1'] if x['scripts'])
    L += [f"Скрипты на 1v1: карт со своими скриптами {with_scripts} из {n1}; действий {len(s['scripts_1v1']['ACT'])} имён / "
          f"{calls['ACT']} вызовов, условий {len(s['scripts_1v1']['COND'])} / {calls['COND']}. "
          f"Полная таблица в `script_names.tsv`.", '',
          '| тип | имя | вызовов | карт | есть в ZH |', '|---|---|---|---|---|']
    for k in ('ACT', 'COND'):
        L += [f"| {k} | `{r['name']}` | {r['count']} | {r['maps']} | {'да' if r['in_zh'] else 'нет'} |" for r in s['scripts_1v1'][k][:top]]
    L += ['', '### Пересчёт имён скриптов (набор Геймплея: «нет в ZH»)', '',
          '| | действия | условия |', '|---|---|---|']
    rows = [('имён в ZH `ScriptEngine.cpp`', 'zh_total'), ('имён на картах базы (maps.big)', 'base_names'),
            ('из них нет в ZH', 'base_not_in_zh'), ('набор Геймплея (нет в ZH)', 'gameplay_set'),
            ('…используется на картах 2.22 (все итоговые .map)', 'gameplay_used_on_222_maps'),
            ('…используется на картах 1v1 2.22', 'gameplay_used_on_222_1v1'),
            ('имён на итоговых картах', 'final_names'), ('имён на картах 1v1', 'final_1v1_names')]
    L += [f'| {t} | {c["ACT"][k]} | {c["COND"][k]} |' for t, k in rows]
    L += [f'| новых в 2.22 (нет на картах базы) | {len(c["ACT"]["new_in_222"])} | {len(c["COND"]["new_in_222"])} |',
          f'| …из них нет в ZH | {len(c["ACT"]["new_in_222_not_in_zh"])} | {len(c["COND"]["new_in_222_not_in_zh"])} |',
          f'| …из них на 1v1 | {len(c["ACT"]["new_in_222_used_on_1v1"])} | {len(c["COND"]["new_in_222_used_on_1v1"])} |',
          f'| на 1v1 и нет в ZH | {len(c["ACT"]["1v1_not_in_zh"])} | {len(c["COND"]["1v1_not_in_zh"])} |', '',
          f'Набор Геймплея взят из: {c["ACT"]["gameplay_set_source"]}. TSV, заново собранный по картам базы '
          f'(`bfme1_script_names_maps_big.tsv` в этой папке), совпадает с `docs/formats/data/…tsv`: '
          f'ACT {c["ACT"]["docs_tsv_equal_to_regenerated"]}, COND {c["COND"]["docs_tsv_equal_to_regenerated"]}.', '']
    for k in ('ACT', 'COND'):
        if c[k]['new_in_222_not_in_zh']:
            L.append(f'Новые в 2.22 и нет в ZH ({k}): ' + ', '.join(f'`{n or "<пустое имя>"}`' for n in c[k]['new_in_222_not_in_zh']) + '.')
    L.append('')
    b = s['blend_1v1']
    L += ['Счётчики BlendTileData по картам 1v1: ' + ', '.join(f'{k} {v}' for k, v in b.items()) + '.', '']
    return L
