"""Unit tests on synthetic data (no EA files needed). Run: cd tools/datacheck && python3 -m unittest -v"""
import json
import locale
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from datacheck.big import BigArchive, write_big  # noqa: E402
from datacheck.vfs import VFS  # noqa: E402
from datacheck.schema import Schema  # noqa: E402
from datacheck.ini import IniAnalyzer  # noqa: E402
from datacheck.objects import resolve, module_frequency  # noqa: E402
from datacheck.patchdiff import patch_diffs, find_field_everywhere  # noqa: E402
from datacheck.values import normalize, parse_number  # noqa: E402

BASE_GAMEDATA = b"""GameData
  GoodCommandPointLimit\t\t= 111
  EvilCommandPointLimit\t\t= 222
  MaxCameraHeight = 123.0
End
"""
PATCH_GAMEDATA = b"""GameData
  GoodCommandPointLimit = 333
  EvilCommandPointLimit = 444
  MaxCameraHeight = 123
End
"""
OBJECT_INI = b"""#define MY_HEALTH 400
Object TestParent
  KindOf = INFANTRY
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    OkToChangeModelColor = Yes
    ModelConditionState = NONE
      Model = ABC_SKN
    End
    AnimationState = MOVING
      Animation = Walk
        AnimationName = ABC_SKL.ABC_RUNA
        AnimationMode = LOOP
      End
      BeginScript
        Prev = CurDrawablePrevAnimationState()
        if Prev == "X" then CurDrawableSetTransitionAnimState("Y") end
      EndScript
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = MY_HEALTH
  End
  Behavior = PhysicsBehavior ModuleTag_Phys
    GravityMult = 1.0
  End
  TotallyUnknownField = 5
  UnknownNestedThing = Foo
    Inner = 1
  End
End

ChildObject TestChild TestParent
  Body = ActiveBody ModuleTag_Body2
    MaxHealth = 500
  End
  RemoveModule ModuleTag_Phys
End

MysteryBlock Foo
  A = 1
End
"""


class TestBig(unittest.TestCase):
    def test_roundtrip_and_header(self):
        d = tempfile.mkdtemp()
        try:
            for magic in (b'BIGF', b'BIG4'):
                p = os.path.join(d, 'a.big')
                write_big(p, [('data\\ini\\a.ini', b'hello'), ('art\\x.w3d', b'\0' * 10)], magic=magic)
                a = BigArchive(p)
                self.assertEqual(a.magic, magic.decode())
                self.assertEqual([e.name for e in a.entries], ['data\\ini\\a.ini', 'art\\x.w3d'])
                self.assertEqual(a.entries[0].read(), b'hello')
                self.assertEqual(a.declared_size, os.path.getsize(p))   # little-endian size field
                self.assertEqual(a.warnings, [])
        finally:
            shutil.rmtree(d)


class TestVfsPriority(unittest.TestCase):
    def test_first_archive_wins_and_loose_beats_all(self):
        d = tempfile.mkdtemp()
        try:
            write_big(os.path.join(d, 'INI.big'), [('data\\ini\\gamedata.ini', b'base'), ('data\\ini\\only.ini', b'o')])
            write_big(os.path.join(d, '_patch222.big'), [('Data\\INI\\GameData.ini', b'patch')])
            write_big(os.path.join(d, '!mod.big'), [('data\\ini\\only.ini', b'mod')])
            os.makedirs(os.path.join(d, 'Data', 'INI'))
            with open(os.path.join(d, 'Data', 'INI', 'loose.ini'), 'wb') as f:
                f.write(b'loose')
            v = VFS(d)
            self.assertEqual([a.label for a in v.archives], ['!mod.big', '_patch222.big', 'INI.big'])
            g = v.get('data/ini/gamedata.ini')
            self.assertEqual((g.source, v.read('data\\ini\\gamedata.ini')), ('_patch222.big', b'patch'))
            self.assertEqual([o[0] for o in g.overridden], ['INI.big'])
            self.assertEqual(v.read('data\\ini\\only.ini'), b'mod')
            self.assertEqual(v.get('data\\ini\\loose.ini').source, '<loose>')
        finally:
            shutil.rmtree(d)


