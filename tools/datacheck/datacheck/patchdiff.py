"""'What patch 2.22 changes on top of the base': value diff per block/field for INI files overridden by patch archives."""
import re

from .ini import IniAnalyzer
from .values import normalize

PATCH_RE = re.compile(r'(^|[\\/])[_!]*patch', re.I)


def is_patch_archive(label):
    return bool(PATCH_RE.search(label))


def _records(schema, vfs, defines, path, data):
    a = IniAnalyzer(schema, vfs)
    a.defines = dict(defines)
    recs = a.parse_text(path, data.decode('latin-1'), records=[])
    out = {}
    for key, field, value, resolved, where in recs:
        out[key] = (field, value, resolved, where)
    return out, a.warnings


def key_label(key):
    parts = []
    for p in key:
        a, b = p
        parts.append(f'{a} {b}'.strip() if not isinstance(b, int) else (f'{a}' + (f'[{b}]' if b else '')))
    return ' / '.join(parts)


def diff_file(schema, vfs, defines, path, base_entry, final_entry, base_label, final_label):
    b, bw = _records(schema, vfs, defines, path, base_entry.read())
    f, fw = _records(schema, vfs, defines, path, final_entry.read())
    changes = []
    for k in f:
        if k in b:
            if normalize(b[k][2]) != normalize(f[k][2]):
                changes.append({'op': 'changed', 'key': key_label(k), 'field': f[k][0],
                                'base': b[k][1], 'final': f[k][1],
                                'base_at': f'{base_label}:{b[k][3]}', 'final_at': f'{final_label}:{f[k][3]}'})
        else:
            changes.append({'op': 'added', 'key': key_label(k), 'field': f[k][0], 'base': None,
                            'final': f[k][1], 'base_at': None, 'final_at': f'{final_label}:{f[k][3]}'})
    for k in b:
        if k not in f:
            changes.append({'op': 'removed', 'key': key_label(k), 'field': b[k][0], 'base': b[k][1],
                            'final': None, 'base_at': f'{base_label}:{b[k][3]}', 'final_at': None})
    return changes, len(bw) + len(fw)


def _world_records(schema, vfs, items, defines):
    """Parse a set of (path, label, entry) as one INI world; key -> (field, value, resolved, label:where)."""
    a = IniAnalyzer(schema, vfs)
    a.defines = dict(defines)
    recs, warn = {}, 0
    for path, label, entry in items:
        out = a.parse_text(path, entry.read().decode('latin-1'), records=[])
        for key, field, value, resolved, where in out:
            recs[key] = (field, value, resolved, f'{label}:{where}')
    return recs, len(a.warnings)


def _collect_defines(schema, vfs, items):
    a = IniAnalyzer(schema, vfs)
    for path, label, entry in items:
        a.collect_defines(path, entry.read().decode('latin-1'))
    return a.defines


def _file_of(at):
    # 'archive:path:line' -> path
    return at.split(':', 1)[1].rsplit(':', 1)[0]


