"""Per-map BlendTileData counters for the E1-25 decisions (docs/formats/BFME1_vs_ZH.md §2.4.4, §6.1 item 7).

For every winning .map in the VFS (so patch 2.22 maps are picked up on re-run):
  dangling_tiles      cells whose tile index >> 2 >= numBitmapTiles (ZH m_sourceTiles[tile>>2], WorldHeightMap.h:206)
  dangling_blend/3way/cliff  cells whose blend / threeWay / cliff index >= numBlendedTiles / numBlendedTiles / numCliffInfo
  flags_outside_0x3   blend descriptions whose `inverted` byte has bits outside INVERTED|FLIPPED (0x3, TileData.h:46-47),
                      plus how many cells (blend or 3-way) reference such descriptions
  dir_bytes_not_01    blend descriptions with a direction byte (horiz/vert/rDiag/lDiag) other than 0/1
  edge_class_without_classes  blend descriptions with customBlendEdgeClass >= 0 while numEdgeTextureClasses == 0
  edge_class_out_of_range     customBlendEdgeClass >= numEdgeTextureClasses (superset of the above)
Map decoding (CkMp TOC, chunk headers, RefPack) is in datacheck/chunks.py (copied from tools/formatprobe/probe.py).
"""
import os
import struct
import sys
from collections import Counter

from . import chunks as probe  # noqa: E402  (copy of tools/formatprobe/probe.py)

FLAG_VAL = 0x7ADA0000


