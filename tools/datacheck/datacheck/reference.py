"""Reference ("эталон") for the E1-56 harness: what our INI analysis sees in a pinned set of BFME1 archives,
reduced to counters, names (blocks, fields, modules, tags, templates, macros), line numbers, paths inside the
archives and sha256. No INI values, no Lua bodies, no comment text, nothing copied from EA files.

  python3 -m datacheck --game DIR --archives ini.big,maps.big,_patch222.big,_patch222maps.big \
      --reference-out reference/2.22 --reference-only
  python3 -m datacheck --game DIR --archives ini.big,maps.big --reference-out reference/base --reference-only

Files (all UTF-8, LF, deterministic for the same archives):
  manifest.json          archives (name, bytes, sha256, BIG magic, entries) in mount order, schema revisions,
                         label, the command that rebuilds the directory
  ini_files.tsv          every INI: path, archive, sha256, bytes, lines, top-level blocks, warnings, line endings,
                         longest line
  counts.json            analyzer counters and name lists (report.json without values, messages or captions)
  defines.json           #define / substitution / skip counters, macro names with substitution counts
  objects.tsv            Object / ChildObject / ObjectReskin definitions: own and final (after inheritance) field and
                         module counts (objects.object_counts)
  nested_openers.tsv     which keys open a nested block, in which context and by which rule
  beginscript_modules.tsv  modules with BeginScript blocks: module type, tag, wrapper, parent, file:line, blocks
  beginscript_blocks.tsv   every BeginScript block (also inside states ZH would skip): file:line, template, module
                         type and tag, wrapper (AddModule/ReplaceModule), state (AnimationState / IdleAnimationState /
                         TransitionState), body_sha256 + body_bytes of the raw body = bytes from the first byte after
                         the BeginScript token up to (not including) the first byte of the EndScript token, as stored
                         in the archive (CRLF, comments, indentation of the EndScript line included); lines_sha256 /
                         lines_bytes = only the whole lines between the two token lines; zh_end_lines = body lines
                         whose first ZH token (cut at ';', strtok " \t\r\n=") is `end`
  syntax.json            BFME syntax extensions vs the ZH parser: kind, count, file:line of the first example,
                         ZH outcome; Lua block summary without text
"""
import hashlib
import json
import os
import re

from .vfs import VFS
from .schema import Schema
from .ini import IniAnalyzer
from .objects import resolve, module_frequency, object_counts
from .patchdiff import is_patch_archive
from . import syntax

FORMAT = 1
ARCHIVES = {'base': ['ini.big', 'maps.big'],
            '2.22': ['ini.big', 'maps.big', '_patch222.big', '_patch222maps.big']}   # every archive with *.ini


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _file_sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def _tsv(path, cols, rows):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\t'.join(cols) + '\n')
        for r in rows:
            f.write('\t'.join('' if r.get(c) is None else str(r[c]).replace('\t', ' ') for c in cols) + '\n')


def _json(path, obj):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(obj, f, ensure_ascii=False, indent=1, sort_keys=False)
        f.write('\n')


WARN_KINDS = [(re.compile(r'^stray END'), 'stray_end'), (re.compile(r'not closed by END before EOF'), 'not_closed_before_eof'),
              (re.compile(r'^BeginScript without EndScript'), 'beginscript_without_endscript'),
              (re.compile(r'^#include .* not (resolved|found)'), 'include_not_found')]


def _warn_kind(msg):
    for rx, k in WARN_KINDS:
        if rx.search(msg):
            return k
    return 'other'


def _usage(u, fmt=lambda k: k):
    return [{'key': fmt(k), 'count': c, 'example': ex} for k, c, ex, _m in u.items()]


def _eol(data):
    c = {'crlf': data.count(b'\r\n')}
    c['lf'] = data.count(b'\n') - c['crlf']
    c['cr'] = data.count(b'\r') - c['crlf']
    return c


