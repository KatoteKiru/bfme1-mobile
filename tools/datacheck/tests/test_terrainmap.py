"""E1-26 M1 slice. Synthetic cases need no game data. The Fords of Isen check
runs only when BFME_GAMEDATA points at the archives."""
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from datacheck.big import BigArchive, write_big  # noqa: E402
from datacheck.terrainmap import (  # noqa: E402
    FILE_KINDS, M1_MAP, PathIndex, analyze, blend_tile_hits, generic_texture_hits,
    parse_blocks, parse_class, read_blend_classes, render_md, terrain_definitions,
)
from datacheck.vfs import VFS  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
REPORT = os.path.join(REPO, 'docs', 'bfme', 'E1-26-M1.md')


def blend_v15(w, h, tiles, classes):
    """classes: (name, cell_count, cell_size) laid out back to back. v15."""
    n = w * h
    if len(tiles) != n:
        raise ValueError('tiles')
    buf = bytearray()
    buf += struct.pack('<I', n)
    buf += struct.pack(f'<{n}H', *tiles)
    zeros = struct.pack(f'<{n}I', *([0] * n))
    buf += zeros + zeros + zeros
    row = (w + 7) // 8
    buf += bytes(row * h * 5)
    buf += struct.pack('<IIII', sum(c[1] for c in classes), 1, 1, len(classes))
    start = 0
    for name, count, size in classes:
        raw = name.encode('latin-1')
        buf += struct.pack('<IIII', start, count, size, 0)
        buf += struct.pack('<H', len(raw)) + raw
        start += count
    return bytes(buf)


class TestSchema(unittest.TestCase):
    def test_class_forms(self):
        none = parse_class(['NONE'])
        self.assertTrue(none['ok'])
        self.assertEqual(none['form'], 'NONE')
        plain = parse_class(['Type', 'Grass'])
        self.assertTrue(plain['ok'])
        self.assertEqual(plain['type'], 'Grass')
        self.assertEqual(plain['regions'], [])
        # OpenSAGE ParseString would have kept only this token.
        self.assertEqual(plain['first_token'], 'Type')
        regions = parse_class(['Type', 'Dirt', 'NEXT', 'Region', 'Edoras', 'NEXT', 'Region', 'Rivendell'])
        self.assertTrue(regions['ok'])
        self.assertEqual(regions['regions'], ['Edoras', 'Rivendell'])
        second = parse_class(['Type', 'Rock', 'NEXT', 'Type', 'Snow', 'NEXT', 'Region', 'Osgiliath'])
        self.assertTrue(second['ok'])
        self.assertEqual(second['extra_types'], ['Snow'])
        self.assertEqual(second['regions'], ['Osgiliath'])
        dangling = parse_class(['Type', 'Cliff', 'NEXT'])
        self.assertTrue(dangling['ok'])
        self.assertTrue(dangling['dangling_next'])
        broken = parse_class(['Type', 'Cliff', 'NEXT', 'Nope'])
        self.assertFalse(broken['ok'])

    def test_later_definition_wins_for_zh_first_for_opensage(self):
        text = """
Terrain Sand
  Texture = first.tga
  Class = Type Dirt NEXT Region Isengard
End
Terrain Sand
  Texture = first.tga
  Class = Type Dirt
End
Terrain Other
  Texture = only.tga
  Class = NONE
  NotAField = 1
End
"""
        blocks = parse_blocks(text, FILE_KINDS['terrain'])
        self.assertEqual([b['unknown_fields'] for b in blocks], [[], [], ['NotAField']])
        defs = terrain_definitions(blocks)
        sand = defs['by_name']['sand']
        self.assertEqual(sand['count'], 2)
        self.assertEqual(sand['opensage']['texture'], 'first.tga')
        self.assertEqual(sand['zh']['texture'], 'first.tga')
        self.assertNotEqual(sand['opensage']['class'], sand['zh']['class'])
        self.assertEqual(sand['zh']['class']['regions'], [])
        self.assertEqual(sand['opensage']['class']['regions'], ['Isengard'])

    def test_water_and_roads_keep_unknown_fields(self):
        water = parse_blocks("""
WaterSet MORNING
  SkyTexture = sky.tga
  WaterTexture = water.tga
  NoSuch = 1
End
WaterTransparency
  StandingWaterTexture = stand.tga
  RiverTransparencyMultiplier = 1.0
End
""", FILE_KINDS['water'])
        self.assertEqual([b['kind'] for b in water], ['WaterSet', 'WaterTransparency'])
        self.assertEqual(water[0]['unknown_fields'], ['NoSuch'])
        self.assertEqual(water[1]['name'], '')
        roads = parse_blocks("""
Road Dirt
  Texture = road.tga
  RoadWidth = 10
  RoadWidthInTexture = 0.5
End
Bridge Wide
  Texture = bridge.tga
  TextureBroken = bridge.tga
  BridgeScale = 0.6
End
Mystery
  Texture = x.tga
End
""", FILE_KINDS['roads'])
        self.assertEqual([b['known'] for b in roads], [True, True, False])
        self.assertEqual(roads[2]['kind'], 'Mystery')


