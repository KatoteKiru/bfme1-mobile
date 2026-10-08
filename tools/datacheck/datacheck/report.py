"""Build JSON + Markdown reports."""
import json
import os
from collections import Counter, defaultdict

from .patchdiff import is_patch_archive


def _ext(p):
    b = p.rsplit('\\', 1)[-1]
    return b.rsplit('.', 1)[-1].lower() if '.' in b else '(none)'


def _u(usage, n=None, fmtkey=lambda k: k):
    return [{'key': fmtkey(k), 'count': c, 'example': ex, **({'meta': m} if m else {})} for k, c, ex, m in usage.items(n)]


def build(vfs, schema, an, final, missing, pdiff, padded, cp, top, elapsed, game):
    by_ext = defaultdict(lambda: [0, 0])
    by_dir = Counter()
    overridden = 0
    over_by_patch = 0
    for k, vf in vfs.files.items():
        e = _ext(k)
        by_ext[e][0] += 1
        by_ext[e][1] += vf.size
        parts = k.split('\\')
        by_dir['\\'.join(parts[:2]) if len(parts) > 2 else parts[0]] += 1
        if vf.overridden:
            overridden += 1
            if is_patch_archive(vf.source):
                over_by_patch += 1
    arcs = [{'archive': a.label, 'magic': a.magic, 'entries': len(a.entries), 'warnings': a.warnings,
             'patch': is_patch_archive(a.label)} for a in vfs.archives]
    loose = sum(1 for v in vfs.files.values() if v.entry is None)
    fmtf = lambda k: f'{k[0]}.{k[1]}'
    decl_mod = Counter()
    for (mt, name), c in an.modules.count.items():
        decl_mod[name] += c
    for (fld, name), c in an.unknown_modules.count.items():
        decl_mod[name + ' (unknown)'] += c
    from .objects import module_frequency
    fin_c, fin_o, fin_ex = module_frequency(final)
    kinds_bfme = Counter(k[0] for k in an.bfme_only.count)
    kinds_nozh = Counter(k[0] for k in an.not_in_zh_cpp.count)
    tot_changes = Counter(ch['op'] for f in pdiff for ch in f['changes'])
    top_ops = Counter(ch['op'] for f in pdiff for ch in f['changes']
                      if ch['field'] == '<block>' and ' / ' not in ch['key'])
    blocks_touched = len({ch['key'].split(' / ')[0] for f in pdiff for ch in f['changes'] if f['path'] != '#define'})
    moved = sum(len(f.get('moved_in', [])) for f in pdiff)
    ini_over = sum(1 for k, vf in vfs.files.items() if k.endswith('.ini') and is_patch_archive(vf.source)
                   and any(not is_patch_archive(o[0]) for o in vf.overridden))
    defines_changed = Counter(ch['op'] for f in pdiff if f['path'] == '#define' for ch in f['changes'])
    rep = {
        'game_dir': game, 'elapsed_s': round(elapsed, 1),
        'schema': {'opensage_revision': schema.os.get('revision'), 'zh_revision': schema.zh.get('revision'),
                   'opensage_top_level': len(schema.top), 'opensage_tables': len(schema.tables),
                   'opensage_modules': sum(len(m) for m in schema.modules.values()),
                   'zh_blocks': len(schema.zh['blocks']), 'zh_modules': len(schema.zh['modules']),
                   'zh_field_names': len(schema.zh['fields'])},
        'vfs': {'archives_count': len(vfs.archives), 'archives_mount_order': arcs,
                'archive_errors': vfs.archive_errors, 'loose_files': loose,
                'files_total': len(vfs.files), 'files_overridden': overridden,
                'files_overridden_by_patch': over_by_patch, 'duplicate_entries_inside_archive': vfs.duplicates_in_archive,
                'by_extension': {e: {'files': v[0], 'bytes': v[1]} for e, v in sorted(by_ext.items(), key=lambda x: -x[1][0])},
                'by_dir': dict(by_dir.most_common())},
        'ini': {'files': len(an.files), 'files_with_warnings': sum(1 for f in an.files.values() if f['warnings']),
                'lines': an.n_lines, 'top_level_blocks': an.n_blocks, 'nested_blocks': an.n_nested,
                'field_lines': an.n_field_lines, 'lines_inside_unknown_blocks': an.n_unknown_ctx_lines,
                'defines': len(an.defines), 'embedded_scripts': an.n_scripts,
                'structure_warnings': len(an.warnings), 'warnings': [{'where': w, 'msg': m} for w, m in an.warnings[:500]],
                'top_level_by_type': dict(an.top_blocks.count.most_common()),
                'unknown_blocks_total': sum(an.unknown_blocks.count.values()),
                'unknown_fields_total': sum(an.unknown_fields.count.values()),
                'unknown_modules_total': sum(an.unknown_modules.count.values()),
                'unknown_blocks': _u(an.unknown_blocks),
                'unknown_fields': _u(an.unknown_fields, fmtkey=fmtf),
                'unknown_fields_as_blocks': _u(an.unknown_nested, fmtkey=fmtf),
                'unknown_modules': _u(an.unknown_modules, fmtkey=lambda k: f'{k[0]} = {k[1]}'),
                'modules_declared': dict(decl_mod.most_common()),
                'objects': {'definitions': len(an.objects), 'resolved': len(final),
                            'missing_parents': dict(missing)},
                'modules_final_after_inheritance': [{'module': f'{k[0]} = {k[1]}', 'objects': c, 'instances': fin_c[k],
                                                     'example': fin_ex[k]} for k, c in fin_o.most_common()]},
        'bfme_vs_zh': {
            'opensage_bfme_only_used': {'distinct_by_kind': dict(kinds_bfme), 'uses_total': sum(an.bfme_only.count.values()),
                                        'items': _u(an.bfme_only, fmtkey=lambda k: f'{k[0]}: {k[1]}')},
            'not_in_generals_zh_cpp': {'distinct_by_kind': dict(kinds_nozh), 'uses_total': sum(an.not_in_zh_cpp.count.values()),
                                       'items': _u(an.not_in_zh_cpp, fmtkey=lambda k: f'{k[0]}: {k[1]}')},
            'tagged_later_than_bfme_in_opensage': _u(an.later_game, fmtkey=lambda k: f'{k[0]}: {k[1]}')},
        'patch': {'patch_archives': [a['archive'] for a in arcs if a['patch']],
                  'ini_overridden_by_patch': ini_over, 'ini_added_by_patch': padded,
                  'changes_total': dict(tot_changes), 'top_level_blocks': dict(top_ops),
                  'top_level_blocks_touched': blocks_touched, 'top_level_blocks_moved': moved,
                  'defines': dict(defines_changed), 'files_with_changes': len(pdiff),
                  'files': [{k: v for k, v in f.items() if k != 'changes'} | {'changes_count': len(f['changes'])} for f in pdiff],
                  'command_point_sets': cp},
    }
    rep['_patch_full'] = pdiff
    return rep


