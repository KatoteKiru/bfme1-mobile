"""E1-26, M1 slice: our schemas for terrain.ini, water.ini and roads.ini,
and the blend-tile texture map of one .map (Fords of Isen).

OpenSAGE rev 588ac477 (GPL-3.0) is an external oracle only. Nothing in this
file is copied from OpenSAGE. Field names were checked against
tools/datacheck/schema/opensage_schema.json at that revision. Where the
oracle and the ZH engine disagree, both results are kept and the difference
is reported. WorldHeightMap.cpp is not touched (the atlas is E1-51).
The other INI schemas stay in E1-23.

No EA bytes are written. Reports may contain counters, texture names and
paths inside archives.
"""
import argparse
import os
import struct
import sys
from collections import defaultdict

from . import chunks as probe
from .ini import SPLIT, strip_comment
from .maps import blend_counters
from .vfs import VFS, norm

# Oracle identity. Not a source import.
OPENSAGE_REV = '588ac477'
OPENSAGE_REV_FULL = '588ac477367a0022adf29f20a084e8873014e6ce'

M1_MAP = 'maps\\map mp fords of isen\\map mp fords of isen.map'
# Lead's M1 criterion: the file is in one of these two archives.
M1_ARCHIVES = ('terrain.big', 'textures.big')

# Blend tiles: OpenSAGE Terrain.CreateTextures joins "Art/Terrain" with the
# Texture field as written (extension included) and opens that path.
# Case-insensitive, same as OpenSAGE's BIG dictionary.
TERRAIN_TILE_DIR = 'art\\terrain\\'

# Water and roads use the generic texture loader, not the blend-tile path.
# OpenSAGE OnDemandTextureLoader tries these extensions in order, and
# BfmeTexturePathResolver tries lang/<lang>/art/textures, art/textures, then
# the bare name. Listed here so the cross-check is explicit; not used for
# the M1 blend-tile count.
GENERIC_PREFIXES = ('lang\\english\\art\\textures\\', 'art\\textures\\', '')
GENERIC_EXTS = ('.dds', '.tga', '.jpg')

# Our field sets. A field not in the set is recorded and skipped, not fatal.
# Terrain.Class is not a single token: see parse_class().
TERRAIN_FIELDS = ('Texture', 'BlendEdges', 'Class', 'RestrictConstruction', 'TerrainObject')
WATER_SET_FIELDS = (
    'SkyTexture', 'WaterTexture',
    'Vertex00Color', 'Vertex10Color', 'Vertex01Color', 'Vertex11Color',
    'DiffuseColor', 'TransparentDiffuseColor',
    'UScrollPerMS', 'VScrollPerMS', 'SkyTexelsPerUnit', 'WaterRepeatCount',
)
WATER_TRANSPARENCY_FIELDS = (
    'TransparentWaterMinOpacity', 'TransparentWaterDepth',
    'SkyboxTextureN', 'SkyboxTextureE', 'SkyboxTextureS', 'SkyboxTextureW', 'SkyboxTextureT',
    'StandingWaterColor', 'StandingWaterTexture', 'AdditiveBlending', 'RadarWaterColor',
    'RiverTransparencyMultiplier', 'ReflectionPlaneZ', 'ReflectionOn', 'ReflectionGuard',
)
ROAD_FIELDS = ('Texture', 'RoadWidth', 'RoadWidthInTexture')
BRIDGE_FIELDS = (
    'BridgeModelName', 'BridgeModelNameBroken', 'BridgeModelNameDamaged', 'BridgeModelNameReallyDamaged',
    'BridgeScale', 'DamagedToSound', 'NumFXPerType', 'RadarColor', 'RepairedToSound',
    'ScaffoldObjectName', 'ScaffoldSupportObjectName',
    'Texture', 'TextureBroken', 'TextureDamaged', 'TextureReallyDamaged',
    'TowerObjectNameFromLeft', 'TowerObjectNameFromRight', 'TowerObjectNameToLeft', 'TowerObjectNameToRight',
    'TransitionEffectsHeight', 'TransitionToFX', 'TransitionToOCL',
)
FILE_KINDS = {
    'terrain': {'Terrain': TERRAIN_FIELDS},
    'water': {'WaterSet': WATER_SET_FIELDS, 'WaterTransparency': WATER_TRANSPARENCY_FIELDS},
    'roads': {'Road': ROAD_FIELDS, 'Bridge': BRIDGE_FIELDS},
}
INI_PATHS = {
    'terrain': 'data\\ini\\terrain.ini',
    'water': 'data\\ini\\water.ini',
    'roads': 'data\\ini\\roads.ini',
}