class TestIni(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = Schema()

    def parse(self, text):
        a = IniAnalyzer(self.schema)
        a.collect_defines('t.ini', text.decode())
        recs = a.parse_text('t.ini', text.decode(), records=[])
        return a, recs

    def test_structure_and_unknowns(self):
        a, recs = self.parse(OBJECT_INI)
        self.assertEqual(a.warnings, [])
        self.assertEqual(a.n_blocks, 3)
        self.assertEqual(a.n_scripts, 1)
        self.assertIn('MysteryBlock', a.unknown_blocks.count)
        self.assertIn(('ObjectDefinition', 'TotallyUnknownField'), a.unknown_fields.count)
        self.assertIn(('ObjectDefinition', 'UnknownNestedThing'), a.unknown_nested.count)
        self.assertEqual(a.unknown_modules.count, {})
        mh = [r for r in recs if r[1] == 'MaxHealth']
        self.assertEqual(mh[0][3], '400')          # #define resolved
        self.assertEqual(mh[0][4], 't.ini:21')

    def test_child_object_inheritance(self):
        a, _ = self.parse(OBJECT_INI)
        final, missing = resolve(a.objects)
        self.assertEqual(missing, {})
        child = final['TestChild']
        types = sorted(m['type'] for m in child.values())
        # parent Body (kind Body) replaced by child's Body, PhysicsBehavior removed by RemoveModule, Draw inherited
        self.assertEqual(types, ['ActiveBody', 'W3DScriptedModelDraw'])
        self.assertIn('moduletag_body2', child)
        inst, objs, _ = module_frequency(final)
        self.assertEqual(objs[('Body', 'ActiveBody')], 2)

    def test_patch_diff_and_command_points(self):
        d = tempfile.mkdtemp()
        try:
            write_big(os.path.join(d, 'ini.big'), [('data\\ini\\gamedata.ini', BASE_GAMEDATA)])
            write_big(os.path.join(d, '_patch222.big'), [('data\\ini\\gamedata.ini', PATCH_GAMEDATA)])
            v = VFS(d)
            files, added = patch_diffs(self.schema, v, {})
            self.assertEqual(len(files), 1)
            ch = {c['field']: c for c in files[0]['changes']}
            self.assertEqual(ch['GoodCommandPointLimit']['base'], '111')
            self.assertEqual(ch['GoodCommandPointLimit']['final'], '333')
            self.assertEqual(ch['GoodCommandPointLimit']['base_at'], 'ini.big:data\\ini\\gamedata.ini:2')
            self.assertEqual(ch['EvilCommandPointLimit']['final_at'], '_patch222.big:data\\ini\\gamedata.ini:3')
            self.assertNotIn('MaxCameraHeight', ch)    # 123.0 == 123 after normalisation
            hits = find_field_everywhere(v, ['GoodCommandPointLimit'])
            self.assertEqual(sorted((h['archive'], h['value'], h['winning']) for h in hits),
                             [('_patch222.big', '333', True), ('ini.big', '111', False)])
        finally:
            shutil.rmtree(d)


LOCALE_DIR = os.path.join(os.path.dirname(HERE), '.locale')


def ensure_ru_locale():
    """Make ru_RU.UTF-8 usable without root: if the system lacks it but glibc locale sources are present
    (Debian package `locales`), compile it into tools/datacheck/.locale and point LOCPATH there."""
    for cand in ('ru_RU.UTF-8', 'ru_RU.utf8'):
        try:
            locale.setlocale(locale.LC_ALL, cand)
            locale.setlocale(locale.LC_ALL, 'C')
            return True
        except locale.Error:
            pass
    target = os.path.join(LOCALE_DIR, 'ru_RU.UTF-8')
    if not os.path.isdir(target):
        if not (shutil.which('localedef') and os.path.exists('/usr/share/i18n/locales/ru_RU')):
            return False
        os.makedirs(LOCALE_DIR, exist_ok=True)
        subprocess.run(['localedef', '-i', 'ru_RU', '-f', 'UTF-8', target], capture_output=True)
    if not os.path.isdir(target):
        return False
    os.environ['LOCPATH'] = LOCALE_DIR
    try:
        locale.setlocale(locale.LC_ALL, 'ru_RU.UTF-8')
        locale.setlocale(locale.LC_ALL, 'C')
        return True
    except locale.Error:
        return False


class TestValues(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ru = ensure_ru_locale()

    def test_normalize(self):
        self.assertEqual(normalize('50%'), '0.5')
        self.assertEqual(normalize('X:1.50 Y:-0.0 Z:2'), 'x:1.5 y:0 z:2')
        self.assertEqual(normalize('1.5s'), '1500')
        self.assertEqual(normalize('YES'), 'yes')
        self.assertEqual(parse_number('1,5'), None)   # SAGE never uses comma decimals
        self.assertEqual(normalize('1e40 1E+3 123456789012345678901234567890.0'),
                         '10000000000000000000000000000000000000000 1000 123456789012345678901234567890')

    def test_locale_independent(self):
        """Same results under a comma-decimal locale (ru_RU) if it is installed or can be generated."""
        loc = None
        for cand in ('ru_RU.UTF-8', 'ru_RU.utf8'):
            try:
                locale.setlocale(locale.LC_ALL, cand)
                loc = cand
                break
            except locale.Error:
                pass
        if loc is None:
            self.skipTest('ru_RU locale not available on this box (see README: LOCPATH test)')
        try:
            self.assertEqual(locale.localeconv()['decimal_point'], ',')
            self.assertEqual(normalize('12.50 50% X:0.25'), '12.5 0.5 x:0.25')
        finally:
            locale.setlocale(locale.LC_ALL, 'C')

    def test_locale_subprocess(self):
        """Values under LC_ALL=ru_RU.UTF-8 in a subprocess (uses LOCPATH from env if set)."""
        env = dict(os.environ, LC_ALL='ru_RU.UTF-8')
        code = ("import locale,sys;sys.path.insert(0,%r);"
                "locale.setlocale(locale.LC_ALL,'');"
                "from datacheck.values import normalize;"
                "print(locale.localeconv()['decimal_point'], normalize('12.50 50%%'))") % os.path.dirname(HERE)
        r = subprocess.run([sys.executable, '-c', code], env=env, capture_output=True, text=True)
        if r.returncode != 0:
            self.skipTest('ru_RU locale not available: ' + r.stderr.strip().splitlines()[-1])
        self.assertEqual(r.stdout.split(), [',', '12.5', '0.5'])



RECRUIT_INI = """PlayerTemplate FactionTest
  Side = Test
  PlayableSide = Yes
  BuildableHeroesMP = TestHero
End
CommandButton Command_BuildBarracks
  Command = FOUNDATION_CONSTRUCT
  Object = TestBarracks
End
CommandButton Command_TrainHorde
  Command = UNIT_BUILD
  Object = TestHorde
End
CommandButton Command_Sell
  Command = SELL
End
CommandSet PlotCommandSet
  1 = Command_BuildBarracks
End
CommandSet BarracksCommandSet
  1 = Command_TrainHorde
  2 = Command_Sell
End
Object TestPlot
  Side = Test
  KindOf = STRUCTURE
  CommandSet = PlotCommandSet
End
Object TestBarracksBase
  Side = Test
  KindOf = STRUCTURE
  CommandSet = BarracksCommandSet
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 10
  End
End
ChildObject TestBarracks TestBarracksBase
  Behavior = AutoDepositUpdate ModuleTag_Money
    DepositAmount = 5
  End
End
Object TestHorde
  Side = Test
  KindOf = HORDE
  Behavior = HordeContain ModuleTag_Contain
    InitialPayload = TestSoldier 5
  End
End
Object TestSoldier
  Side = Test
  KindOf = INFANTRY
End
Object TestHero
  Side = Test
  KindOf = HERO
End
Object EnemyThing
  Side = Other
  KindOf = INFANTRY
End
"""


class TestGameplay(unittest.TestCase):
    def test_recruit_closure_and_sell(self):
        from datacheck import recruit, gameplay
        a = IniAnalyzer(Schema())
        path = 'data\\ini\\t.ini'
        a.collect_defines(path, RECRUIT_INI)
        recs = a.parse_text(path, RECRUIT_INI, records=[])
        final, missing = resolve(a.objects)
        out = recruit.build(recs, final, a.objects)['factiontest']
        self.assertEqual(sorted(out['objects']),
                         ['TestBarracks', 'TestHero', 'TestHorde', 'TestPlot', 'TestSoldier'])
        self.assertEqual(out['objects']['TestBarracks']['via'], 'Command_BuildBarracks')   # CommandSet inherited
        self.assertEqual(out['objects']['TestSoldier']['via'], 'TestHorde')                # HordeContain InitialPayload
        self.assertEqual(out['by_class']['hero'], 1)
        sells = gameplay.sell_holders(recs, final, a.objects)
        self.assertEqual(sells, {'structure': ['TestBarracks', 'TestBarracksBase']})


class TestRuLocaleFullParse(unittest.TestCase):
    def test_parser_same_under_ru_locale(self):
        """INI parse + inheritance + normalisation give identical output under C and ru_RU.UTF-8."""
        if not ensure_ru_locale():
            self.skipTest('ru_RU locale not available and cannot be generated (install Debian package `locales`)')
        code = ("import locale,sys,json;sys.path.insert(0,%r);sys.path.insert(0,%r);"
                "locale.setlocale(locale.LC_ALL,'');"
                "from test_datacheck import OBJECT_INI;"
                "from datacheck.schema import Schema;from datacheck.ini import IniAnalyzer;"
                "from datacheck.objects import resolve;from datacheck.values import normalize;"
                "a=IniAnalyzer(Schema());t=OBJECT_INI.decode();a.collect_defines('t.ini',t);"
                "r=a.parse_text('t.ini',t,records=[]);f,m=resolve(a.objects);"
                "print(json.dumps([locale.localeconv()['decimal_point'],[[str(x) for x in q[:4]]+[normalize(q[3])] for q in r],"
                "{k:sorted(v) for k,v in f.items()}]))") % (os.path.dirname(HERE), HERE)
        outs = []
        for lc in ('C', 'ru_RU.UTF-8'):
            r = subprocess.run([sys.executable, '-c', code], env=dict(os.environ, LC_ALL=lc), capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            outs.append(json.loads(r.stdout))
        self.assertEqual(outs[0][0], '.')
        self.assertEqual(outs[1][0], ',')
        self.assertEqual(outs[0][1:], outs[1][1:])



class TestPatchMoves(unittest.TestCase):
    def test_moved_block_is_not_removed(self):
        d = tempfile.mkdtemp()
        try:
            base_a = b"#define HP 100\nObject Foo\n  Body = ActiveBody ModuleTag_B\n    MaxHealth = HP\n  End\nEnd\nObject Gone\nEnd\n"
            patch_a = b"; emptied by the patch\n"
            patch_b = b"#define HP 120\nObject Foo\n  Body = ActiveBody ModuleTag_B\n    MaxHealth = HP\n  End\nEnd\n"
            write_big(os.path.join(d, 'ini.big'), [('data\\ini\\object\\a.ini', base_a)])
            write_big(os.path.join(d, '_patch222.big'), [('data\\ini\\object\\a.ini', patch_a),
                                                        ('data\\ini\\object\\b.ini', patch_b)])
            v = VFS(d)
            files, added = patch_diffs(Schema(), v, {})
            self.assertEqual([x['path'] for x in added], ['data\\ini\\object\\b.ini'])
            by = {f['path'].lower(): f for f in files}
            b = by['data\\ini\\object\\b.ini']
            self.assertEqual([m['block'] for m in b['moved_in']], ['Object foo'])
            ch = [(c['op'], c['field'], c['base_resolved'], c['final_resolved']) for c in b['changes']]
            self.assertEqual(ch, [('changed', 'MaxHealth', '100', '120')])     # value via #define, base defines used
            a = by['data\\ini\\object\\a.ini']
            self.assertEqual([(c['op'], c['key']) for c in a['changes']], [('removed', 'Object gone')])
            dfn = by['#define']['changes']
            self.assertEqual([(c['key'], c['base'], c['final']) for c in dfn], [('#define HP', '100', '120')])
        finally:
            shutil.rmtree(d)



LUA_INI = (b"Object Foo\r\n"
           b"  Draw = W3DScriptedModelDraw ModuleTag_01\r\n"
           b"    AnimationState = MOVING\r\n"
           b"      BeginScript ; comment after token\r\n"
           b"\tif CurDrawableModelcondition(\"MOVING\") then  \r\n"
           b"\t\tCurDrawablePlaySound(\"x;y\") -- lua comment, \xe9 byte\r\n"
           b"\t\tprint(\"EndScript inside a string\")\n"
           b"\tend\r\n"
           b"      EndScript\r\n"
           b"    End\r\n"
           b"  End\r\n"
           b"End\r\n")
LUA_BODY = (b"\tif CurDrawableModelcondition(\"MOVING\") then  \r\n"
            b"\t\tCurDrawablePlaySound(\"x;y\") -- lua comment, \xe9 byte\r\n"
            b"\t\tprint(\"EndScript inside a string\")\n"
            b"\tend\r\n")
LUA_PATCH = (b"ChildObject Bar Foo\r\n"
             b"  ReplaceModule ModuleTag_01\r\n"
             b"    Draw = W3DScriptedModelDraw ModuleTag_02\r\n"
             b"      IdleAnimationState\r\n"
             b"        BeginScript\r\n"
             b"          CurDrawableHideSubObject(\"AXE\")\r\n"
             b"        endscript   ; tail\r\n"
             b"      End\r\n"
             b"    End\r\n"
             b"  End\r\n"
             b"End\r\n")


class TestLua(unittest.TestCase):
    def test_raw_body_is_byte_exact(self):
        from datacheck import syntax
        bl = syntax.lua_blocks(LUA_INI)
        self.assertEqual(len(bl), 1)
        b = bl[0]
        self.assertEqual(b['body'], LUA_BODY)                       # byte for byte, CRLF/LF/tabs/0xE9 kept
        self.assertEqual((b['line'], b['end_line']), (4, 9))
        self.assertEqual(b['after_begin'].strip(), '; comment after token')
        self.assertEqual(b['after_end'], '')
        self.assertEqual(b['lua_end_lines'], 1)                     # Lua "end" = ZH End token
        self.assertEqual(b['semicolon_lines'], 1)
        self.assertEqual(b['endscript_substring_lines'], [7])       # not a terminator: not the first token
        self.assertEqual(dict(b['eol']), {'\r\n': 3, '\n': 1})
        # the body is a contiguous slice of the file
        start = LUA_INI.index(b'\tif')
        self.assertEqual(LUA_INI[start:start + len(b['body'])], b['body'])
        tb = LUA_INI[LUA_INI.index(b'BeginScript') + len(b'BeginScript'):LUA_INI.index(b'EndScript\r\n    End')]
        self.assertEqual(b['token_body'], tb)
        self.assertTrue(tb.startswith(b' ; comment after token\r\n') and tb.endswith(b'\tend\r\n      '))
        self.assertEqual((b['lua_end_lines'], b['zh_end_lines']), (1, 1))
        self.assertEqual([syntax.zh_first_token(x) for x in (b'  end) -- x', b'\tEND=1', b' ; end', b'end;x')],
                         [b'end)', b'END', None, b'end'])
        eof = syntax.lua_blocks(b"Object A\n BeginScript\n x = 1\n BeginScript\n")
        self.assertIsNone(eof[0]['end_line'])
        self.assertEqual(eof[0]['nested_begin'], [4])
        self.assertEqual(eof[0]['body'], b" x = 1\n BeginScript\n")

    def test_base_vs_patch_sets_and_tsv(self):
        from datacheck import syntax
        with tempfile.TemporaryDirectory() as d:
            write_big(os.path.join(d, 'ini.big'), [('data\\ini\\a.ini', LUA_INI)])
            write_big(os.path.join(d, '_patch222.big'), [('data\\ini\\a.ini', LUA_INI), ('data\\ini\\b.ini', LUA_PATCH)])
            v = VFS(d, include_loose=False)
            sx = syntax.build(v, Schema(), gp_expected=1)
            self.assertEqual(set(sx['sets']), {'base', '2.22'})
            self.assertEqual(sx['sets']['base']['lua']['blocks'], 1)
            lua = sx['sets']['2.22']['lua']
            self.assertEqual(lua['blocks'], 2)
            self.assertEqual(lua['direct_in_module'], 1)
            self.assertEqual(lua['by_wrapper'], {'напрямую': 1, 'ReplaceModule': 1})
            self.assertEqual(lua['end_spellings'], {'EndScript': 1, 'endscript': 1})
            self.assertEqual(lua['after_end_nonempty'], 1)
            self.assertEqual(lua['outside_module'], [])
            bb = {b['file']: b for b in sx['blocks']['2.22']}
            self.assertEqual((bb['data\\ini\\b.ini']['module_type'], bb['data\\ini\\b.ini']['module_tag'],
                              bb['data\\ini\\b.ini']['parent'], bb['data\\ini\\b.ini']['owner']),
                             ('W3DScriptedModelDraw', 'ModuleTag_02', 'Bar', 'IdleAnimationState'))
            self.assertEqual(bb['data\\ini\\a.ini']['body'], LUA_BODY)
            out = os.path.join(d, 'out')
            os.makedirs(out)
            syntax.write(sx, out)
            with open(os.path.join(out, 'beginscript.tsv'), encoding='utf-8') as f:
                rows = f.read().splitlines()
            self.assertEqual(len(rows), 1 + 3)
            self.assertIn('2.22\tdraw\tW3DScriptedModelDraw\tModuleTag_02\tReplaceModule\tChildObject\tBar\t'
                          '_patch222.big:data\\ini\\b.ini:3\t5\t1', rows)


class TestCommandPointSets(unittest.TestCase):
    def test_captions_and_lines(self):
        from datacheck.patchdiff import command_point_sets
        g = (b"GameData\r\n  ;;; TEST CAPS\r\n  GoodCommandPointLimit = 11\r\n  EvilCommandPointLimit = 22\r\n"
             b"  ;;; TEST MP POINTS ;;;\r\n  GoodCommandPointsMP2 = 33 ; 44 ; note\r\n"
             b"  EvilCommandPointsMP2 = 55\r\nEnd\r\n")
        with tempfile.TemporaryDirectory() as d:
            write_big(os.path.join(d, 'ini.big'), [('data\\ini\\gamedata.ini', g)])
            sets = command_point_sets(VFS(d, include_loose=False))
            by = {s['set']: s for s in sets}
            self.assertEqual(by['CommandPointLimit']['caption'], 'TEST CAPS')
            self.assertEqual((by['CommandPointLimit']['good'], by['CommandPointLimit']['evil']), ('11', '22'))
            mp2 = by['CommandPointsMP2']
            self.assertEqual((mp2['good'], mp2['good_line'], mp2['evil'], mp2['evil_line'], mp2['caption_line']),
                             ('33', 6, '55', 7, 5))
            self.assertEqual(mp2['caption'], 'TEST MP POINTS')


class TestModifierPercent(unittest.TestCase):
    def test_percent_hypothesis(self):
        from datacheck import modifiers
        am = (b"ModifierList A\r\n  Modifier = PRODUCTION 1.7 %\r\nEnd\r\n"
              b"ModifierList B\r\n  Modifier = PRODUCTION 300%\r\nEnd\r\n"
              b"ModifierList C\r\n  Modifier = PRODUCTION = FARM_L3 %\r\nEnd\r\n"
              b"ModifierList D\r\n  Modifier = PRODUCTION BUILD_L2\r\nEnd\r\n")
        gd = b"#define FARM_L3 1.33\r\n#define BUILD_L2 1.44\r\n"
        rows = modifiers.scan({'x': {'data\\ini\\attributemodifier.ini': ('ini.big', am),
                                     'data\\ini\\gamedata.ini': ('ini.big', gd)}})['x']
        got = {r['list']: (r['hypothesis'], r['opensage']) for r in rows}
        self.assertAlmostEqual(got['A'][0], 1.7)
        self.assertAlmostEqual(got['A'][1], 0.017)
        self.assertAlmostEqual(got['B'][0], 3.0)
        self.assertAlmostEqual(got['C'][0], 1.33)
        self.assertAlmostEqual(got['D'][0], 1.44)


def _chunk(cid, ver, body):
    import struct
    return struct.pack('<IHi', cid, ver, len(body)) + body


class TestMapScan(unittest.TestCase):
    def test_objects_starts_scripts_and_mapcache(self):
        import struct
        from datacheck import mapscan
        names = ['ObjectsList', 'Object', 'waypointName', 'PlayerScriptsList', 'ScriptList', 'Script',
                 'OrCondition', 'Condition', 'ScriptAction', 'SET_FLAG', 'FLAG', 'objectName']
        ids = {n: i + 1 for i, n in enumerate(names)}
        toc = struct.pack('<i', len(names)) + b''.join(bytes([len(n)]) + n.encode() + struct.pack('<I', ids[n]) for n in names)

        def obj(name, flags=0, wp=None):
            d = b''
            items = [(ids['waypointName'] << 8 | 3, wp)] if wp else [(ids['objectName'] << 8 | 1, None)]
            d = struct.pack('<H', len(items))
            for kt, val in items:
                d += struct.pack('<I', kt) + (struct.pack('<H', len(val)) + val.encode() if val else struct.pack('<i', 7))
            body = struct.pack('<ffffi', 1, 2, 3, 0, flags) + struct.pack('<H', len(name)) + name.encode() + d
            return _chunk(ids['Object'], 3, body)
        objs = (obj('*Waypoints/Waypoint', wp='Player_1_Start') + obj('*Waypoints/Waypoint', wp='Player_2_Start') +
                obj('EconomyPlotFlag') + obj('EconomyPlotFlag') + obj('DirtRoad', flags=0x2) + obj('DirtRoad', flags=0x4))
        cond = _chunk(ids['Condition'], 4, struct.pack('<iI', 5, ids['FLAG'] << 8) + b'\0' * 4)
        act = _chunk(ids['ScriptAction'], 2, struct.pack('<iI', 9, ids['SET_FLAG'] << 8) + b'\0' * 4)
        script = _chunk(ids['Script'], 2, struct.pack('<H', 2) + b's1' + b'\x01\x01' + _chunk(ids['OrCondition'], 1, cond) + act)
        psl = _chunk(ids['PlayerScriptsList'], 1, _chunk(ids['ScriptList'], 1, script))
        data = b'CkMp' + toc + _chunk(ids['ObjectsList'], 3, objs) + psl
        t, p = mapscan.probe.parse_toc(data)
        c = mapscan.content(data, t, mapscan.probe.top_chunks(data, t, p))
        self.assertEqual(c['starts'], [1, 2])
        self.assertEqual(c['num_start_spots'], 2)
        self.assertEqual(c['objects'], {'*Waypoints/Waypoint': 2, 'EconomyPlotFlag': 2})
        self.assertEqual(c['road_points'], {'DirtRoad': 2})
        self.assertEqual(c['scripts'], {'ACT': {'SET_FLAG': 1}, 'COND': {'FLAG': 1}})
        mc = mapscan.mapcache("MapCache maps_5Cmap_20mp_20x_5Cmap_20mp_20x_2Emap\n  isMultiplayer = yes\n"
                              "  isOfficial = yes\n  numPlayers = 2\n  Player_1_Start = X:1 Y:2 Z:0\nEND\n")
        e = mc['maps\\map mp x\\map mp x.map']
        self.assertEqual((e['numplayers'], e['ismultiplayer'], e['isofficial'], e['starts'], e['line']), (2, True, True, [1], 1))



class TestQaMaps(unittest.TestCase):
    def test_crc_poly_place_and_castle_templates(self):
        import math
        import struct
        from datacheck import qamaps as Q
        # ZH MapUtil.cpp calcCRC: rotate-left-1 then add each byte
        self.assertEqual(Q.zh_crc(b'\x01\x02\x03'), 11)
        self.assertEqual(Q.zh_crc(bytes([0x80]) + b'\0' * 25), 1)          # bit 7 + 25 rotations wraps to bit 0
        sq = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
        st = Q.poly_stats(sq)
        self.assertEqual((st['bbox'], st['area']), ((0, 0, 10, 10), 100.0))
        self.assertAlmostEqual(st['centroid'][0], 5.0)
        self.assertTrue(Q.point_in_poly(5, 5, sq))
        self.assertFalse(Q.point_in_poly(11, 5, sq))
        flag = {'x': 100.0, 'y': 200.0, 'angle': math.pi / 2, 'pva': 0.0}
        it = {'dx': 10.0, 'dy': 0.0, 'angle': 0.25}
        self.assertEqual(Q.place(flag, it, 'pva'), (110.0, 200.0, 0.25))      # PlacementViewAngle only
        x, y, a = Q.place(flag, it, 'map')                                    # + map angle (hypothesis)
        self.assertAlmostEqual(x, 100.0)
        self.assertAlmostEqual(y, 210.0)
        self.assertAlmostEqual(a, math.pi / 2 + 0.25)
        # synthetic .bse: CastleTemplates v2 with 2 items and no perimeter
        names = ['CastleTemplates', 'Camp_Test']
        ids = {n: i + 1 for i, n in enumerate(names)}
        toc = struct.pack('<i', len(names)) + b''.join(bytes([len(n)]) + n.encode() + struct.pack('<I', ids[n]) for n in names)

        def s16(t):
            return struct.pack('<H', len(t)) + t.encode()
        body = bytes([3]) + ids['Camp_Test'].to_bytes(3, 'little') + struct.pack('<I', 2)
        body += s16('Obj1') + s16('GondorBuildingFoundation') + struct.pack('<4f', 1.5, -2.0, 0.0, 0.5)
        body += s16('Obj2') + s16('GondorCampKeep') + struct.pack('<4f', 0.0, 0.0, 0.0, 0.0)
        body += struct.pack('<I', 0)
        mf = Q.MapFile(b'CkMp' + toc + _chunk(ids['CastleTemplates'], 2, body), 'x.bse')
        ct = mf.castle_templates()
        self.assertEqual((ct['version'], ct['name'], len(ct['items']), ct['perimeter']), (2, 'Camp_Test', 2, []))
        self.assertEqual((ct['items'][0]['template'], ct['items'][0]['dx'], ct['items'][0]['dy']),
                         ('GondorBuildingFoundation', 1.5, -2.0))
        self.assertEqual(Q.slot_kind(['BASE_FOUNDATION', 'BASE_DEFENSE_FOUNDATION']), 'defense_slot')
        self.assertEqual(Q.slot_kind(['STRUCTURE', 'WALL_UPGRADE', 'WALK_ON_TOP_OF_WALL']), 'wall_upgrade_slot')
        cb = Q._cache_blocks('MapCache maps_5Ca_5Ca_2Emap\n  fileSize = 12\n  fileCRC = 34\n  numPlayers = 2\n'
                             '  Player_1_Start = X:1.5 Y:2 Z:0\nEND\n')
        e = cb['maps\\a\\a.map']
        self.assertEqual((e['filesize'], e['filecrc'], e['numplayers'], e['starts']), (12, 34, 2, {1: (1.5, 2.0)}))


INHERIT_INI = """Object DefaultThingTemplate
  EditorSorting = NONE
  Behavior = DefaultUpdate ModuleTag_Default
  End
End
Object Base
  KindOf = INFANTRY
  VisionRange = 1
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 1
  End
  Behavior = PhysicsBehavior ModuleTag_Phys
  End
End
ChildObject Kid Base
  VisionRange = 2
  ShroudClearingRange = 3
  RemoveModule ModuleTag_Phys
  AddModule
    Behavior = SlowDeathBehavior ModuleTag_Death
    End
  End
End
ObjectReskin Skin Kid
  ReplaceModule ModuleTag_Body
    Body = ActiveBody ModuleTag_Body2
      MaxHealth = 2
    End
  End
End
"""


class TestObjectCounts(unittest.TestCase):
    def test_childobject_and_reskin_inheritance(self):
        from datacheck.objects import object_counts
        a = IniAnalyzer(Schema())
        a.parse_text('data\\ini\\t.ini', INHERIT_INI, records=[], source='ini.big')
        rows = {r['name']: r for r in object_counts(a.objects)}
        # Base: own KindOf, VisionRange + EditorSorting from DefaultThingTemplate
        self.assertEqual((rows['Base']['own_fields'], rows['Base']['final_fields']), (2, 3))
        # Kid: VisionRange overrides (not added twice), ShroudClearingRange is new
        self.assertEqual((rows['Kid']['own_fields'], rows['Kid']['final_fields']), (2, 4))
        self.assertEqual(rows['Skin']['final_fields'], 4)
        self.assertEqual(rows['Base']['final_modules'], 3)            # Default + Body + Phys
        self.assertEqual(rows['Kid']['final_modules'], 3)             # Phys removed, Death added
        self.assertEqual((rows['Skin']['final_modules'], rows['Skin']['final_modules_inherited']), (3, 2))
        self.assertEqual(rows['Kid']['at'], 'ini.big:data\\ini\\t.ini:15')

    def test_openers_table(self):
        a = IniAnalyzer(Schema())
        a.parse_text('data\\ini\\t.ini', OBJECT_INI.decode(), records=[])
        hows = {(k[1], k[2]): k[3] for k in a.openers.count}
        self.assertEqual(hows[('ObjectDefinition', 'Draw')], 'schema_module')
        self.assertEqual(hows[('ObjectDefinition', 'UnknownNestedThing')], 'indent')
        self.assertIn('raw_until_EndScript', hows.values())


REF_DIR = os.path.join(os.path.dirname(HERE), 'reference')
REF_ALLOWED = re.compile(r"^[A-Za-z0-9_.:\\ \-\[\]'()+/,!&$А-Яа-яЁё]*$")


def _ref_strings(o):
    if isinstance(o, dict):
        for k, v in o.items():
            yield k
            yield from _ref_strings(v)
    elif isinstance(o, list):
        for v in o:
            yield from _ref_strings(v)
    elif isinstance(o, str):
        yield o


class TestReference(unittest.TestCase):
    def _files(self):
        if not os.path.isdir(REF_DIR):
            self.skipTest('no reference/ directory')
        for label in sorted(os.listdir(REF_DIR)):
            d = os.path.join(REF_DIR, label)
            if os.path.isdir(d):
                for n in sorted(os.listdir(d)):
                    yield label, d, n

    def test_reference_has_no_ea_text(self):
        """Only counters, names, paths, file:line and sha256: no '=', ';', quotes, End/Lua tokens, no long lines."""
        lua = re.compile(r'(?i)curdrawable|(?<![a-z_])then(?![a-z_])|(?<![a-z_])local\s|~=')
        end = re.compile(r'(?<![A-Za-z_])End(?![A-Za-z_])')
        n = 0
        for label, d, name in self._files():
            p = os.path.join(d, name)
            with open(p, encoding='utf-8') as f:
                text = f.read()
            for i, line in enumerate(text.split('\n'), 1):
                self.assertLessEqual(len(line), 512, f'{label}/{name}:{i} too long')
            if name.endswith('.tsv'):
                cells = [c for line in text.split('\n') for c in line.split('\t')]
            else:
                cells = list(_ref_strings(json.loads(text)))
            for c in cells:
                self.assertRegex(c, REF_ALLOWED, f'{label}/{name}: unexpected text {c[:80]!r}')
                self.assertNotRegex(c, end, f'{label}/{name}: {c[:80]!r}')
                self.assertNotRegex(c, lua, f'{label}/{name}: {c[:80]!r}')
            n += 1
        self.assertGreater(n, 0)

    def test_reference_reproduces(self):
        """Rebuilding from the archives in manifest.json gives byte-identical files (needs BFME_GAMEDATA)."""
        game = os.environ.get('BFME_GAMEDATA')
        if not game or not os.path.isdir(game):
            self.skipTest('BFME_GAMEDATA not set')
        from datacheck import reference
        labels = sorted({label for label, _d, _n in self._files()})
        for label in labels:
            d = os.path.join(REF_DIR, label)
            with open(os.path.join(d, 'manifest.json'), encoding='utf-8') as f:
                man = json.load(f)
            arcs = [a['name'] for a in man['archives_mount_order']]
            if not all(os.path.isfile(os.path.join(game, a)) for a in arcs):
                self.skipTest(f'{label}: archives {arcs} not all in {game}')
            with tempfile.TemporaryDirectory() as tmp:
                reference.generate(game, reference.ARCHIVES.get(label, arcs), tmp,
                                   label if label in reference.ARCHIVES else None)
                self.assertEqual(sorted(os.listdir(tmp)), sorted(os.listdir(d)), label)
                for n in sorted(os.listdir(d)):
                    with open(os.path.join(d, n), 'rb') as a, open(os.path.join(tmp, n), 'rb') as b:
                        self.assertTrue(a.read() == b.read(), f'reference/{label}/{n} differs from a rebuild')


if __name__ == '__main__':
    unittest.main()
