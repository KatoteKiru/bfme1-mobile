"""QA/Gameplay map extraction (read-only): waypoints, PolygonTriggers, start flags, castle/camp layouts and world
coordinates of construction slots, plus maps\\mapcache.ini vs the actual map files (size + ZH CRC).

  python3 -m datacheck.qamaps --game DIR --out out-all \
      [--map "maps\\map mp fords of isen\\map mp fords of isen.map" ...] [--no-ini]

Formats (DataChunk "CkMp", RefPack "EAR\\0", see datacheck/chunks.py and docs/formats/BFME1_vs_ZH.md):
* ObjectsList/Object v3: x y z angle(rad) f32, flags i32, template u16+ascii, Dict (mapscan.read_dict).
* PolygonTriggers v4/v5: OpenSAGE Logic/Map/PolygonTrigger.cs; points i32 x,y,z (world units).
  Chunk names are matched case-insensitively: some .bse spell it "PolyGonTriggers".
* HeightMapData: w h border i32, v>=3 numBorders + borders (8 bytes, 16 for v>=6), dataSize, samples
  (u16 * 10/256 for v>=5, u8 * 0.625 before). World (x, y) -> sample (x/10 + border, y/10 + border), bilinear
  (OpenSAGE Terrain/HeightMap.cs GetHeight).
* CastleTemplates (only in bases\\*.bse): AssetPropertyKey (u8 type, u24 TOC index = layout name), u32 count,
  {name a16, template a16, offset vec3 f32, angle f32 rad, v>=4: priority u32, phase u32}, v>=2: perimeter
  (u32 bool, u32 n, points f32x2 for v>=3 else i32x3) - OpenSAGE Data/Map/CastleTemplate(s).cs, CastlePerimeter.cs.
* BuildLists v1 (bases\\camps\\camps.map, bases\\others\\others.map): u32 n, {AssetPropertyKey faction, u32 n,
  BuildListInfo (OpenSAGE Logic/Map/BuildListInfo.cs Parse)}.
* ZH map CRC (Core/GameEngine/Source/GameClient/MapUtil.cpp:115 calcCRC + Common/crc.h): over the raw file bytes,
  crc = rotl(crc, 1) + byte (mod 2^32).
"""
import argparse
import math
import os
import struct
from collections import Counter, defaultdict

from .maps import probe
from . import mapscan

XY = 10.0
WAYPOINT = '*waypoints/waypoint'


def a16(d, p):
    n = struct.unpack_from('<H', d, p)[0]
    return d[p + 2:p + 2 + n].decode('latin-1'), p + 2 + n


def zh_crc(data):
    c = 0
    for x in data:
        c = (((c << 1) | (c >> 31)) + x) & 0xffffffff
    return c