class TestLookup(unittest.TestCase):
    def test_blend_path_is_case_insensitive_and_keeps_extension(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'terrain.big')
            write_big(path, [('art\\terrain\\TGras.TGA', b'not-a-real-tga')])
            index = PathIndex([BigArchive(path, label='terrain.big')])
            hits = blend_tile_hits(index, 'tgras.tga')
            self.assertEqual(len(hits), 1)
            self.assertEqual(hits[0][0], 'terrain.big')
            # A .dds with the same stem is not the blend-tile file.
            path2 = os.path.join(d, 'textures.big')
            write_big(path2, [('art\\textures\\tgras.dds', b'x')])
            index2 = PathIndex([BigArchive(path, label='terrain.big'), BigArchive(path2, label='textures.big')])
            self.assertEqual(len(blend_tile_hits(index2, 'tgras.tga')), 1)
            resolved, generic_hits = generic_texture_hits(index2, 'TGras.tga')
            self.assertTrue(resolved.lower().endswith('tgras.dds'))
            self.assertEqual(generic_hits[0][1].lower(), 'art\\textures\\tgras.dds')
            self.assertEqual(generic_hits[0][0], 'textures.big')

    def test_generic_loader_prefers_dds_then_tga_then_jpg(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'textures.big')
            write_big(path, [
                ('art\\textures\\trwagon.jpg', b'j'),
                ('art\\textures\\trwagon.tga', b't'),
                ('art\\textures\\trwagon.dds', b'd'),
            ])
            index = PathIndex([BigArchive(path, label='textures.big')])
            resolved, hits = generic_texture_hits(index, 'TRWagon.tga')
            self.assertEqual(os.path.splitext(resolved)[1], '.dds')
            self.assertEqual(hits[0][1].lower(), 'art\\textures\\trwagon.dds')

    def test_blend_classes_and_which_cells_use_them(self):
        # 2x1 map. Cell 0 paints source tile 0, cell 1 paints source tile 1.
        tiles = [0 << 2, 1 << 2]
        data = blend_v15(2, 1, tiles, [('Grass', 1, 1), ('Rock', 1, 1)])
        got = read_blend_classes(data, 15, 0, (2, 1))
        self.assertEqual([c['name'] for c in got['classes']], ['Grass', 'Rock'])
        self.assertEqual([c['cells_used'] for c in got['classes']], [1, 1])
        self.assertEqual(got['num_bitmap_tiles'], 2)
        self.assertEqual(got['source_tiles_used'], 2)
        # Quadrant bits do not change the source tile.
        tiles = [(0 << 2) | 3, (0 << 2) | 1]
        data = blend_v15(2, 1, tiles, [('Grass', 1, 1), ('Rock', 4, 2)])
        got = read_blend_classes(data, 15, 0, (2, 1))
        self.assertEqual(got['classes'][0]['cells_used'], 1)
        self.assertEqual(got['classes'][1]['cells_used'], 0)


class TestFordsOfIsen(unittest.TestCase):
    def setUp(self):
        game = os.environ.get('BFME_GAMEDATA')
        if not game:
            self.skipTest('BFME_GAMEDATA not set')
        map_big = os.path.join(game, 'maps.big')
        terrain_big = os.path.join(game, 'terrain.big')
        if not (os.path.isfile(map_big) and os.path.isfile(terrain_big)):
            self.skipTest('gamedata archives not in ' + game)
        self.vfs = VFS(game, include_loose=False)

    def test_m1_blend_textures_match_oracle_counts(self):
        report = analyze(self.vfs, M1_MAP)
        self.assertEqual(len(report['rows']), 63)
        self.assertEqual(report['found_in_m1_archives'], 63)
        self.assertEqual(report['missing'], [])
        self.assertTrue(all(r['m1_hits'][0]['archive'] == 'terrain.big' for r in report['rows']))
        self.assertTrue(all(r['m1_hits'][0]['path'].lower().startswith('art\\terrain\\') for r in report['rows']))
        self.assertTrue(all(len(r['hits']) == 1 for r in report['rows']))
        self.assertTrue(all(r['cells_used'] > 0 for r in report['rows']))
        self.assertTrue(all(r['texture_same_in_ini_big'] for r in report['rows']))
        self.assertTrue(all(r['magic'] == 0 for r in report['rows']))
        win, retail = report['map_versions']
        self.assertEqual(win['archive'], '_patch222maps.big')
        self.assertEqual(retail['archive'], 'maps.big')
        self.assertEqual(retail['decoded_bytes'], 2989851)
        for version in report['map_versions']:
            self.assertEqual(version['height'], {
                'version': 5, 'w': 415, 'h': 353, 'border': 20, 'chunk_size': version['height']['chunk_size'],
            })
            self.assertEqual(version['height']['w'], 415)
            self.assertEqual(version['height']['h'], 353)
            self.assertEqual(version['height']['border'], 20)
            # OpenSAGE, recorded in docs/formats/BFME1_vs_ZH.md: 984 cells,
            # 16700 blend descriptions, 577 cliff mappings. The file stores
            # those counts plus the unused slot 0.
            self.assertEqual(version['num_bitmap_tiles'], 984)
            self.assertEqual(version['num_blended'], 16701)
            self.assertEqual(version['num_cliff'], 578)
            self.assertEqual(version['leftover_bytes'], 0)
            self.assertEqual(version['num_texture_classes'], 63)
        self.assertEqual(win['texture_names'], retail['texture_names'])
        terrain = report['terrain']
        self.assertEqual(terrain['blocks'], 886)
        self.assertEqual(terrain['unique'], 883)
        self.assertEqual(terrain['class_bad'], 0)
        self.assertEqual(terrain['extra_type'], 3)
        self.assertEqual(terrain['dangling_next'], 1)
        self.assertEqual(terrain['unknown'], [])
        self.assertEqual(len(terrain['redefinitions']), 3)
        self.assertEqual(report['water']['unknown'], [])
        self.assertEqual(report['roads']['unknown'], [])
        self.assertEqual(sum(1 for x in report['water']['textures'] if x['m1_hit']), 9)
        self.assertEqual(sum(1 for x in report['roads']['textures'] if x['m1_hit']), 57)
        text = render_md(report)
        with open(REPORT, encoding='utf-8') as f:
            self.assertEqual(f.read(), text)


if __name__ == '__main__':
    unittest.main()
