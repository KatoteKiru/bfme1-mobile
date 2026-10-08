"""Map container decoding used by datacheck: RefPack ("EAR\\0"), DataChunk TOC ("CkMp"), chunk headers and the
heuristic sub-chunk walk. Copied from tools/formatprobe/probe.py (not in git), so that datacheck is self-contained.
"""
import collections
import struct


# ---------------- RefPack ----------------
def refpack_decode(src):
    """src: поток RefPack (начиная с 0x10FB/0x90FB заголовка)."""
    p = 0
    flags = src[p] ; p += 1
    sig = src[p]; p += 1
    if sig != 0xFB:
        raise ValueError('bad refpack sig')
    big = flags & 0x80
    if flags & 0x01:  # compressed size present
        p += 4 if big else 3
    if big:
        usize = int.from_bytes(src[p:p+4], 'big'); p += 4
    else:
        usize = int.from_bytes(src[p:p+3], 'big'); p += 3
    out = bytearray()
    n = len(src)
    while p < n:
        b0 = src[p]
        if b0 < 0x80:
            b1 = src[p+1]; p += 2
            lit = b0 & 3
            out += src[p:p+lit]; p += lit
            cnt = ((b0 & 0x1C) >> 2) + 3
            off = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = src[p+1], src[p+2]; p += 3
            lit = b1 >> 6
            out += src[p:p+lit]; p += lit
            cnt = (b0 & 0x3F) + 4
            off = ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = src[p+1], src[p+2], src[p+3]; p += 4
            lit = b0 & 3
            out += src[p:p+lit]; p += lit
            cnt = ((b0 & 0x0C) << 6) + b3 + 5
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            p += 1
            lit = ((b0 & 0x1F) << 2) + 4
            out += src[p:p+lit]; p += lit
            continue
        else:
            p += 1
            lit = b0 & 3
            out += src[p:p+lit]; p += lit
            break
        start = len(out) - off
        for i in range(cnt):
            out.append(out[start + i])
    return bytes(out), usize

def map_bytes(raw):
    if raw[:4] == b'EAR\0':
        data, usize = refpack_decode(raw[8:])
        return data, 'EAR(RefPack)'
    if raw[:4] == b'ZL5\0' or raw[:3] == b'ZL':
        import zlib
        return zlib.decompress(raw[8:]), 'zlib'
    return bytes(raw), 'raw'

# ---------------- DataChunk ----------------
def parse_toc(buf):
    if buf[:4] != b'CkMp':
        raise ValueError('no CkMp: %r' % buf[:4])
    n = struct.unpack('<i', buf[4:8])[0]
    p = 8; toc = {}
    for _ in range(n):
        l = buf[p]; p += 1
        name = buf[p:p+l].decode('latin-1'); p += l
        cid = struct.unpack('<I', buf[p:p+4])[0]; p += 4
        toc[cid] = name
    return toc, p

def hdr(buf, p):
    cid, ver, size = struct.unpack('<IHi', buf[p:p+10])
    return cid, ver, size

def chain_ok(buf, toc, p, end, depth=0):
    """Проверяет, что от p до end ровно укладывается цепочка валидных чанков."""
    steps = 0
    while p < end:
        if p + 10 > end: return False
        cid, ver, size = hdr(buf, p)
        if cid not in toc or ver > 40 or size < 0 or p + 10 + size > end:
            return False
        p += 10 + size; steps += 1
        if steps > 100000: return False
    return p == end

# чанки, внутри которых (после собственных данных) идут подчанки
CHILDREN = {'Script': {'OrCondition','ScriptAction','ScriptActionFalse'},
            'ScriptGroup': {'Script','ScriptGroup'}, 'ScriptList': {'Script','ScriptGroup'},
            'OrCondition': {'Condition'}, 'PlayerScriptsList': {'ScriptList'},
            'ObjectsList': {'Object'}}

def find_subchain(buf, toc, start, end, first=None):
    """Ищет минимальное смещение >= start, с которого до end идёт цепочка чанков."""
    p = start
    lim = end - 10
    while p <= lim:
        cid = struct.unpack_from('<I', buf, p)[0]
        if cid in toc and (first is None or toc[cid] in first) and chain_ok(buf, toc, p, end):
            return p
        p += 1
    return None

LEAF = {'HeightMapData','BlendTileData','GlobalLighting','WorldInfo','EnvironmentData',
        'SkyboxSettings','NamedCameras','PolygonTriggers','WaypointsList','Object','Condition',
        'ScriptAction','ScriptActionFalse','MapPreview','WorldDict'}

def walk(buf, toc, p, end, path, stats, depth=0, maxdepth=6):
    while p < end:
        cid, ver, size = hdr(buf, p)
        name = toc.get(cid, '?%d' % cid)
        body = p + 10; bend = body + size
        key = '/'.join(path + [name])
        stats[key][ver] += 1
        if name in ('Condition', 'ScriptAction', 'ScriptActionFalse') and size >= 8:
            t, nk = struct.unpack_from('<iI', buf, body)
            kind = 'COND' if name == 'Condition' else 'ACT'
            stats['__names__'][(kind, toc.get(nk >> 8, '?%d' % nk))] += 1
        if depth < maxdepth and size >= 10 and name not in LEAF:
            sub = find_subchain(buf, toc, body, bend, CHILDREN.get(name))
            if sub is not None:
                stats.setdefault('__datalen__', collections.defaultdict(collections.Counter))
                walk(buf, toc, sub, bend, path + [name], stats, depth+1, maxdepth)
        p = bend

def top_chunks(buf, toc, p):
    res = []
    while p + 10 <= len(buf):
        cid, ver, size = hdr(buf, p)
        res.append((toc.get(cid, '?%d' % cid), ver, size, p + 10))
        p += 10 + size
    return res