class MapFile:
    def __init__(self, raw, path=''):
        self.path = path
        self.d, self.compression = probe.map_bytes(raw)
        self.toc, p = probe.parse_toc(self.d)
        self.tops = probe.top_chunks(self.d, self.toc, p)
        self._by = {}
        for n, v, s, b in self.tops:
            self._by.setdefault(n.lower(), (n, v, s, b))

    def chunk(self, name):
        return self._by.get(name.lower())

    # -- heightmap ------------------------------------------------------------------------------------------
    def heightmap(self):
        c = self.chunk('HeightMapData')
        if not c:
            return None
        n, v, s, b = c
        d = self.d
        w, h, border = struct.unpack_from('<iii', d, b)
        q = b + 12
        if v >= 3:
            nb = struct.unpack_from('<i', d, q)[0]
            q += 4 + nb * (16 if v >= 6 else 8)
        ds = struct.unpack_from('<i', d, q)[0]
        q += 4
        if v >= 5:
            data = struct.unpack_from('<%dH' % ds, d, q)
            scale = 10.0 / 256.0
        else:
            data = d[q:q + ds]
            scale = 0.625
        return {'w': w, 'h': h, 'border': border, 'version': v, 'scale': scale, 'data': data}

    @staticmethod
    def height(hm, x, y):
        if hm is None:
            return None

        def conv(p, m):
            p = min(max(p / XY + hm['border'], 0), m - 1)
            i0 = int(math.floor(p))
            return i0, min(i0 + 1, m - 1), p - i0
        x0, x1, fx = conv(x, hm['w'])
        y0, y1, fy = conv(y, hm['h'])
        g = lambda i, j: hm['data'][i + j * hm['w']] * hm['scale']
        lo = g(x1, y0) * fx + g(x0, y0) * (1 - fx)
        hi = g(x1, y1) * fx + g(x0, y1) * (1 - fx)
        return hi * fy + lo * (1 - fy)

    # -- objects --------------------------------------------------------------------------------------------
    def objects(self):
        c = self.chunk('ObjectsList')
        out = []
        if not c:
            return out
        n, v, s, b = c
        d, q, end = self.d, b, b + s
        idx = 0
        while q < end:
            cid, ver, size = probe.hdr(d, q)
            o = q + 10
            if self.toc.get(cid) == 'Object':
                x, y, z, a, fl, ln = struct.unpack_from('<ffffiH', d, o)
                name = d[o + 22:o + 22 + ln].decode('latin-1')
                dd, _e = mapscan.read_dict(d, o + 22 + ln)
                out.append({'i': idx, 'template': name, 'x': x, 'y': y, 'z': z, 'angle': a, 'flags': fl,
                            'dict': {self.toc.get(k, k): val for k, val in dd.items()}})
                idx += 1
            q = o + size
        return out

    # -- polygon triggers -----------------------------------------------------------------------------------
    def polygon_triggers(self):
        c = self.chunk('PolygonTriggers')
        if not c:
            return []
        n, v, s, b = c
        d = self.d
        cnt = struct.unpack_from('<I', d, b)[0]
        o = b + 4
        out = []
        for _ in range(cnt):
            t = {}
            t['name'], o = a16(d, o)
            if v >= 4:
                t['layer'], o = a16(d, o)
            t['id'] = struct.unpack_from('<I', d, o)[0]
            o += 4
            if v >= 2:
                t['is_water'] = bool(d[o])
                o += 1
            if v >= 3:
                t['is_river'] = bool(d[o])
                t['river_start'] = struct.unpack_from('<I', d, o + 1)[0]
                o += 5
            if v >= 5:
                for k in ('river_tex', 'noise_tex', 'alpha_edge_tex', 'sparkle_tex', 'bump_tex', 'sky_tex'):
                    t[k], o = a16(d, o)
                o += 1 + 3 + 1 + 8 + 4      # additive u8, RGB, u8 0, uvScroll vec2, alpha f32
            np_ = struct.unpack_from('<I', d, o)[0]
            o += 4
            t['points'] = [struct.unpack_from('<iii', d, o + 12 * i) for i in range(np_)]
            o += 12 * np_
            out.append(t)
        if o != b + s:
            raise ValueError(f'PolygonTriggers v{v}: parsed {o - b} of {s} bytes')
        return out

    # -- castle templates (.bse) ----------------------------------------------------------------------------
    def castle_templates(self):
        c = self.chunk('CastleTemplates')
        if not c:
            return None
        n, v, s, b = c
        d = self.d
        name = self.toc.get(int.from_bytes(d[b + 1:b + 4], 'little'))
        o = b + 4
        cnt = struct.unpack_from('<I', d, o)[0]
        o += 4
        items = []
        for _ in range(cnt):
            it = {}
            it['name'], o = a16(d, o)
            it['template'], o = a16(d, o)
            it['dx'], it['dy'], it['dz'], it['angle'] = struct.unpack_from('<4f', d, o)
            o += 16
            if v >= 4:
                it['priority'], it['phase'] = struct.unpack_from('<II', d, o)
                o += 8
            items.append(it)
        perim = []
        if v >= 2:
            has = struct.unpack_from('<I', d, o)[0]
            o += 4
            if has:
                np_ = struct.unpack_from('<I', d, o)[0]
                o += 4
                for _ in range(np_):
                    if v >= 3:
                        perim.append(struct.unpack_from('<2f', d, o))
                        o += 8
                    else:
                        perim.append(struct.unpack_from('<3i', d, o))
                        o += 12
        if o != b + s:
            raise ValueError(f'CastleTemplates v{v}: parsed {o - b} of {s} bytes')
        return {'version': v, 'name': name, 'items': items, 'perimeter': perim}

    # -- build lists (camps.map / others.map) ---------------------------------------------------------------
    def build_lists(self):
        c = self.chunk('BuildLists')
        if not c:
            return None
        n, v, s, b = c
        d = self.d
        cnt = struct.unpack_from('<I', d, b)[0]
        o = b + 4
        out = []
        for _ in range(cnt):
            fac = self.toc.get(int.from_bytes(d[o + 1:o + 4], 'little'))
            ni = struct.unpack_from('<I', d, o + 4)[0]
            o += 8
            items = []
            for _ in range(ni):
                it = {}
                it['name'], o = a16(d, o)
                it['template'], o = a16(d, o)
                it['x'], it['y'], it['z'], it['angle'] = struct.unpack_from('<4f', d, o)
                o += 16
                it['initially_built'] = bool(d[o])
                it['rebuilds'] = struct.unpack_from('<I', d, o + 1)[0]
                o += 5
                it['script'], o = a16(d, o)
                o += 4 + 3
                items.append(it)
            out.append({'faction': fac, 'items': items})
        if o != b + s:
            raise ValueError(f'BuildLists v{v}: parsed {o - b} of {s} bytes')
        return out