def analyze(game, archives):
    only = {a.lower() for a in archives}
    vfs = VFS(game, include_loose=False, big_filter=lambda rel: os.path.basename(rel).lower() in only)
    mounted = {a.label.lower() for a in vfs.archives}
    if mounted != only:
        raise SystemExit(f'reference: archives missing in {game}: {sorted(only - mounted)}')
    schema = Schema()
    an = IniAnalyzer(schema, vfs)
    inis = [k for k in vfs.files if k.endswith('.ini')]
    texts = {k: vfs.read(k).decode('latin-1') for k in inis}
    for k in inis:
        an.collect_defines(vfs.files[k].path, texts[k])
    records = []
    for k in inis:
        an.parse_text(vfs.files[k].path, texts[k], records=records, source=vfs.files[k].source)
    return vfs, schema, an, records


def build(game, archives, label=None, command=None):
    """Returns {file name: python object or (cols, rows)}."""
    vfs, schema, an, records = analyze(game, archives)
    label = label or ('2.22' if any(is_patch_archive(a.label) for a in vfs.archives) else 'base')
    final, missing = resolve(an.objects, only=lambda o: o['file'].lower().startswith('data\\ini'))
    out = {}
    out['manifest.json'] = {
        'format': FORMAT, 'label': label,
        'archives_mount_order': [{'name': a.label, 'bytes': os.path.getsize(a.path), 'sha256': _file_sha(a.path),
                                  'magic': a.magic, 'entries': len(a.entries)} for a in vfs.archives],
        'schema': {'opensage_revision': schema.os.get('revision'), 'zh_revision': schema.zh.get('revision')},
        'rebuild': command or f'./run.sh --game $BFME_GAMEDATA --archives {",".join(archives)} '
                              f'--reference-out reference/{label} --reference-only',
    }
    rows = []
    for k in [k for k in vfs.files if k.endswith('.ini')]:
        vf = vfs.files[k]
        data = vfs.read(k)
        lines = data.split(b'\n')
        fi = an.files.get(vf.path, {})
        rows.append(dict({'path': vf.path, 'archive': vf.source, 'sha256': _sha(data), 'bytes': len(data),
                          'lines': fi.get('lines', 0), 'top_level_blocks': fi.get('blocks', 0),
                          'warnings': fi.get('warnings', 0),
                          'max_line_len': max((len(l.rstrip(b'\r')) for l in lines), default=0)}, **_eol(data)))
    out['ini_files.tsv'] = (['path', 'archive', 'sha256', 'bytes', 'lines', 'top_level_blocks', 'warnings',
                             'crlf', 'lf', 'cr', 'max_line_len'], rows)

    fmtf = lambda k: f'{k[0]}.{k[1]}'
    decl = {}
    for (mt, name), c in an.modules.count.most_common():
        decl[name] = decl.get(name, 0) + c
    fin_c, fin_o, fin_ex = module_frequency(final)
    wk = {}
    for w, m in an.warnings:
        wk.setdefault(_warn_kind(m), []).append(w)
    out['counts.json'] = {
        'schema': {'opensage_top_level': len(schema.top), 'opensage_tables': len(schema.tables),
                   'opensage_modules': sum(len(m) for m in schema.modules.values()),
                   'zh_blocks': len(schema.zh['blocks']), 'zh_modules': len(schema.zh['modules']),
                   'zh_field_names': len(schema.zh['fields'])},
        'vfs': {'archives': len(vfs.archives), 'files_total': len(vfs.files),
                'files_overridden': sum(1 for v in vfs.files.values() if v.overridden),
                'ini_files': len(rows)},
        'ini': {'files': len(an.files), 'files_with_warnings': sum(1 for f in an.files.values() if f['warnings']),
                'lines': an.n_lines, 'top_level_blocks': an.n_blocks, 'nested_blocks': an.n_nested,
                'field_lines': an.n_field_lines, 'lines_inside_unknown_blocks': an.n_unknown_ctx_lines,
                'embedded_scripts': an.n_scripts, 'structure_warnings': len(an.warnings),
                'warnings_by_kind': {k: {'count': len(v), 'where': v} for k, v in sorted(wk.items())},
                'top_level_by_type': dict(an.top_blocks.count.most_common()),
                'unknown_blocks_total': sum(an.unknown_blocks.count.values()),
                'unknown_fields_total': sum(an.unknown_fields.count.values()),
                'unknown_modules_total': sum(an.unknown_modules.count.values()),
                'unknown_blocks': _usage(an.unknown_blocks),
                'unknown_fields': _usage(an.unknown_fields, fmtf),
                'unknown_fields_as_blocks': _usage(an.unknown_nested, fmtf),
                'unknown_modules': _usage(an.unknown_modules, fmtf),
                'modules_declared': decl,
                'fields_known': _usage(an.fields, lambda k: f"{schema.tables[k[0]]['class']}.{k[1]}"),
                'objects': {'definitions': len(an.objects),
                            'by_kind': _count(o['kind'] for o in an.objects),
                            'resolved_data_ini': len(final), 'missing_parents': dict(missing)},
                'modules_final_after_inheritance': [{'module': f'{k[0]}.{k[1]}', 'objects': c, 'instances': fin_c[k]}
                                                    for k, c in fin_o.most_common()]},
        'bfme_vs_zh': {
            'opensage_bfme_only_used': _usage(an.bfme_only, lambda k: f'{k[0]}: {k[1]}'),
            'not_in_generals_zh_cpp': _usage(an.not_in_zh_cpp, lambda k: f'{k[0]}: {k[1]}'),
            'tagged_later_than_bfme_in_opensage': _usage(an.later_game, lambda k: f'{k[0]}: {k[1]}')},
    }
    used = an.substitutions
    out['defines.json'] = {
        'define_lines': an.n_define_lines, 'macros': len(an.defines),
        'redefinitions': an.n_define_redefined, 'redefinitions_with_other_value': an.n_define_redefined_diff,
        'substitutions_total': sum(used.values()), 'macros_used': len(used),
        'macros_unused': len(set(an.defines) - set(used)),
        'skipped': {'unknown_top_level_blocks': sum(an.unknown_blocks.count.values()),
                    'lines_inside_unknown_blocks': an.n_unknown_ctx_lines,
                    'unknown_fields': sum(an.unknown_fields.count.values()),
                    'unknown_fields_as_blocks': sum(an.unknown_nested.count.values()),
                    'unknown_modules': sum(an.unknown_modules.count.values()),
                    'schema_gaps': sum(an.schema_gaps.count.values())},
        'substitutions_by_macro': dict(sorted(used.items(), key=lambda x: (-x[1], x[0]))),
        'defined_at': {k: v[1] for k, v in sorted(an.defines.items())},
    }
    oc = object_counts(an.objects)
    out['objects.tsv'] = (['scope', 'kind', 'name', 'parent', 'at', 'effective', 'parent_missing', 'own_field_lines',
                           'own_fields', 'final_fields', 'own_module_adds', 'own_module_removes', 'final_modules',
                           'final_modules_inherited'], oc)
    zf = schema.zh_fields
    op_rows = [{'context': k[0], 'table_class': k[1], 'key': k[2], 'how': k[3],
                'opens': 0 if k[3] == 'schema_block_but_value_by_token_count' else 1, 'count': c,
                'key_in_zh': int(k[2].lower() in zf), 'example': ex}
               for k, c, ex, _m in sorted(an.openers.items(), key=lambda x: (x[0][1].lower(), x[0][2].lower(), x[0][3], x[0][0]))]
    out['nested_openers.tsv'] = (['context', 'table_class', 'key', 'how', 'opens', 'count', 'key_in_zh', 'example'], op_rows)

    sx = syntax.build(vfs, schema)
    key = label if label in sx['sets'] else list(sx['sets'])[-1]
    blocks = sx['blocks'][key]
    mods, order = {}, []
    for b in blocks:
        k = (b['field'], b['module_type'], b['module_tag'], b['wrapper'], b['parent_kind'], b['parent'],
             f"{b['source']}:{b['file']}:{b['module_line']}")
        if k not in mods:
            mods[k] = [b['line'], 0]
            order.append(k)
        mods[k][1] += 1
    mcols = ['module_field', 'module_type', 'module_tag', 'module_wrapper', 'template_kind', 'template', 'module_at',
             'first_beginscript_line', 'blocks']
    out['beginscript_modules.tsv'] = (mcols, [dict(zip(mcols, list(k) + mods[k])) for k in order])
    brows = []
    for b in blocks:
        brows.append({
            'at': f"{b['source']}:{b['file']}:{b['line']}", 'end_line': b['end_line'],
            'template': b['parent'], 'template_kind': b['parent_kind'],
            'module_field': b['field'], 'module_type': b['module_type'], 'module_tag': b['module_tag'],
            'module_wrapper': b['wrapper'], 'state': b['owner'],
            'body_bytes': len(b['token_body']), 'body_sha256': _sha(b['token_body']),
            'module_at': f"{b['source']}:{b['file']}:{b['module_line']}",
            'begin_case': 'canonical' if b['begin_token'] == 'BeginScript' else 'other',
            'end_case': None if b['end_token'] is None else ('canonical' if b['end_token'] == 'EndScript' else 'other'),
            'after_begin_bytes': len(b['after_begin'].strip()),
            'after_begin_comment': int(b['after_begin'].strip()[:1] == ';') if b['after_begin'].strip() else 0,
            'after_end_bytes': len((b['after_end'] or '').strip()),
            'lines': b['lines'], 'lines_bytes': len(b['body']), 'lines_sha256': _sha(b['body']),
            'max_line_len': b['max_line_len'], 'max_line_no': b['max_line_no'],
            'eol_crlf': b['eol'].get('\r\n', 0), 'eol_lf': b['eol'].get('\n', 0), 'eol_cr': b['eol'].get('\r', 0),
            'eol_none': b['eol'].get('EOF', 0),
            'lua_end_lines': b['lua_end_lines'], 'zh_end_lines': b['zh_end_lines'], 'semicolon_lines': b['semicolon_lines'],
            'nested_begin': len(b['nested_begin']), 'endscript_substring_lines': len(b['endscript_substring_lines']),
            'closed': int(b['end_line'] is not None)})
    out['beginscript_blocks.tsv'] = (list(brows[0]) if brows else ['at'], brows)
    st = sx['sets'][key]
    lua = {k: v for k, v in st['lua'].items() if k not in ('after_begin_examples', 'after_end_examples', 'begin_spellings',
                                                           'end_spellings', 'gameplay_expected', 'body_eol', 'by_wrapper')}
    lua['by_wrapper'] = {('direct' if k == 'напрямую' else k): v for k, v in st['lua']['by_wrapper'].items()}
    lua['begin_case'] = {'canonical': st['lua']['begin_spellings'].get('BeginScript', 0),
                         'other': sum(v for k, v in st['lua']['begin_spellings'].items() if k != 'BeginScript')}
    lua['end_case'] = {'canonical': st['lua']['end_spellings'].get('EndScript', 0),
                       'other': sum(v for k, v in st['lua']['end_spellings'].items() if k != 'EndScript')}
    lua['body_eol'] = {{'\r\n': 'crlf', '\n': 'lf', '\r': 'cr'}.get(k, 'none'): v for k, v in st['lua']['body_eol'].items()}
    for f in ('all_ini_eol', 'files_with_lua_eol'):
        lua[f] = {('none' if k == 'без переводов строк' else k.lower()): v for k, v in st['lua'][f].items()}
    out['syntax.json'] = {'set': key, 'files': st['files'], 'lua': lua,
                          'extensions': {k: {'count': v['count'], 'example': v['example'], 'zh': v['zh']}
                                         for k, v in st['extensions'].items()}}
    return out


def _count(it):
    c = {}
    for x in it:
        c[x] = c.get(x, 0) + 1
    return dict(sorted(c.items(), key=lambda x: -x[1]))


def write(out, path):
    os.makedirs(path, exist_ok=True)
    for name, obj in out.items():
        p = os.path.join(path, name)
        if name.endswith('.tsv'):
            _tsv(p, *obj)
        else:
            _json(p, obj)


def generate(game, archives, path, label=None):
    out = build(game, archives, label)
    write(out, path)
    return out
