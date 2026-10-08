#!/usr/bin/env python3
"""Extract INI name sets the Generals Zero Hour C++ engine (TheSuperHackers/GeneralsGameCode) knows.

Ground truth for "what the Generals engine reads as-is":
  blocks  : theTypeTable[] in Core/GameEngine/Source/Common/INI/INI.cpp
  modules : addModule( X ) in GeneralsMD ModuleFactory.cpp + Core W3DModuleFactory.cpp
  fields  : every { "Name", parser, ... } entry inside FieldParse tables (Core + GeneralsMD, global set),
            plus per-table sets keyed by the table variable (class::s_fieldParseTable or local array name).
Field sets are global (not per block) - a field counted as "known to ZH" may still live in another block.
Usage: gen_zh_schema.py <GeneralsGameCode root> <out.json>
"""
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cslex import strip_comments, match_brace  # noqa: E402  (C-like enough)

TABLE_RE = re.compile(r'(?:static\s+)?(?:const\s+)?FieldParse\s+([\w:]+)\s*\[\s*\]\s*=\s*\{')
ENTRY_RE = re.compile(r'\{\s*"([^"]+)"\s*,')


def main():
    root, out = sys.argv[1], sys.argv[2]
    dirs = [os.path.join(root, 'Core'), os.path.join(root, 'GeneralsMD')]
    blocks, modules, fields, tables = [], [], set(), {}
    script = {'actions': set(), 'conditions': set()}
    for d in dirs:
        for dp, dn, fn in os.walk(d):
            for f in fn:
                if not f.endswith(('.cpp', '.h', '.inl')):
                    continue
                p = os.path.join(dp, f)
                txt = strip_comments(open(p, encoding='latin-1').read())
                rel = os.path.relpath(p, root)
                if rel.replace(os.sep, '/').endswith('Common/INI/INI.cpp'):
                    m = re.search(r'theTypeTable\[\]\s*=\s*\{', txt)
                    body = txt[m.end() - 1:match_brace(txt, m.end() - 1)]
                    blocks = ENTRY_RE.findall(body)
                if rel.replace(os.sep, '/').endswith('GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptEngine.cpp'):
                    # ScriptEngine::init(): curTemplate = &m_actionTemplates[ScriptAction::X]; curTemplate->m_internalName = "X";
                    for kind, name in re.findall(r'curTemplate\s*=\s*&m_(action|condition)Templates\[\s*(?:ScriptAction|Condition)::\w+\s*\];'
                                                 r'\s*curTemplate->m_internalName\s*=\s*"(\w+)"', txt):
                        script['actions' if kind == 'action' else 'conditions'].add(name)
                if f in ('ModuleFactory.cpp', 'W3DModuleFactory.cpp'):
                    modules += re.findall(r'\baddModule\s*\(\s*(\w+)\s*\)', txt)
                for m in TABLE_RE.finditer(txt):
                    b = m.end() - 1
                    e = match_brace(txt, b)
                    names = ENTRY_RE.findall(txt[b:e])
                    tables.setdefault(f'{rel}:{m.group(1)}', []).extend(names)
                    fields.update(names)
    try:
        rev = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True).strip()
    except Exception:
        rev = None
    js = {'source': 'TheSuperHackers/GeneralsGameCode (Core + GeneralsMD = Zero Hour)', 'revision': rev,
          'generator': 'tools/datacheck/schema/gen_zh_schema.py',
          'blocks': sorted(set(blocks)), 'modules': sorted(set(modules)), 'fields': sorted(fields),
          'tables': {k: sorted(set(v)) for k, v in sorted(tables.items())},
          'script_actions': sorted(script['actions']), 'script_conditions': sorted(script['conditions'])}
    with open(out, 'w') as f:
        json.dump(js, f, indent=1)
    print(f'ZH blocks {len(js["blocks"])}, modules {len(js["modules"])}, field names {len(fields)}, tables {len(tables)}, '
          f'script actions {len(js["script_actions"])}, conditions {len(js["script_conditions"])}')


if __name__ == '__main__':
    main()