def point_in_poly(x, y, pts):
    """Even-odd crossing test on the polygon's x, y (ZH PolygonTrigger::pointInTrigger is the same kind of test)."""
    inside = False
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i][0], pts[i][1]
        x2, y2 = pts[(i + 1) % n][0], pts[(i + 1) % n][1]
        if (y1 > y) != (y2 > y):
            xc = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < xc:
                inside = not inside
    return inside


def poly_stats(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    a = cx = cy = 0.0
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i][0], pts[i][1]
        x2, y2 = pts[(i + 1) % n][0], pts[(i + 1) % n][1]
        cr = x1 * y2 - x2 * y1
        a += cr
        cx += (x1 + x2) * cr
        cy += (y1 + y2) * cr
    if abs(a) > 1e-9:
        cx, cy = cx / (3 * a), cy / (3 * a)
    else:
        cx, cy = sum(xs) / n, sum(ys) / n
    return {'bbox': (min(xs), min(ys), max(xs), max(ys)), 'centroid': (cx, cy), 'area': abs(a) / 2, 'n': n}


# ---------------------------------------------------------------------------------------------------------------
# INI side: which object is a flag (CastleBehavior), which layout per faction, KindOf of slot templates

def load_ini(vfs):
    from .schema import Schema
    from .ini import IniAnalyzer
    from .objects import resolve
    from .recruit import Recruit
    an = IniAnalyzer(Schema(), vfs)
    inis = [k for k in vfs.files if k.endswith('.ini')]
    texts = {k: vfs.read(k).decode('latin-1') for k in inis}
    for k in inis:
        an.collect_defines(vfs.files[k].path, texts[k])
    records = []
    for k in inis:
        an.parse_text(vfs.files[k].path, texts[k], records=records, source=vfs.files[k].source)
    final, _missing = resolve(an.objects, only=lambda o: o['file'].lower().startswith('data\\ini'))
    return Recruit(records, final, an.objects)