def blend_counters(d, v, b, s, wh):
    w, h = wh
    p = b
    n = struct.unpack_from('<I', d, p)[0]; p += 4
    if n != w * h:
        raise ValueError(f'numTiles {n} != w*h {w * h}')
    if v > 15:
        raise ValueError(f'unsupported BlendTileData v{v}')
    big = 14 <= v < 24
    ifmt = 'I' if big else 'H'
    isz = 4 if big else 2
    tiles = struct.unpack_from(f'<{n}H', d, p); p += 2 * n
    blends = struct.unpack_from(f'<{n}{ifmt}', d, p); p += isz * n
    three = struct.unpack_from(f'<{n}{ifmt}', d, p) if v >= 6 else (0,) * n
    p += isz * n if v >= 6 else 0
    cliff = struct.unpack_from(f'<{n}{ifmt}', d, p) if v >= 5 else (0,) * n
    p += isz * n if v >= 5 else 0
    row = (w + 7) // 8
    if v >= 7:
        p += (((w + 1) // 8) if v == 7 else row) * h
    p += row * h * ((v >= 10) + (v >= 11) + (14 <= v < 25) + (v >= 15))
    cells, nblend, ncliff, ntex = struct.unpack_from('<IIII', d, p); p += 16
    tex_names = []
    for _ in range(ntex):
        p += 16
        ln = struct.unpack_from('<H', d, p)[0]; p += 2
        tex_names.append(d[p:p + ln].decode('latin-1')); p += ln
    nedge_tiles = nedge_cls = 0
    if v >= 4:
        nedge_tiles = struct.unpack_from('<I', d, p)[0]; p += 4
        nedge_cls = struct.unpack_from('<I', d, p)[0]; p += 4
        for _ in range(nedge_cls):
            p += 12
            ln = struct.unpack_from('<H', d, p)[0]; p += 2 + ln
    flag_vals, bad_desc, dir_bad, edge0, edge_oor, bad_magic = Counter(), set(), 0, 0, 0, 0
    edge_vals = Counter()
    for i in range(1, nblend):
        _, hz, vt, rd, ld, inv, ldg, ce, fl = struct.unpack_from('<IBBBBBBiI', d, p); p += 18
        if fl != FLAG_VAL:
            bad_magic += 1
        if inv & ~0x3:
            flag_vals[inv] += 1
            bad_desc.add(i)
        if any(x not in (0, 1) for x in (hz, vt, rd, ld)):
            dir_bad += 1
        if ce >= 0:
            edge_vals[ce] += 1
            if nedge_cls == 0:
                edge0 += 1
            if ce >= nedge_cls:
                edge_oor += 1
    p += 38 * max(ncliff - 1, 0)
    max_tile = max(tiles) if tiles else 0
    return {
        'version': v, 'w': w, 'h': h, 'numBitmapTiles': cells, 'numBlendedTiles': nblend, 'numCliffInfo': ncliff,
        'numTextureClasses': ntex, 'numEdgeTiles': nedge_tiles, 'numEdgeTextureClasses': nedge_cls,
        'max_tile_div4': max_tile >> 2,
        'dangling_tiles': sum(1 for t in tiles if (t >> 2) >= cells),
        # index 0 = "none" (ZH slot 0 is unused; counts are records + 1, or 0 when there are none)
        'dangling_blend': sum(1 for x in blends if x and x >= nblend),
        'dangling_3way': sum(1 for x in three if x and x >= nblend),
        'dangling_cliff': sum(1 for x in cliff if x and x >= ncliff),
        'flags_outside_0x3': sum(flag_vals.values()),
        'flags_outside_0x3_values': {str(k): c for k, c in sorted(flag_vals.items())},
        'cells_using_flags_outside_0x3': sum(1 for x in blends if x in bad_desc) + sum(1 for x in three if x in bad_desc),
        'dir_bytes_not_01': dir_bad,
        'edge_class_without_classes': edge0,
        'edge_class_out_of_range': edge_oor,
        'edge_class_values': {str(k): c for k, c in sorted(edge_vals.items())},
        'flag_val_mismatch': bad_magic,
        'leftover_bytes': b + s - p,
        'texture_classes': tex_names,
    }


def _is_patch(label):
    return label.lower().startswith('_patch')


def scan(vfs, contents=True):
    """BlendTileData counters per winning .map; with contents=True also objects/scripts/start spots per map
    (datacheck/mapscan.py) for the winning version and for the base (non-patch) version of patched maps."""
    from . import mapscan
    res, errors, base = {}, {}, {}
    for k, vf in sorted(vfs.files.items()):
        if not k.endswith('.map'):
            continue
        try:
            d, comp = probe.map_bytes(vfs.read(k))
            toc, p = probe.parse_toc(d)
            tops = probe.top_chunks(d, toc, p)
            wh = None
            for name, v, s, b in tops:
                if name == 'HeightMapData':
                    wh = struct.unpack_from('<ii', d, b)
                elif name == 'BlendTileData':
                    r = blend_counters(d, v, b, s, wh)
                    r['source'] = vf.source
                    r['group'] = vf.path.split('\\')[0].lower()
                    r['compression'] = comp
                    r['size'] = vf.size
                    res[vf.path] = r
            if contents and vf.path in res:
                res[vf.path]['content'] = mapscan.content(d, toc, tops)
        except Exception as e:  # report, never abort the whole run
            errors[vf.path] = f'{type(e).__name__}: {e}'
        if not contents:
            continue
        # base version: the winning one if it is not from a patch archive, else the oldest non-patch overridden one
        olds = [o for o in vf.overridden if not _is_patch(o[0])]
        if not _is_patch(vf.source):
            if vf.path in res:
                base[vf.path] = dict(res[vf.path]['content'], source=vf.source)
        elif olds:
            label, entry, _ = olds[-1]
            try:
                d, _c = probe.map_bytes(entry.read() if entry is not None else vfs.read(k))
                toc, p = probe.parse_toc(d)
                base[vf.path] = dict(mapscan.content(d, toc, probe.top_chunks(d, toc, p)), source=label)
            except Exception as e:
                errors[vf.path + ' @' + label] = f'{type(e).__name__}: {e}'
    keys = ['dangling_tiles', 'dangling_blend', 'dangling_3way', 'dangling_cliff', 'flags_outside_0x3',
            'cells_using_flags_outside_0x3', 'dir_bytes_not_01', 'edge_class_without_classes', 'edge_class_out_of_range',
            'flag_val_mismatch']
    groups = {}
    for g in sorted({r['group'] for r in res.values()}):
        rs = [r for r in res.values() if r['group'] == g]
        groups[g] = {'files': len(rs), 'totals': {k: sum(r[k] for r in rs) for k in keys},
                     'with_nonzero': {k: sum(1 for r in rs if r[k]) for k in keys}}
    return {'maps': res, 'errors': errors, 'groups': groups, 'maps_total': len(res), 'base_contents': base,
            'leftover_nonzero': [m for m, r in res.items() if r['leftover_bytes']]}


COLS = [('dangling_tiles', 'висячие tile (tile>>2 ≥ numBitmapTiles)'), ('dangling_blend', 'висячие blend'),
        ('dangling_3way', 'висячие 3-way'), ('dangling_cliff', 'висячие cliff'),
        ('flags_outside_0x3', 'описаний с флагами вне &0x3'), ('cells_using_flags_outside_0x3', 'клеток с такими описаниями'),
        ('dir_bytes_not_01', 'направление не 0/1'), ('edge_class_without_classes', 'customBlendEdgeClass ≥ 0 при 0 классов'),
        ('edge_class_out_of_range', 'customBlendEdgeClass ≥ numEdgeTextureClasses'), ('flag_val_mismatch', 'FLAG_VAL ≠ 0x7ADA0000')]


def md_summary(m):
    L = ['## 5. Карты: счётчики BlendTileData для E1-25', '',
         f'Файлов `.map` с BlendTileData: {m["maps_total"]} (' +
         ', '.join(f'`{g}\\` {x["files"]}' for g, x in m['groups'].items()) + f'), ошибок разбора: {len(m["errors"])}, '
         f'с ненулевым остатком чанка: {len(m["leftover_nonzero"])}. По каждой карте: `maps.md` / `maps.json`.', '',
         '| счётчик | ' + ' | '.join(f'{g}: всего / файлов с ненулём' for g in m['groups']) + ' |',
         '|---|' + '---|' * len(m['groups'])]
    for k, t in COLS:
        L.append(f'| {t} | ' + ' | '.join(f"{x['totals'][k]} / {x['with_nonzero'][k]}" for x in m['groups'].values()) + ' |')
    L.append('')
    hit = [(p, r) for p, r in sorted(m['maps'].items()) if r['dangling_tiles'] or r['flags_outside_0x3'] or
           r['edge_class_out_of_range'] or r['dangling_blend'] or r['dangling_3way'] or r['dangling_cliff']]
    if hit:
        L += ['Файлы с ненулевыми счётчиками:', '']
        for p, r in hit:
            parts = [f'{t} {r[k]}' for k, t in COLS if r[k]]
            L.append(f"- `{p}` (v{r['version']}, numBitmapTiles {r['numBitmapTiles']}, max tile>>2 {r['max_tile_div4']}, "
                     f"numEdgeTextureClasses {r['numEdgeTextureClasses']}): " + '; '.join(parts) +
                     (f"; флаги: {', '.join(f'{a}×{c}' for a, c in r['flags_outside_0x3_values'].items())}" if r['flags_outside_0x3'] else '') +
                     (f"; customBlendEdgeClass: {', '.join(f'{a}×{c}' for a, c in r['edge_class_values'].items())}" if r['edge_class_values'] else ''))
        L.append('')
    return L


def write(m, out):
    import json
    with open(os.path.join(out, 'maps.json'), 'w') as f:
        json.dump(m, f, ensure_ascii=False, indent=1)
    L = ['# Карты: BlendTileData по каждой карте (E1-25)', '',
         'Определения счётчиков см. в docstring `datacheck/maps.py`, а также в `docs/formats/BFME1_vs_ZH.md` §2.4.4. '
         'Учитываются итоговые (выигравшие в VFS) `.map`.', ''] + md_summary(m)[2:]
    L += ['| карта | v | w×h | bitmapTiles | blends | cliffs | edgeCls | max tile>>2 | ' + ' | '.join(t for _, t in COLS) + ' | значения флагов вне 0x3 | значения customBlendEdgeClass |',
          '|' + '---|' * (8 + len(COLS) + 2)]
    for path, r in sorted(m['maps'].items()):
        L.append(f"| `{path}` | {r['version']} | {r['w']}×{r['h']} | {r['numBitmapTiles']} | {r['numBlendedTiles']} | "
                 f"{r['numCliffInfo']} | {r['numEdgeTextureClasses']} | {r['max_tile_div4']} | " +
                 ' | '.join(str(r[k]) for k, _ in COLS) + ' | ' +
                 (', '.join(f'{k}×{c}' for k, c in r['flags_outside_0x3_values'].items()) or '—') + ' | ' +
                 (', '.join(f'{k}×{c}' for k, c in r['edge_class_values'].items()) or '—') + ' |')
    if m['errors']:
        L += ['', '## Ошибки разбора', ''] + [f'- `{k}`: {e}' for k, e in m['errors'].items()]
    with open(os.path.join(out, 'maps.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')