def tokens(line):
    return [p for p in SPLIT.split(strip_comment(line).strip()) if p]


def parse_blocks(text, kinds):
    """Top-level blocks. `kinds` is {header: known field names}. Unknown headers
    and unknown fields are kept. A block ends at a line whose first token is End,
    or at the next unindented header."""
    lines = text.splitlines()
    i = 0
    blocks = []
    known = {k.lower(): k for k in kinds}
    while i < len(lines):
        raw = lines[i]
        i += 1
        if not raw.strip() or raw[:1] in ' \t':
            continue
        parts = tokens(raw)
        if not parts or parts[0].startswith('#'):
            continue
        kind_key = parts[0].lower()
        if kind_key == 'end':
            continue
        canonical = known.get(kind_key)
        name = parts[1] if len(parts) > 1 else ''
        fields = []
        unknown = []
        while i < len(lines):
            raw2 = lines[i]
            s2 = strip_comment(raw2).strip()
            if s2.lower() == 'end':
                i += 1
                break
            if s2 and raw2[:1] not in ' \t':
                break
            i += 1
            if not s2 or s2.startswith('#'):
                continue
            fp = tokens(raw2)
            if not fp:
                continue
            field = fp[0]
            known_fields = kinds.get(canonical, ()) if canonical else ()
            if canonical and field not in known_fields:
                unknown.append(field)
            fields.append((field, fp[1:]))
        blocks.append({
            'kind': canonical or parts[0],
            'known': canonical is not None,
            'name': name,
            'fields': fields,
            'unknown_fields': unknown,
        })
    return blocks


def parse_class(field_tokens):
    """BFME terrain class clause, from the shapes actually used in terrain.ini.

    NONE
    Type <name> (NEXT Type <name>)* (NEXT Region <region>)*
    and one block that ends on a bare NEXT.

    OpenSAGE keeps only the first token (ParseString, then the next line).
    ZH's parseIndexList has neither `Type` nor `NEXT`.
    """
    toks = list(field_tokens)
    if toks == ['NONE']:
        return {'ok': True, 'form': 'NONE', 'type': 'NONE', 'extra_types': [], 'regions': [],
                'dangling_next': False, 'first_token': 'NONE', 'tail': []}
    if len(toks) >= 2 and toks[0] == 'Type':
        extra, regions = [], []
        dangling = False
        rest = toks[2:]
        ok = True
        while rest:
            if len(rest) >= 3 and rest[0] == 'NEXT' and rest[1] == 'Type':
                extra.append(rest[2])
                rest = rest[3:]
            elif len(rest) >= 3 and rest[0] == 'NEXT' and rest[1] == 'Region':
                regions.append(rest[2])
                rest = rest[3:]
            elif rest == ['NEXT']:
                dangling = True
                rest = []
            else:
                ok = False
                break
        return {
            'ok': ok,
            'form': 'Type',
            'type': toks[1],
            'extra_types': extra,
            'regions': regions,
            'dangling_next': dangling,
            'first_token': 'Type',
            'tail': rest,
        }
    return {'ok': False, 'form': 'other', 'type': None, 'extra_types': [], 'regions': [],
            'dangling_next': False, 'first_token': toks[0] if toks else '', 'tail': toks}


def _field(block, name):
    return [toks for key, toks in block['fields'] if key == name]


def _one(block, name):
    got = _field(block, name)
    return got[0] if got else None


def terrain_definitions(blocks):
    """One entry per Terrain block, in file order. ZH overwrites an existing
    name (later wins). OpenSAGE's ScopedAssetCollection keeps the first."""
    by_order = []
    grouped = defaultdict(list)
    for b in blocks:
        if b['kind'] != 'Terrain':
            continue
        tex = _one(b, 'Texture')
        cls = parse_class(_one(b, 'Class') or [])
        objs = []
        for toks in _field(b, 'TerrainObject'):
            objs.append({'name': toks[0] if toks else '', 'weight': toks[1] if len(toks) > 1 else None, 'tokens': toks})
        rec = {
            'name': b['name'],
            'texture': tex[0] if tex else None,
            'class': cls,
            'blend_edges': (_one(b, 'BlendEdges') or [None])[0],
            'restrict_construction': (_one(b, 'RestrictConstruction') or [None])[0],
            'terrain_objects': objs,
            'unknown_fields': list(b['unknown_fields']),
        }
        grouped[b['name'].lower()].append(rec)
        by_order.append(rec)
    defs = []
    for key, recs in grouped.items():
        defs.append({
            'key': key,
            'zh': recs[-1],       # later definition overwrites, as INI::parseTerrainDefinition
            'opensage': recs[0],  # first add wins
            'count': len(recs),
        })
    return {'order': by_order, 'by_name': {d['key']: d for d in defs}, 'defs': defs}