class IniInfo:
    def __init__(self, recruit):
        self.r = recruit

    def castle_entries(self, obj):
        """[(faction, layout, where)] from the final CastleBehavior module(s) of obj."""
        out = []
        for tag, m in self.r.final.get(obj.lower(), {}).items():
            if m['type'] != 'CastleBehavior':
                continue
            for f, v, w in self.r.module_refs.get(m['where'], ()):
                t = v.split()
                if f == 'castletounpackforfaction' and len(t) >= 2:
                    out.append((t[0], t[1], w))
        return out

    def module_where(self, obj, mtype):
        for tag, m in self.r.final.get(obj.lower(), {}).items():
            if m['type'] == mtype:
                return m['where']
        return None

    def kindof(self, obj):
        k = self.r.obj_field(obj, 'kindof')
        return (k[0].upper().split(), k[1]) if k else ([], None)

    def field(self, obj, f):
        return self.r.obj_field(obj, f.lower())

    def commandset_buttons(self, obj):
        cs = self.r.obj_field(obj, 'commandset')
        if not cs:
            return None, []
        return cs[0], [e[1] for e in self.r.commandset_edges(cs[0])]

    def button_command(self, b):
        for f, v, w in self.r.blocks['commandbutton'].get(b.lower(), ()):
            if f.lower() == 'command':
                return v.split()[0].upper() if v.split() else ''
        return None


def slot_kind(kinds):
    """Role of a layout item by its final KindOf (2.22 INI): see qa-maps.md section 3."""
    k = set(kinds)
    if 'CASTLE_CENTER' in k:
        return 'center'
    if 'BASE_SITE' in k:
        return 'site_flag'            # nested flag (unpacks its own layout)
    if 'BASE_DEFENSE_FOUNDATION' in k:
        return 'defense_slot'
    if 'BASE_FOUNDATION' in k:
        return 'building_slot'
    if 'CASTLE_KEEP' in k:
        return 'keep'
    if 'WALL_UPGRADE' in k:
        return 'wall_upgrade_slot'    # castle wall hardpoint: turret / garrison / postern upgrades (Gondor, Rohan castles)
    if 'DEFENSIVE_WALL' in k or 'WALK_ON_TOP_OF_WALL' in k:
        return 'wall'
    if 'TACTICAL_MARKER' in k:
        return 'ai_marker'
    if 'STRUCTURE' in k:
        return 'structure'
    return 'prop' if k else 'undefined'



# ---------------------------------------------------------------------------------------------------------------

def place(flag, it, rot_mode):
    """World position of a CastleTemplate item for a flag object. rot_mode 'pva': rotate by the flag's
    PlacementViewAngle (OpenSAGE CastleBehavior.Unpack; 0 for every flag in 2.22) -> pure translation;
    'map': additionally rotate by the flag's map angle (hypothesis, not confirmed)."""
    th = flag.get('pva', 0.0) + (flag['angle'] if rot_mode == 'map' else 0.0)
    c, s = math.cos(th), math.sin(th)
    return (flag['x'] + c * it['dx'] - s * it['dy'], flag['y'] + s * it['dx'] + c * it['dy'], th + it['angle'])


def load_layouts(vfs):
    res, errors = {}, {}
    for k, vf in vfs.files.items():
        if not k.endswith('.bse'):
            continue
        try:
            mf = MapFile(vfs.read(k), vf.path)
            ct = mf.castle_templates()
            objs = mf.objects()
            res[os.path.splitext(k.rsplit('\\', 1)[-1])[0]] = {'path': vf.path, 'source': vf.source, 'ct': ct,
                                                               'objects': objs, 'chunks': [(n, v) for n, v, s, b in mf.tops]}
        except Exception as e:  # noqa: BLE001
            errors[vf.path] = f'{type(e).__name__}: {e}'
    return res, errors


