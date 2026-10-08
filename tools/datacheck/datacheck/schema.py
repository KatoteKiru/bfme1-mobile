"""Load generated schemas (OpenSAGE BFME/ZH tags + Generals ZH C++ name sets)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_DIR = os.path.join(os.path.dirname(HERE), 'schema')
LEVEL = {None: 0, 'CncGenerals': 0, 'CncGeneralsZeroHour': 1, 'Bfme': 2, 'Bfme2': 3, 'Bfme2Rotwk': 4,
         'Cnc3': 5, 'Cnc3KanesWrath': 6, 'Ra3': 7, 'Ra3Uprising': 8, 'Cnc4': 9}
BFME = 2
ZH = 1


class Schema:
    def __init__(self, opensage_path=None, zh_path=None):
        with open(opensage_path or os.path.join(SCHEMA_DIR, 'opensage_schema.json')) as f:
            self.os = json.load(f)
        zp = zh_path or os.path.join(SCHEMA_DIR, 'generals_zh_schema.json')
        self.zh = {'blocks': [], 'modules': [], 'fields': []}
        if os.path.exists(zp):
            with open(zp) as f:
                self.zh = json.load(f)
        self.top = self.os['top_level']
        self.tables = self.os['tables']
        self.modules = self.os['module_tables']
        self.zh_blocks = {b.lower() for b in self.zh['blocks']}
        self.zh_modules = {m.lower() for m in self.zh['modules']}
        self.zh_fields = {f.lower() for f in self.zh['fields']}
        # case-insensitive lookup helpers (SAGE compares field names case-insensitively)
        self._top_ci = {k.lower(): k for k in self.top}
        self._fields_ci = {tid: {k.lower(): k for k in t['fields']} for tid, t in self.tables.items()}
        self._mods_ci = {mt: {k.lower(): k for k in m} for mt, m in self.modules.items()}
        # names that are blocks in every table they appear in (used to keep depth inside unknown blocks)
        kinds = {}
        for t in self.tables.values():
            for k, v in t['fields'].items():
                kinds.setdefault(k.lower(), set()).add(v['kind'])
        for k, v in self.top.items():
            kinds.setdefault(k.lower(), set()).add(v['kind'])
        self.always_block = {k for k, s in kinds.items() if s and s <= {'block', 'module'}}
        self.value_names = {k for k, s in kinds.items() if 'value' in s}

    def top_block(self, name):
        k = self._top_ci.get(name.lower())
        return (k, self.top[k]) if k else (None, None)

    def field(self, tid, name):
        k = self._fields_ci.get(tid, {}).get(name.lower())
        return (k, self.tables[tid]['fields'][k]) if k else (None, None)

    def module(self, mt, name):
        k = self._mods_ci.get(mt, {}).get(name.lower())
        return (k, self.modules[mt][k]) if k else (None, None)

    @staticmethod
    def level(game):
        return LEVEL.get(game, 99)