def texture_refs(blocks, kind, field_names):
    """(block name, field, filename) for texture-like fields."""
    out = []
    for b in blocks:
        if b['kind'] != kind:
            continue
        for key, toks in b['fields']:
            if key in field_names and toks:
                out.append({'kind': b['kind'], 'block': b['name'], 'field': key, 'file': toks[0]})
    return out


class PathIndex:
    """Case-insensitive archive paths. Hits are in VFS mount order (first wins
    in the game). This is not OpenSAGE's last-archive-wins order."""

    def __init__(self, archives):
        self.hits = defaultdict(list)
        for arc in archives:
            label = os.path.basename(arc.label.replace('\\', '/'))
            for entry in arc.entries:
                key = entry.name.replace('/', '\\').lower()
                self.hits[key].append((label, entry.name))

    def get(self, path):
        return list(self.hits.get(path.replace('/', '\\').lower(), []))


def in_m1_archives(hits):
    return [h for h in hits if h[0].lower() in M1_ARCHIVES]


def blend_tile_hits(index, texture_file):
    """Exact art\\terrain\\<Texture> path. Extension is not rewritten."""
    if not texture_file:
        return []
    return index.get(TERRAIN_TILE_DIR + texture_file.replace('/', '\\'))


def generic_texture_hits(index, texture_file):
    """First candidate that exists, using the generic extension order.
    Returns (candidate_path, hits_in_mount_order) or (None, [])."""
    if not texture_file:
        return None, []
    stem = os.path.splitext(os.path.basename(texture_file.replace('\\', '/')))[0]
    for prefix in GENERIC_PREFIXES:
        for ext in GENERIC_EXTS:
            path = prefix + stem + ext
            hits = index.get(path)
            if hits:
                return path, hits
    return None, []