def layout_vs_objects(lay):
    """Check CastleTemplates offsets == ObjectsList positions - BaseCenter* (mean of the center objects)."""
    ct, objs = lay['ct'], lay['objects']
    if not ct:
        return None
    centers = [o for o in objs if o['template'].lower().startswith('basecenter')]
    if not centers:   # castles: no BaseCenterGeneric; CastleFlag sits near (not exactly at) the template centre
        centers = [o for o in objs if o['template'].lower().startswith('castleflag')]
    if not centers:
        return {'center': None}
    cx = sum(o['x'] for o in centers) / len(centers)
    cy = sum(o['y'] for o in centers) / len(centers)
    cset = {o['i'] for o in centers}
    rest = [o for o in objs if o['i'] not in cset]
    used, worst = set(), 0.0
    matched = 0
    for it in ct['items']:
        best = None
        for o in rest:
            if o['i'] in used or o['template'] != it['template']:
                continue
            e = math.hypot(o['x'] - cx - it['dx'], o['y'] - cy - it['dy']) + abs(o['angle'] - it['angle'])
            if best is None or e < best[0]:
                best = (e, o)
        if best and best[0] < 0.05:
            used.add(best[1]['i'])
            matched += 1
            worst = max(worst, best[0])
    return {'center': [c['template'] for c in centers], 'cx': cx, 'cy': cy, 'items': len(ct['items']),
            'objects_wo_center': len(rest), 'matched': matched, 'worst_err': worst}


# ---------------------------------------------------------------------------------------------------------------

def _cache_blocks(text):
    """{lower map path: {'line', 'filesize', 'filecrc', 'numplayers', 'ismultiplayer', 'isofficial', 'starts': {n: (x, y)}}}"""
    import re
    res, cur = {}, None
    for i, line in enumerate(text.splitlines(), 1):
        t = line.split(';', 1)[0].strip()
        if not t:
            continue
        if t.startswith('MapCache '):
            cur = {'line': i, 'starts': {}}
            res[mapscan._unescape(t.split(None, 1)[1]).lower()] = cur
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
        else:
            m = mapscan.START_RX.match(k)
            if m:
                xy = re.findall(r'([XYZ]):\s*(-?[0-9.]+)', v)
                d = {a: float(b) for a, b in xy}
                cur['starts'][int(m.group(1))] = (d.get('X'), d.get('Y'))
    return res


def map_starts(mf):
    st = {}
    for o in mf.objects():
        if o['template'].lower() == WAYPOINT:
            m = mapscan.START_RX.match(str(o['dict'].get('waypointName', '')))
            if m:
                st[int(m.group(1))] = (o['x'], o['y'])
    n = 0
    while n < 8 and (n + 1) in st:      # ZH WaypointMap::update, MapUtil.cpp:325-341
        n += 1
    return st, max(1, n)


def mapcache_check(vfs, starts=True):
    vf = vfs.get('maps\\mapcache.ini')
    out = {'winning': None, 'versions': [], 'rows': []}
    if vf is None:
        return out
    out['winning'] = f'{vf.source}:{vf.path}'
    out['versions'] = [(vf.source, vf.path, vf.size)] + [(lab, e.name if e is not None else '', sz) for lab, e, sz in vf.overridden]
    caches = {vf.source: _cache_blocks(vfs.read(vf.path).decode('latin-1'))}
    for lab, e, sz in vf.overridden:
        if e is not None:
            caches[lab] = _cache_blocks(e.read().decode('latin-1'))
    cache = caches[vf.source]
    for k, f in vfs.files.items():
        if not (k.startswith('maps\\') and k.endswith('.map')):
            continue
        e = cache.get(k)
        versions = [(f.source, f.entry, f.size)] + list(f.overridden)
        crcs = {lab: zh_crc(ent.read()) for lab, ent, sz in versions if ent is not None}
        row = {'map': f.path, 'source': f.source, 'size': f.size, 'crc': crcs.get(f.source), 'in_cache': e is not None,
               'other_versions': [(lab, sz, crcs.get(lab)) for lab, ent, sz in f.overridden]}
        if starts:
            try:
                st, n = map_starts(MapFile(f.read(), f.path))
                row['map_numplayers'], row['map_starts'] = n, st
            except Exception as ex:  # noqa: BLE001
                row['map_error'] = str(ex)
        if e is not None:
            row.update(cache_line=e['line'], cache_size=e.get('filesize'), cache_crc=e.get('filecrc'),
                       cache_numplayers=e.get('numplayers'), cache_mp=e.get('ismultiplayer'),
                       cache_official=e.get('isofficial'), cache_starts=e['starts'])
            row['cache_describes'] = next((lab for lab, ent, sz in versions
                                           if sz == e.get('filesize') and crcs.get(lab) == e.get('filecrc')), None)
            if starts and 'map_starts' in row:
                d = 0.0
                for i, (x, y) in e['starts'].items():
                    if i in row['map_starts'] and x is not None:
                        mx, my = row['map_starts'][i]
                        d = max(d, math.hypot(mx - x, my - y))
                row['starts_max_diff'] = d
        out['rows'].append(row)
    files = {k for k, f in vfs.files.items() if k.startswith('maps\\') and k.endswith('.map')}
    out['cache_without_file'] = sorted(k for k in cache if k not in files)
    out['cache_entries'] = {lab: len(c) for lab, c in caches.items()}
    return out