def patch_diffs(schema, vfs, defines=None):
    """What patch archives change on top of the base game.

    data\\ini\\*: one global, block-level diff of the whole base INI world (highest-priority non-patch version of
    every file, with the base #defines) against the final world (winning versions, final #defines). Blocks that the
    patch merely moved to another file are not reported as removed/added (2.22 splits object files into 235 new
    INIs); they are counted per destination file in 'moved_in'. Other INIs (maps\\*\\map.ini): per-file diff.
    Returns (files, added_files); each file entry groups changes by the file they land in."""
    base_items, final_items, added_files, per_file = [], [], [], []
    involved = False
    for key, vf in vfs.files.items():
        if not key.endswith('.ini') or vf.entry is None:
            continue
        patched = is_patch_archive(vf.source)
        base = [o for o in vf.overridden if not is_patch_archive(o[0])] if patched else []
        if patched:
            involved = True
            if not base:
                added_files.append({'path': vf.path, 'archive': vf.source})
        if key.startswith('data\\ini\\'):
            final_items.append((vf.path, vf.source, vf.entry))
            if not patched:
                base_items.append((vf.path, vf.source, vf.entry))
            elif base:
                base_items.append((vf.path, base[0][0], base[0][1]))
        elif patched and base:
            per_file.append((vf, base[0]))
    if not involved:
        return [], []
    bdef = _collect_defines(schema, vfs, base_items)
    fdef = _collect_defines(schema, vfs, final_items)
    b, bw = _world_records(schema, vfs, base_items, bdef)
    f, fw = _world_records(schema, vfs, final_items, fdef)
    groups = {}

    def grp(path, base_label, final_label):
        g = groups.get(path.lower())
        if g is None:
            g = groups[path.lower()] = {'path': path, 'base': base_label, 'final': final_label, 'changes': [],
                                        'moved_in': [], 'other_versions': [], 'parse_warnings': 0}
        return g

    def label(at):
        return at.split(':', 1)[0]

    for k, fv in f.items():
        fpath = _file_of(fv[3])
        if k in b:
            bv = b[k]
            if len(k) == 1 and _file_of(bv[3]).lower() != fpath.lower():
                grp(fpath, label(bv[3]), label(fv[3]))['moved_in'].append(
                    {'block': key_label(k), 'from': bv[3], 'to': fv[3]})
            if normalize(bv[2]) != normalize(fv[2]):
                grp(fpath, label(bv[3]), label(fv[3]))['changes'].append(
                    {'op': 'changed', 'key': key_label(k), 'field': fv[0], 'base': bv[1], 'final': fv[1],
                     'base_resolved': bv[2], 'final_resolved': fv[2], 'base_at': bv[3], 'final_at': fv[3]})
        else:
            grp(fpath, '', label(fv[3]))['changes'].append(
                {'op': 'added', 'key': key_label(k), 'field': fv[0], 'base': None, 'final': fv[1],
                 'base_at': None, 'final_at': fv[3]})
    for k, bv in b.items():
        if k not in f:
            grp(_file_of(bv[3]), label(bv[3]), '')['changes'].append(
                {'op': 'removed', 'key': key_label(k), 'field': bv[0], 'base': bv[1], 'final': None,
                 'base_at': bv[3], 'final_at': None})
    # #define values that changed (they affect every field using them)
    for name in sorted(set(bdef) | set(fdef)):
        bv, fv = bdef.get(name), fdef.get(name)
        if bv is not None and fv is not None and normalize(bv[0]) == normalize(fv[0]):
            continue
        grp('#define', 'base', 'final')['changes'].append(
            {'op': 'changed' if bv and fv else ('added' if fv else 'removed'), 'key': f'#define {name}', 'field': name,
             'base': bv[0] if bv else None, 'final': fv[0] if fv else None,
             'base_at': bv[1] if bv else None, 'final_at': fv[1] if fv else None})
    files = [g for g in groups.values() if g['changes'] or g['moved_in']]
    if files:
        files[0]['parse_warnings'] = bw + fw
    for vf, (blabel, bentry, _) in per_file:
        ch, nw = diff_file(schema, vfs, defines or fdef, vf.path, bentry, vf.entry, blabel, vf.source)
        files.append({'path': vf.path, 'base': blabel, 'final': vf.source, 'changes': ch, 'moved_in': [],
                      'other_versions': [o[0] for o in vf.overridden], 'parse_warnings': nw})
    files.sort(key=lambda g: (g['path'] == '#define', g['path'].lower()))
    return files, added_files


def find_field_everywhere(vfs, names):
    """All occurrences of given field names in every version of every INI (winning and overridden)."""
    pat = re.compile(r'^\s*(' + '|'.join(map(re.escape, names)) + r')\b\s*=?\s*([^;\r\n]*)', re.I)
    hits = []
    for key, vf in vfs.files.items():
        if not key.endswith('.ini'):
            continue
        versions = [(vf.source, vf.entry, True)] + [(o[0], o[1], False) for o in vf.overridden]
        for label, entry, winning in versions:
            data = entry.read() if entry is not None else vfs.read(vf.path)
            for i, line in enumerate(data.decode('latin-1').splitlines(), 1):
                m = pat.match(line)
                if m:
                    hits.append({'field': m.group(1), 'value': m.group(2).strip(), 'file': vf.path,
                                 'archive': label, 'line': i, 'winning': winning})
    return hits


CP_RX = re.compile(r'^\s*(Good|Evil)(\w*CommandPoint\w*)\s*=\s*([^;\r\n]*?)\s*(?:;(.*))?$', re.I)


def command_point_sets(vfs, file_suffix='gamedata.ini'):
    """Good/Evil*CommandPoint* pairs of every version of every */gamedata.ini, labelled with the nearest ';;;'
    comment line above (the block caption in the file) and the inline comment."""
    sets = []
    for key, vf in vfs.files.items():
        if not key.endswith(file_suffix):
            continue
        versions = [(vf.source, vf.entry, True)] + [(o[0], o[1], False) for o in vf.overridden]
        for label, entry, winning in versions:
            data = entry.read() if entry is not None else vfs.read(vf.path)
            caption, cur = None, {}
            for i, line in enumerate(data.decode('latin-1').splitlines(), 1):
                s = line.strip()
                if s.startswith(';;;'):
                    caption = (s.strip(';').strip(), i)
                    continue
                m = CP_RX.match(line)
                if not m:
                    continue
                side, name, value, comment = m.group(1), m.group(2), m.group(3).strip(), (m.group(4) or '').strip()
                k = (name.lower(), caption)
                rec = cur.get(k)
                if rec is None:
                    rec = cur[k] = {'set': name, 'caption': caption[0] if caption else None,
                                    'caption_line': caption[1] if caption else None, 'file': vf.path,
                                    'archive': label, 'winning': winning, 'comment': ''}
                    sets.append(rec)
                rec[side.lower()] = value
                rec[side.lower() + '_line'] = i
                if comment and not rec['comment']:
                    rec['comment'] = comment
    return sets