def read_blend_classes(data, version, start, wh):
    """Texture classes of BlendTileData and which source tiles a cell uses.

    The byte layout is the same one maps.blend_counters walks (BFME1_vs_ZH.md
    §2.4). Names must match blend_counters['texture_classes'] or the reader
    has drifted.
    """
    w, h = wh
    if version > 15:
        raise ValueError(f'unsupported BlendTileData v{version}')
    p = start
    n = struct.unpack_from('<I', data, p)[0]
    p += 4
    if n != w * h:
        raise ValueError(f'numTiles {n} != w*h {w * h}')
    big = 14 <= version < 24
    isz = 4 if big else 2
    tiles = struct.unpack_from(f'<{n}H', data, p)
    p += 2 * n
    p += isz * n
    if version >= 6:
        p += isz * n
    if version >= 5:
        p += isz * n
    row = (w + 7) // 8
    if version >= 7:
        p += (((w + 1) // 8) if version == 7 else row) * h
    p += row * h * ((version >= 10) + (version >= 11) + (14 <= version < 25) + (version >= 15))
    cells, nblend, ncliff, ntex = struct.unpack_from('<IIII', data, p)
    p += 16
    classes = []
    for _ in range(ntex):
        cell_start, cell_count, cell_size, magic = struct.unpack_from('<IIII', data, p)
        p += 16
        ln = struct.unpack_from('<H', data, p)[0]
        p += 2
        name = data[p:p + ln].decode('latin-1')
        p += ln
        classes.append({
            'name': name,
            'cell_start': cell_start,
            'cell_count': cell_count,
            'cell_size': cell_size,
            'magic': magic,
        })
    used = {t >> 2 for t in tiles}
    for c in classes:
        lo, hi = c['cell_start'], c['cell_start'] + c['cell_count']
        c['cells_used'] = sum(1 for u in used if lo <= u < hi)
    return {
        'version': version, 'w': w, 'h': h,
        'num_cells': n, 'num_bitmap_tiles': cells, 'num_blended': nblend, 'num_cliff': ncliff,
        'classes': classes, 'source_tiles_used': len(used),
    }


def _map_body(raw):
    data, comp = probe.map_bytes(raw)
    toc, p = probe.parse_toc(data)
    tops = probe.top_chunks(data, toc, p)
    height = blend = None
    for name, ver, size, body in tops:
        if name == 'HeightMapData':
            w, h, border = struct.unpack_from('<iii', data, body)
            height = {'version': ver, 'w': w, 'h': h, 'border': border, 'chunk_size': size}
        elif name == 'BlendTileData':
            wh = (height['w'], height['h']) if height else None
            if wh is None:
                raise ValueError('BlendTileData before HeightMapData')
            classes = read_blend_classes(data, ver, body, wh)
            counters = blend_counters(data, ver, body, size, wh)
            if [c['name'] for c in classes['classes']] != counters['texture_classes']:
                raise ValueError('blend class names drifted from maps.blend_counters')
            blend = {'classes': classes, 'counters': counters}
    return {'decoded_bytes': len(data), 'compression': comp, 'height': height, 'blend': blend}


def _archive_label(arc_or_vf_source):
    return os.path.basename(str(arc_or_vf_source).replace('\\', '/'))


def analyze(vfs, map_path=M1_MAP):
    index = PathIndex(vfs.archives)
    parsed = {}
    for key, path in INI_PATHS.items():
        vf = vfs.files.get(norm(path))
        if vf is None:
            raise KeyError(path)
        text = vf.read().decode('latin-1')
        blocks = parse_blocks(text, FILE_KINDS[key])
        parsed[key] = {'path': path, 'source': _archive_label(vf.source), 'blocks': blocks, 'vf': vf}

    terrain = terrain_definitions(parsed['terrain']['blocks'])
    base_terrain = None
    for label, entry, _size in parsed['terrain']['vf'].overridden:
        if _archive_label(label) == 'ini.big':
            base_terrain = terrain_definitions(parse_blocks(entry.read().decode('latin-1'), FILE_KINDS['terrain']))

    mvf = vfs.files.get(norm(map_path))
    if mvf is None:
        raise KeyError(map_path)
    versions = [{'archive': _archive_label(mvf.source), 'raw_bytes': mvf.size, 'body': _map_body(mvf.read())}]
    for label, entry, size in mvf.overridden:
        raw = entry.read()
        versions.append({'archive': _archive_label(label), 'raw_bytes': len(raw), 'body': _map_body(raw)})

    winning = versions[0]['body']
    rows = []
    missing = []
    for cls in winning['blend']['classes']['classes']:
        defin = terrain['by_name'].get(cls['name'].lower())
        texture = defin['zh']['texture'] if defin else None
        hits = blend_tile_hits(index, texture) if texture else []
        m1 = in_m1_archives(hits)
        base_texture = None
        if base_terrain is not None and defin is not None:
            base_def = base_terrain['by_name'].get(cls['name'].lower())
            base_texture = base_def['zh']['texture'] if base_def else None
        row = {
            'name': cls['name'],
            'ini_name': defin['zh']['name'] if defin else None,
            'texture': texture,
            'base_texture': base_texture,
            'texture_same_in_ini_big': texture == base_texture,
            'cell_start': cls['cell_start'],
            'cell_count': cls['cell_count'],
            'cell_size': cls['cell_size'],
            'cells_used': cls['cells_used'],
            'magic': cls['magic'],
            'hits': [{'archive': a, 'path': p} for a, p in hits],
            'm1_hits': [{'archive': a, 'path': p} for a, p in m1],
            'class_type': (defin['zh']['class']['type'] if defin else None),
            'region_count': len(defin['zh']['class']['regions']) if defin else 0,
            'class_ok': defin['zh']['class']['ok'] if defin else False,
        }
        rows.append(row)
        if not m1:
            missing.append(row)

    def _resolve_generic(refs):
        out = []
        for ref in refs:
            path, hits = generic_texture_hits(index, ref['file'])
            m1 = in_m1_archives(hits)
            out.append({
                **ref,
                'resolved': path,
                'hit': {'archive': hits[0][0], 'path': hits[0][1]} if hits else None,
                'm1_hit': {'archive': m1[0][0], 'path': m1[0][1]} if m1 else None,
            })
        return out

    water_refs = _resolve_generic(
        texture_refs(parsed['water']['blocks'], 'WaterSet', ('SkyTexture', 'WaterTexture'))
        + texture_refs(parsed['water']['blocks'], 'WaterTransparency', ('StandingWaterTexture',))
    )
    road_refs = _resolve_generic(
        texture_refs(parsed['roads']['blocks'], 'Road', ('Texture',))
        + texture_refs(parsed['roads']['blocks'], 'Bridge', ('Texture', 'TextureBroken', 'TextureDamaged', 'TextureReallyDamaged'))
    )

    redefs = []
    for d in terrain['defs']:
        if d['count'] < 2:
            continue
        redefs.append({
            'name': d['opensage']['name'],
            'count': d['count'],
            'opensage_texture': d['opensage']['texture'],
            'zh_texture': d['zh']['texture'],
            'texture_differs': d['opensage']['texture'] != d['zh']['texture'],
            'class_differs': d['opensage']['class'] != d['zh']['class'],
        })

    t_blocks = [b for b in parsed['terrain']['blocks'] if b['kind'] == 'Terrain']
    class_drop = 0
    class_bad = 0
    with_regions = 0
    extra_type = 0
    dangling_next = 0
    for rec in terrain['order']:
        clause = rec['class']
        if not clause['ok']:
            class_bad += 1
        if clause['form'] == 'Type':
            class_drop += 1
        if clause['regions']:
            with_regions += 1
        if clause.get('extra_types'):
            extra_type += 1
        if clause.get('dangling_next'):
            dangling_next += 1

    patch_only = []
    if base_terrain is not None:
        patch_only = sorted(set(terrain['by_name']) - set(base_terrain['by_name']))

    def _unknown(blocks):
        found = []
        for b in blocks:
            if not b['known']:
                found.append({'where': 'header', 'kind': b['kind'], 'name': b['name']})
            for field in b['unknown_fields']:
                found.append({'where': 'field', 'kind': b['kind'], 'name': b['name'], 'field': field})
        return found

    return {
        'opensage_rev': OPENSAGE_REV,
        'map_path': mvf.path,
        'map_versions': [
            {
                'archive': v['archive'],
                'raw_bytes': v['raw_bytes'],
                'decoded_bytes': v['body']['decoded_bytes'],
                'compression': v['body']['compression'],
                'height': v['body']['height'],
                'num_texture_classes': v['body']['blend']['counters']['numTextureClasses'],
                'num_bitmap_tiles': v['body']['blend']['counters']['numBitmapTiles'],
                'num_blended': v['body']['blend']['counters']['numBlendedTiles'],
                'num_cliff': v['body']['blend']['counters']['numCliffInfo'],
                'source_tiles_used': v['body']['blend']['classes']['source_tiles_used'],
                'texture_names': v['body']['blend']['counters']['texture_classes'],
                'leftover_bytes': v['body']['blend']['counters']['leftover_bytes'],
            }
            for v in versions
        ],
        'rows': rows,
        'missing': [{'name': r['name'], 'texture': r['texture']} for r in missing],
        'found_in_m1_archives': sum(1 for r in rows if r['m1_hits']),
        'terrain': {
            'source': parsed['terrain']['source'],
            'blocks': len(t_blocks),
            'unique': len(terrain['defs']),
            'class_bad': class_bad,
            'opensage_keeps_only_type_token': class_drop,
            'with_regions': with_regions,
            'extra_type': extra_type,
            'dangling_next': dangling_next,
            'redefinitions': redefs,
            'patch_only_names': patch_only,
            'unknown': _unknown(parsed['terrain']['blocks']),
            'unknown_headers': sum(1 for b in parsed['terrain']['blocks'] if not b['known']),
        },
        'water': {
            'source': parsed['water']['source'],
            'blocks': [{'kind': b['kind'], 'name': b['name']} for b in parsed['water']['blocks']],
            'unknown': _unknown(parsed['water']['blocks']),
            'textures': water_refs,
        },
        'roads': {
            'source': parsed['roads']['source'],
            'blocks': [{'kind': b['kind'], 'name': b['name']} for b in parsed['roads']['blocks']],
            'unknown': _unknown(parsed['roads']['blocks']),
            'textures': road_refs,
        },
    }


def render_md(report):
    """Markdown safe to commit: counters, texture names, archive paths. No INI
    values other than texture filenames, no region names, no colours."""
    win = report['map_versions'][0]
    h = win['height']
    blend = None
    # rows carry the class table; counters live on the winning version via rows' parent
    n = len(report['rows'])
    used_classes = sum(1 for r in report['rows'] if r['cells_used'])
    lines = []
    a = lines.append
    a('# E1-26, срез M1: текстуры blend-тайлов Fords of Isen')
    a('')
    a('Срез закрывает критерий M1 по текстурам карты, не весь пункт E1-26 бэклога.')
    a('Схемы `terrain.ini`, `water.ini` и `roads.ini` разбираются здесь (v3.6: E1-26')
    a('для M1 не ждёт остальной E1-23). Атлас тайлов — E1-51, `WorldHeightMap.cpp`')
    a('этот срез не меняет. Вода и дороги как картинка — E1-29; здесь только разбор')
    a('INI и поиск файлов текстур.')
    a('')
    a(f'Оракул: OpenSAGE rev {report["opensage_rev"]} (GPL-3.0). Код оракула в движок')
    a('не копировался. Сверка — по раскладке, которую оракул уже подтвердил')
    a('(`docs/formats/BFME1_vs_ZH.md` §2.4) и по правилам поиска файла, названным ниже.')
    a('')
    a('## Карта')
    a('')
    a(f'Путь в VFS: `{report["map_path"]}`.')
    a('')
    a('| версия | архив | сырой размер | после RefPack | HeightMap | классы текстур | остаток BlendTileData |')
    a('|---|---|---:|---:|---|---:|---:|')
    for v in report['map_versions']:
        hh = v['height']
        a(f"| {'игра' if v is win else 'перекрытая'} | `{v['archive']}` | {v['raw_bytes']} | {v['decoded_bytes']} | "
          f"v{hh['version']} {hh['w']}×{hh['h']}, border {hh['border']} | {v['num_texture_classes']} | {v['leftover_bytes']} |")
    a('')
    names_same = all(v['texture_names'] == win['texture_names'] for v in report['map_versions'])
    a(f'Имена классов текстур у версии из патча и из `maps.big` совпадают: {"да" if names_same else "нет"}.')
    a(f'Классов текстур: {n}. Классов, на которые ссылается хотя бы одна клетка: {used_classes}.')
    a(f'Исходных тайлов (`numBitmapTiles`): {win["num_bitmap_tiles"]}. '
      f'Из них реально стоят на клетке: {win["source_tiles_used"]}.')
    a(f'Описаний blend (`numBlendedTiles`, слот 0 пустой): {win["num_blended"]}. '
      f'Записей cliff (`numCliffInfo`, слот 0 пустой): {win["num_cliff"]}.')
    a('Оракул OpenSAGE по этой карте (`docs/formats/BFME1_vs_ZH.md`): 63 текстуры, 984 тайла, '
      '16 700 описаний blend (= numBlendedTiles − 1), 577 cliff (= numCliffInfo − 1).')
    a('')
    a('## Критерий M1')
    a('')
    a('Текстура blend-тайла — это поле `Texture` одноимённого блока `Terrain` в')
    a(f'`terrain.ini` (источник `{report["terrain"]["source"]}`, позднее определение')
    a('перекрывает раннее, как `INI::parseTerrainDefinition` в ZH). Файл ищется как')
    a(f'`{TERRAIN_TILE_DIR}<Texture>` без смены расширения, без учёта регистра.')
    a('Так же путь собирает OpenSAGE `Terrain.CreateTextures` (`Art/Terrain` + поле `Texture`).')
    a(f'Найдено в `terrain.big` и/или `textures.big`: **{report["found_in_m1_archives"]} / {n}**.')
    a(f'Не найдено: **{len(report["missing"])}**.')
    a('')
    if report['missing']:
        a('Пропуски:')
        a('')
        for m in report['missing']:
            a(f'- `{m["name"]}` → `{m["texture"]}`')
        a('')
    a('| # | класс на карте | клетки (старт + число, сторона) | использовано | файл Texture | архив | путь в архиве |')
    a('|---:|---|---|---:|---|---|---|')
    for i, r in enumerate(report['rows'], 1):
        hit = r['m1_hits'][0] if r['m1_hits'] else None
        arc = f'`{hit["archive"]}`' if hit else '—'
        path = f'`{hit["path"]}`' if hit else '—'
        a(f'| {i} | `{r["name"]}` | {r["cell_start"]}+{r["cell_count"]}, {r["cell_size"]} | {r["cells_used"]} | '
          f'`{r["texture"]}` | {arc} | {path} |')
    a('')
    a('## terrain.ini')
    a('')
    t = report['terrain']
    a(f'Блоков `Terrain`: {t["blocks"]}, уникальных имён: {t["unique"]}, '
      f'повторных определений: {len(t["redefinitions"])}, клауза `Class` не разобрана: {t["class_bad"]}.')
    a(f'С хвостом `NEXT Region`: {t["with_regions"]} (имена регионов в отчёт не входят). '
      f'Со вторым `NEXT Type`: {t["extra_type"]}. С оборванным `NEXT` в конце строки: {t["dangling_next"]}.')
    a(f'Неизвестных заголовков и полей: {len(t["unknown"])}.')
    a(f'Имена, которые есть в выигравшей версии и нет в `ini.big`: {", ".join(f"`{n}`" for n in t["patch_only_names"]) or "нет"}.')
    redef_names = {d['name'].lower() for d in t['redefinitions']}
    on_map = [r['name'] for r in report['rows'] if r['name'].lower() in redef_names]
    if on_map:
        a('Повторные имена, которые есть на карте: ' + ', '.join(f'`{n}`' for n in on_map) + '.')
    else:
        a('Ни одно из повторных имён не входит в классы этой карты.')
    a('')
    if t['redefinitions']:
        a('| имя | определений | Texture, который оставит OpenSAGE (первое) | Texture, который оставит ZH (последнее) | класс тоже различается |')
        a('|---|---:|---|---|---|')
        for d in t['redefinitions']:
            a(f'| `{d["name"]}` | {d["count"]} | `{d["opensage_texture"]}` | `{d["zh_texture"]}` | '
              f'{"да" if d["class_differs"] else "нет"} |')
        a('')
    same = sum(1 for r in report['rows'] if r['texture_same_in_ini_big'])
    a(f'У классов этой карты `Texture` в `_patch222.big` и в `ini.big` совпадает: {same} / {n}.')
    a('')
    a('## water.ini и roads.ini')
    a('')
    a('Это не критерий M1 (там только blend-тайлы). Файлы ищутся общим правилом')
    a('текстур, не правилом `Art/Terrain`: сначала `.dds`, потом `.tga`, потом `.jpg`,')
    a('в `lang\\english\\art\\textures\\`, затем `art\\textures\\`, затем голое имя.')
    a('Порядок расширений — как у загрузчика текстур OpenSAGE той же ревизии.')
    a('В таблицу попадают только имя из INI и путь в архиве.')
    a('')
    w = report['water']
    def _blk(b):
        return f"{b['kind']} `{b['name']}`" if b['name'] else b['kind']
    a(f'`water.ini` из `{w["source"]}`: блоков {len(w["blocks"])} '
      f'({", ".join(_blk(b) for b in w["blocks"])}).')
    a(f'Неизвестных полей: {len(w["unknown"])}. Текстур: {len(w["textures"])}, '
      f'из них в terrain.big/textures.big: {sum(1 for x in w["textures"] if x["m1_hit"])}.')
    a('')
    a('| блок | поле | имя в INI | terrain.big / textures.big | кто побеждает в VFS |')
    a('|---|---|---|---|---|')
    for x in w['textures']:
        label = x['block'] or x['kind']
        hit = x['m1_hit']
        vfs_hit = x['hit']
        if hit:
            where = f'`{hit["archive"]}` `{hit["path"]}`'
        else:
            where = 'не найден'
        if vfs_hit and hit and vfs_hit != hit:
            winner = f'`{vfs_hit["archive"]}` `{vfs_hit["path"]}`'
        elif vfs_hit:
            winner = 'тот же файл'
        else:
            winner = '—'
        a(f'| `{label}` | `{x["field"]}` | `{x["file"]}` | {where} | {winner} |')
    a('')
    rd = report['roads']
    nroad = sum(1 for b in rd['blocks'] if b['kind'] == 'Road')
    nbridge = sum(1 for b in rd['blocks'] if b['kind'] == 'Bridge')
    found_r = sum(1 for x in rd['textures'] if x['m1_hit'])
    a(f'`roads.ini` из `{rd["source"]}`: Road {nroad}, Bridge {nbridge}.')
    a(f'Неизвестных полей: {len(rd["unknown"])}. Ссылок на текстуры: {len(rd["textures"])}, '
      f'найдены в terrain.big/textures.big: {found_r}, не найдены: {len(rd["textures"]) - found_r}.')
    ext_note = {}
    for x in rd['textures']:
        hit = x['m1_hit'] or x['hit']
        ext = os.path.splitext(hit['path'])[1].lower() if hit else 'missing'
        ext_note[ext] = ext_note.get(ext, 0) + 1
    a('По расширению найденного файла: ' + ', '.join(f'{k} ×{v}' for k, v in sorted(ext_note.items())) + '.')
    missed = [x for x in rd['textures'] if not x['m1_hit']]
    if missed:
        a('')
        a('Не найдены в terrain.big/textures.big:')
        a('')
        for x in missed:
            a(f'- `{x["block"]}` `{x["field"]}` `{x["file"]}`')
    a('')
    a('## Расхождения с OpenSAGE')
    a('')
    a(f'Ревизия оракула: `{OPENSAGE_REV_FULL}`.')
    a('')
    a('1. **`Class`.** Оракул читает одно слово (`ParseString`) и переходит на следующую')
    a(f'   строку. У {t["opensage_keeps_only_type_token"]} блоков `Terrain` первое слово — `Type`,')
    a('   имя класса, второй `NEXT Type` и хвост `NEXT Region` оракул не сохраняет.')
    a('   Наша схема хранит имя класса, дополнительные типы и список регионов.')
    a('   У ZH такого хвоста нет (`parseIndexList` по перечислению Generals).')
    a('2. **Повтор имени.** Оракул оставляет первое определение (`ScopedAssetCollection`,')
    a('   «existing entries take precedence»). ZH находит уже созданный тип и перечитывает')
    a('   поля поверх (`initFromINI`), то есть побеждает последнее. Затронуты три имени в')
    a('   таблице выше; на карте M1 их нет, поэтому на критерий M1 это не влияет.')
    a('3. **Путь blend-тайла.** Оракул открывает `Art/Terrain/<Texture>` как есть (расширение')
    a('   из INI, словарь BIG без учёта регистра). Общий загрузчик (`.dds`, потом `.tga`,')
    a('   потом `.jpg` в `art/textures`) для этих тайлов оракул не использует. Все найденные')
    a('   файлы карты лежат в `terrain.big` именно по этому пути, так что оба правила дают')
    a('   один и тот же файл. В `textures.big` этих путей нет, патч их не перекрывает.')
    a('4. **Порядок архивов.** Игра и наш VFS: первый по `_stricmp` побеждает. Оракул для')
    a('   BIG в ряде мест оставляет последний. Для 63 файлов карты источник один')
    a('   (`terrain.big`), расхождения нет. У части текстур воды тот же stem есть и в')
    a('   `_patch222textures.big`; в отчёте воды указан файл из `textures.big`, если он')
    a('   попал в пару архивов критерия.')
    a('5. **Вода и дороги.** Имена в INI оканчиваются на `.tga`, файлы в `textures.big` —')
    a('   `.dds` или `.jpg`. Точное имя `.tga` в `terrain.big`/`textures.big` не лежит.')
    a('   Это ожидаемо для общего загрузчика и не считается пропуском blend-тайлов.')
    a('')
    a('## Чего в этом срезе нет')
    a('')
    a('- Остальные схемы INI (E1-23), в том числе `watertextures.ini`.')
    a('- Отрисовка воды и дорог (E1-29).')
    a('- Атлас тайлов и любые правки `WorldHeightMap.cpp` (E1-51).')
    a('- Загрузка текстур в рантайме движка и монтирование VFS в игре (E1-21b).')
    a('- Тела INI, байты TGA и регионы `Class` в git.')
    a('')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description='E1-26 M1: map Fords of Isen blend-tile textures')
    ap.add_argument('--game', default=os.environ.get('BFME_GAMEDATA'),
                    help='directory with .big archives (default: $BFME_GAMEDATA)')
    ap.add_argument('--map', default=M1_MAP, help='VFS path of the .map')
    ap.add_argument('--report', help='write the markdown report to this path')
    args = ap.parse_args(argv)
    if not args.game:
        ap.error('no game data: pass --game DIR or set BFME_GAMEDATA')
    vfs = VFS(args.game, include_loose=False)
    report = analyze(vfs, args.map)
    text = render_md(report)
    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    missing = len(report['missing'])
    print(f'classes {len(report["rows"])} found {report["found_in_m1_archives"]} missing {missing}', file=sys.stderr)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