# ---------------------------------------------------------------------------------------------------------------

def flags_on_map(objs, info, hm):
    flags = []
    for o in objs:
        ents = info.castle_entries(o['template']) if info else []
        if not ents:
            continue
        pva = info.field(o['template'], 'placementviewangle')
        try:
            pva_v = math.radians(float(pva[0].split()[0])) if pva else 0.0
        except ValueError:
            pva_v = 0.0
        owner = str(o['dict'].get('originalOwner', ''))
        team = owner.split('/', 1)[1] if '/' in owner else owner
        flags.append(dict(o, entries=ents, pva=pva_v, team=team, name=str(o['dict'].get('objectName', '')),
                          ground=MapFile.height(hm, o['x'], o['y'])))
    return flags


def slots_for_map(mpath, mf, info, layouts):
    hm = mf.heightmap()
    objs = mf.objects()
    flags = flags_on_map(objs, info, hm)
    starts = {}
    for o in objs:
        if o['template'].lower() == WAYPOINT:
            nm = str(o['dict'].get('waypointName', ''))
            m = mapscan.START_RX.match(nm)
            if m:
                starts[int(m.group(1))] = o
    rows = []
    for fl in flags:
        m = mapscan.re.match(r'Player_(\d+)_Inherit', fl['team'] or '')
        start = int(m.group(1)) if m else None
        for fac, lay, where in fl['entries']:
            L = layouts.get(lay.lower())
            if not L or not L['ct']:
                rows.append({'map': mpath, 'start': start, 'flag': fl, 'faction': fac, 'layout': lay, 'missing': True})
                continue
            for it in L['ct']['items']:
                kinds, _ = info.kindof(it['template'])
                wx, wy, wa = place(fl, it, 'pva')
                mx, my, ma = place(fl, it, 'map')
                rows.append({'map': mpath, 'start': start, 'flag': fl, 'faction': fac, 'layout': lay, 'item': it,
                             'kind': slot_kind(kinds), 'kindof': ' '.join(kinds), 'wx': wx, 'wy': wy, 'wa': wa,
                             'wz': MapFile.height(hm, wx, wy), 'mx': mx, 'my': my, 'ma': ma})
    return {'flags': flags, 'starts': starts, 'rows': rows, 'hm': hm}


def fmt(v, nd=2):
    return '' if v is None else (f'{v:.{nd}f}' if isinstance(v, float) else str(v))


def write_tsv(path, header, rows):
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\t'.join(header) + '\n')
        for r in rows:
            f.write('\t'.join(fmt(x) for x in r) + '\n')