def _md_table(rows, headers):
    out = ['| ' + ' | '.join(headers) + ' |', '|' + '---|' * len(headers)]
    for r in rows:
        out.append('| ' + ' | '.join(str(x).replace('|', '\\|') for x in r) + ' |')
    return '\n'.join(out)


def write(rep, out, top, extra_md=None, tail_md=None):
    full = rep.pop('_patch_full')
    with open(os.path.join(out, 'report.json'), 'w') as f:
        json.dump(rep, f, indent=1, ensure_ascii=False)
    with open(os.path.join(out, 'patch222_diff.json'), 'w') as f:
        json.dump(full, f, indent=1, ensure_ascii=False)
    v, i, b, p = rep['vfs'], rep['ini'], rep['bfme_vs_zh'], rep['patch']
    L = [f"# datacheck: отчёт по данным BFME1", '',
         f"Данные: `{rep['game_dir']}`, время прогона {rep['elapsed_s']} с. Схемы: OpenSAGE `{(rep['schema']['opensage_revision'] or '')[:10]}` "
         f"({rep['schema']['opensage_top_level']} блоков верхнего уровня, {rep['schema']['opensage_tables']} таблиц, {rep['schema']['opensage_modules']} модулей), "
         f"Generals ZH C++ `{(rep['schema']['zh_revision'] or '')[:10]}` ({rep['schema']['zh_blocks']} блоков, {rep['schema']['zh_modules']} модулей, {rep['schema']['zh_field_names']} имён полей).", '',
         '## 1. Архивы и файлы', '',
         f"Архивов: **{v['archives_count']}**, файлов в итоговом дереве: **{v['files_total']}** (loose: {v['loose_files']}), "
         f"перекрыто другими версиями: {v['files_overridden']}, из них итоговая версия из патча: **{v['files_overridden_by_patch']}**, "
         f"дубликатов внутри одного архива: {v['duplicate_entries_inside_archive']}.", '',
         'Порядок монтирования (первый выигрывает, как в движке SAGE):', '',
         _md_table([(n + 1, a['archive'], a['magic'], a['entries'], 'да' if a['patch'] else '', '; '.join(a['warnings'][:2]))
                    for n, a in enumerate(v['archives_mount_order'])], ['#', 'архив', 'magic', 'файлов', 'патч', 'предупреждения']), '']
    if v['archive_errors']:
        L += ['Ошибки чтения архивов: ' + '; '.join(f'{a}: {e}' for a, e in v['archive_errors']), '']
    L += ['По типам файлов:', '', _md_table([(e, d['files'], f"{d['bytes'] / 1048576:.1f}") for e, d in v['by_extension'].items()],
                                          ['тип', 'файлов', 'МБ']), '',
          '## 2. INI', '',
          f"INI-файлов: **{i['files']}**, строк {i['lines']}, блоков верхнего уровня **{i['top_level_blocks']}**, вложенных блоков/модулей {i['nested_blocks']}, "
          f"строк-полей {i['field_lines']}, #define {i['defines']}, встроенных скриптов BeginScript {i['embedded_scripts']}. "
          f"Структурных предупреждений: **{i['structure_warnings']}** (файлов с предупреждениями: {i['files_with_warnings']}); "
          f"строк внутри неизвестных блоков: {i['lines_inside_unknown_blocks']}.", '',
          'Блоки верхнего уровня по типам:', '',
          _md_table(list(i['top_level_by_type'].items()), ['блок', 'кол-во']), '']
    if i['warnings']:
        L += ['Структурные предупреждения (первые 30):', ''] + [f"- `{w['where']}` {w['msg']}" for w in i['warnings'][:30]] + ['']
    for title, key in (('Неизвестные блоки верхнего уровня (нет в OpenSAGE)', 'unknown_blocks'),
                       ('Неизвестные поля (класс OpenSAGE.поле)', 'unknown_fields'),
                       ('Неизвестные модули', 'unknown_modules')):
        rows = [(x['key'], x['count'], f"`{x['example']}`") for x in i[key][:top]]
        L += [f'### {title}: всего использований {sum(x["count"] for x in i[key])}, различных {len(i[key])}', '',
              _md_table(rows, ['имя', 'раз', 'пример']) if rows else '_нет_', '']
    L += [f"### Модули: объявления в INI и итог после наследования",
          '', f"Объектов: {i['objects']['definitions']} определений, {i['objects']['resolved']} итоговых (data\\ini). "
          f"Родитель не найден: {i['objects']['missing_parents'] or 'нет'}.", '',
          _md_table([(x['module'], x['objects'], x['instances'], i['modules_declared'].get(x['module'].split(' = ', 1)[1], 0), f"`{x['example']}`")
                     for x in i['modules_final_after_inheritance'][:top]],
                    ['модуль', 'объектов (итог)', 'экземпляров (итог)', 'объявлений в INI', 'пример']), '',
          '## 3. BFME против Generals/ZH', '',
          f"Сущности, помеченные в OpenSAGE `AddedIn(SageGame.Bfme)` и реально используемые: различных {sum(b['opensage_bfme_only_used']['distinct_by_kind'].values())} "
          f"({b['opensage_bfme_only_used']['distinct_by_kind']}), использований {b['opensage_bfme_only_used']['uses_total']}.", '',
          f"Имена, которых нет в C++ движке Generals ZH (GeneralsGameCode: theTypeTable / addModule / FieldParse): различных "
          f"{sum(b['not_in_generals_zh_cpp']['distinct_by_kind'].values())} ({b['not_in_generals_zh_cpp']['distinct_by_kind']}), "
          f"использований {b['not_in_generals_zh_cpp']['uses_total']}. Это нижняя оценка того, что движку Generals придётся дописать.", '',
          f'Топ-{top} отсутствующих в ZH C++:', '',
          _md_table([(x['key'], x['count'], f"`{x['example']}`") for x in b['not_in_generals_zh_cpp']['items'][:top]], ['сущность', 'раз', 'пример']), '',
          f'Топ-{top} BFME-only по OpenSAGE:', '',
          _md_table([(x['key'], x['count'], f"`{x['example']}`") for x in b['opensage_bfme_only_used']['items'][:top]], ['сущность', 'раз', 'пример']), '',
          '## 4. Что патч 2.22 меняет поверх базы', '',
          f"Архивы патча: {', '.join(p['patch_archives']) or '**пока нет в gamedata/**'}. INI, перекрытых патчем: {p['ini_overridden_by_patch']}, "
          f"новых INI из патча: {len(p['ini_added_by_patch'])}. Записей диффа (поля и блоки): {p['changes_total'] or 'нет'}; "
          f"блоков верхнего уровня: добавлено {p['top_level_blocks'].get('added', 0)}, удалено {p['top_level_blocks'].get('removed', 0)}, "
          f"затронуто изменениями {p['top_level_blocks_touched']}, просто перенесено в другой файл {p['top_level_blocks_moved']}; "
          f"#define: {p['defines'] or 'нет'}. Дифф глобальный по блокам (перенос блока между файлами не считается удалением), "
          f"значения сравниваются после #define и нормализации. Полный дифф: `patch222_diff.md` / `patch222_diff.json`.", '',
          'Наборы командных очков из `gamedata.ini` (все версии; «блок» — ближайшая строка `;;;` выше). '
          'Подпись блока берётся из данных (см. колонку «блок»). Текст после `;` — комментарий.', '',
          _md_table([(h['set'], h.get('good', '—'), h.get('evil', '—'), h['caption'] or '—', h['comment'] or '—',
                      f"`{h['archive']}:{h['file']}:{h.get('good_line', '?')}–{h.get('evil_line', '?')}`" +
                      (f" (блок :{h['caption_line']})" if h['caption_line'] else ''), 'итог' if h['winning'] else 'перекрыт')
                     for h in p['command_point_sets']], ['набор', 'Good', 'Evil', 'блок', 'комментарий', 'где', 'статус']), '']
    L += list(extra_md or [])
    L += ['## 6. Для геймплея', '', 'Механики с файл:строка (продажа, стойки, пополнение орд, доход ферм), достижимость в скирмише '
          'по фракциям и сверка с gp-scratch: `gameplay.md` / `gameplay.json`.', '']
    L += list(tail_md or [])
    with open(os.path.join(out, 'report.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')
        f.write(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'LIMITATIONS.md')).read()
                if os.path.exists(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'LIMITATIONS.md')) else '')
    D = ['# Патч 2.22 поверх базы: дифф значений INI', '']
    for fdiff in full:
        D += [f"## {fdiff['path']}", f"база `{fdiff['base']}` → итог `{fdiff['final']}`; изменений {len(fdiff['changes'])}"
              + (f"; перенесено сюда блоков: {len(fdiff.get('moved_in', []))}" if fdiff.get('moved_in') else ''), '']
        D.append(_md_table([(c['op'], c['key'], c['base'] if c['base'] is not None else '', c['final'] if c['final'] is not None else '',
                             c['base_at'] or '', c['final_at'] or '') for c in fdiff['changes']],
                           ['op', 'блок / поле', 'база', 'итог', 'база: где', 'итог: где']))
        D.append('')
    if not full:
        D.append('_Патч-архивов с перекрытыми INI пока нет._')
    with open(os.path.join(out, 'patch222_diff.md'), 'w') as f:
        f.write('\n'.join(D) + '\n')