def main(argv=None):
    ap = argparse.ArgumentParser(prog='datacheck.qamaps')
    ap.add_argument('--game', default=os.environ.get('BFME_GAMEDATA'), required=not os.environ.get('BFME_GAMEDATA'))
    ap.add_argument('--out', default='out-all')
    ap.add_argument('--map', action='append', default=[], help='map path(s) to dump (VFS path, case-insensitive)')
    ap.add_argument('--slots-map', action='append', default=[], help='map path(s) for the slot TSV')
    ap.add_argument('--point', action='append', default=[], help='x,y: report which PolygonTriggers of --map contain it')
    ap.add_argument('--no-mapcache', action='store_true', help='skip the (slow, ~100 s) mapcache check')
    args = ap.parse_args(argv)
    from .vfs import VFS
    import json
    vfs = VFS(args.game, include_loose=False)
    os.makedirs(args.out, exist_ok=True)
    info = IniInfo(load_ini(vfs))
    layouts, lerr = load_layouts(vfs)
    res = {'layouts_errors': lerr, 'layouts': {}, 'maps': {}, 'slot_maps': {}}
    for name, L in sorted(layouts.items()):
        if not L['ct']:
            res['layouts'][name] = {'path': L['path'], 'castle_templates': None}
            continue
        cnt = {}
        for it in L['ct']['items']:
            k = slot_kind(info.kindof(it['template'])[0])
            cnt[k] = cnt.get(k, 0) + 1
        res['layouts'][name] = {'path': L['path'], 'source': L['source'], 'version': L['ct'].get('version'),
                                'items': len(L['ct']['items']), 'kinds': cnt, 'check': layout_vs_objects(L)}
    # 1) dumps
    wp_rows, pt_rows = [], []
    for mp in args.map:
        vf = vfs.get(mp)
        mf = MapFile(vfs.read(mp), vf.path)
        hm = mf.heightmap()
        for o in mf.objects():
            if o['template'].lower() == WAYPOINT:
                dd = o['dict']
                wp_rows.append((vf.path, vf.source, dd.get('waypointName', ''), dd.get('waypointID', ''), o['x'], o['y'], o['z'],
                                MapFile.height(hm, o['x'], o['y']), dd.get('waypointPathLabel1', ''),
                                dd.get('waypointPathLabel2', ''), dd.get('waypointPathLabel3', ''), o['i']))
        pts = []
        for t in mf.polygon_triggers():
            st = poly_stats(t['points'])
            pts.append({'name': t['name'], 'id': t['id'], 'n': len(t['points']), **st})
            for j, (x, y, z) in enumerate(t['points']):
                pt_rows.append((vf.path, vf.source, t['name'], t.get('layer', ''), t['id'], int(t.get('is_water', 0)),
                                int(t.get('is_river', 0)), j, x, y, z, st['centroid'][0], st['centroid'][1]))
        res['maps'][vf.path] = {'source': vf.source, 'overridden': [(a, b) for a, _, b in vf.overridden], 'triggers': pts,
                                'points': {}}
        for p in args.point:
            x, y = (float(v) for v in p.split(','))
            res['maps'][vf.path]['points'][p] = {
                'inside': [t['name'] for t in mf.polygon_triggers() if point_in_poly(x, y, [(a, b) for a, b, _ in t['points']])],
                'z_terrain': MapFile.height(hm, x, y)}
    if wp_rows:
        write_tsv(os.path.join(args.out, 'qa-waypoints.tsv'),
                  ['map', 'archive', 'waypointName', 'waypointID', 'x', 'y', 'z_object', 'z_terrain', 'path1', 'path2', 'path3', 'obj_index'], wp_rows)
        write_tsv(os.path.join(args.out, 'qa-polygontriggers.tsv'),
                  ['map', 'archive', 'name', 'layer', 'id', 'is_water', 'is_river', 'point', 'x', 'y', 'z', 'centroid_x', 'centroid_y'], pt_rows)
    # 2) slots
    srows = []
    for mp in args.slots_map:
        vf = vfs.get(mp)
        mf = MapFile(vfs.read(mp), vf.path)
        r = slots_for_map(vf.path, mf, info, layouts)
        summ = {}
        for x in r['rows']:
            if x.get('missing'):
                continue
            key = f"{x['flag']['name'] or x['flag']['template']}|{x['layout']}"
            summ.setdefault(key, {})
            summ[key][x['kind']] = summ[key].get(x['kind'], 0) + 1
        res['slot_maps'][vf.path] = {
            'source': vf.source,
            'flags': [{k: fl[k] for k in ('template', 'name', 'team', 'x', 'y', 'angle', 'pva', 'ground')} | {'entries': fl['entries']}
                      for fl in r['flags']],
            'starts': {n: (o['x'], o['y']) for n, o in r['starts'].items()},
            'missing_layouts': sorted({x['layout'] for x in r['rows'] if x.get('missing')}),
            'per_flag_layout': summ}
        for x in r['rows']:
            fl = x['flag']
            if x.get('missing'):
                continue
            it = x['item']
            srows.append((vf.path, vf.source, x['start'] or '', fl['template'], fl['name'], fl['team'], fl['x'], fl['y'],
                          math.degrees(fl['angle']), x['faction'], x['layout'], it['template'], x['kind'], it['dx'], it['dy'],
                          math.degrees(it['angle']), x['wx'], x['wy'], x['wz'], math.degrees(x['wa']),
                          x['mx'], x['my'], math.degrees(x['ma'])))
    if srows:
        write_tsv(os.path.join(args.out, 'qa-slots.tsv'),
                  ['map', 'archive', 'player_start', 'flag_template', 'flag_objectName', 'flag_team', 'flag_x', 'flag_y',
                   'flag_angle_deg', 'faction', 'layout', 'template', 'slot_kind', 'local_dx', 'local_dy', 'local_angle_deg',
                   'world_x', 'world_y', 'world_z_terrain', 'world_angle_deg',
                   'world_x_if_map_angle', 'world_y_if_map_angle', 'world_angle_deg_if_map_angle'], srows)
        # build slots only (what Gameplay needs): foundations, defence foundations, keeps / prebuilt economy buildings
        keep = {'building_slot', 'defense_slot', 'wall_upgrade_slot', 'keep', 'site_flag', 'center'}
        write_tsv(os.path.join(args.out, 'qa-slots-build.tsv'),
                  ['map', 'archive', 'player_start', 'flag_template', 'flag_objectName', 'flag_team', 'flag_x', 'flag_y',
                   'flag_angle_deg', 'faction', 'layout', 'template', 'slot_kind', 'local_dx', 'local_dy', 'local_angle_deg',
                   'world_x', 'world_y', 'world_z_terrain', 'world_angle_deg',
                   'world_x_if_map_angle', 'world_y_if_map_angle', 'world_angle_deg_if_map_angle'],
                  [r for r in srows if r[12] in keep])
    # 3) mapcache
    if args.no_mapcache:
        with open(os.path.join(args.out, 'qa-maps.json'), 'w') as f:
            json.dump(res, f, ensure_ascii=False, indent=1, default=str)
        return 0
    mc = mapcache_check(vfs)
    write_tsv(os.path.join(args.out, 'qa-mapcache.tsv'),
              ['map', 'archive', 'size', 'zh_crc', 'other_versions(archive:size:crc)', 'in_cache', 'cache_line', 'cache_size',
               'cache_crc', 'cache_describes', 'cache_numplayers', 'map_numplayers', 'starts_max_diff'],
              [(r['map'], r['source'], r['size'], r['crc'], ' '.join(f'{a}:{b}:{c}' for a, b, c in r['other_versions']),
                'yes' if r['in_cache'] else 'no', r.get('cache_line', ''), r.get('cache_size', ''), r.get('cache_crc', ''),
                r.get('cache_describes') or ('' if not r['in_cache'] else 'none'), r.get('cache_numplayers', ''),
                r.get('map_numplayers', ''), r.get('starts_max_diff', '')) for r in mc['rows']])
    res['mapcache'] = {k: v for k, v in mc.items() if k != 'rows'}
    with open(os.path.join(args.out, 'qa-maps.json'), 'w') as f:
        json.dump(res, f, ensure_ascii=False, indent=1, default=str)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
