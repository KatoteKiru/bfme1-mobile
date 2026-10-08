"""datacheck: inventory of BFME1 game data (BIG/VFS + schema-driven INI analysis + patch diff).

Usage:
  python3 -m datacheck --game DIR --out out   (run from tools/datacheck; DIR may also come from $BFME_GAMEDATA)
  ./run.sh [--game DIR] [--out DIR]
"""
import argparse
import os
import sys
import time

from .vfs import VFS
from .schema import Schema
from .ini import IniAnalyzer
from .objects import resolve, module_frequency
from .patchdiff import patch_diffs, command_point_sets
from . import report
from . import gameplay


def main(argv=None):
    ap = argparse.ArgumentParser(prog='datacheck')
    ap.add_argument('--game', default=os.environ.get('BFME_GAMEDATA'),
                    help='directory with the BFME1 .big archives (default: $BFME_GAMEDATA)')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out'))
    ap.add_argument('--no-loose', action='store_true', help='ignore loose files next to the .big archives')
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--archives', default='', help='comma-separated BIG names to mount (default: all *.big), '
                    'e.g. ini.big,maps.big for the base-game-only report')
    ap.add_argument('--no-maps', action='store_true', help='skip per-map BlendTileData counters (~30 s)')
    ap.add_argument('--gp-lua-blocks', type=int, default=2092,
                    help='BeginScript blocks in 2.22 as counted by Gameplay, for the cross-check in report section 8')
    ap.add_argument('--gp-script-tsv', default=os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
                    os.path.abspath(__file__))))), 'docs', 'formats', 'data', 'bfme1_script_names_maps_big.tsv'),
                    help='script-name TSV Gameplay used (read only; the regenerated one goes to --out)')
    ap.add_argument('--gp-scratch', default=os.environ.get('BFME_GP_SCRATCH', ''),
                    help='gameplay scratch dir to cross-check (optional; default: $BFME_GP_SCRATCH)')
    ap.add_argument('--reference-out', default='', metavar='DIR',
                    help='also write the E1-56 reference (counters, names, lines, sha256; no EA data) to DIR. '
                    'Archives: --archives, or the pinned set for DIR named base / 2.22 (datacheck/reference.py)')
    ap.add_argument('--reference-only', action='store_true', help='with --reference-out: skip the reports')
    args = ap.parse_args(argv)
    if not args.game:
        ap.error('no game data: pass --game DIR or set BFME_GAMEDATA')
    if args.reference_out:
        from . import reference
        label = os.path.basename(os.path.normpath(args.reference_out))
        arcs = [a.strip() for a in args.archives.split(',') if a.strip()] or reference.ARCHIVES.get(label)
        if not arcs:
            ap.error(f'--reference-out {args.reference_out}: pass --archives (no pinned set for "{label}")')
        t = time.time()
        reference.generate(args.game, arcs, args.reference_out, label if label in reference.ARCHIVES else None)
        print(f'reference ({", ".join(arcs)}) -> {args.reference_out} in {time.time() - t:.0f} s')
        if args.reference_only:
            return 0
    t0 = time.time()
    only = {a.strip().lower() for a in args.archives.split(',') if a.strip()}
    vfs = VFS(args.game, include_loose=not args.no_loose,
              big_filter=(lambda rel: os.path.basename(rel).lower() in only) if only else None)
    schema = Schema()
    an = IniAnalyzer(schema, vfs)
    inis = [k for k in vfs.files if k.endswith('.ini')]
    texts = {}
    for k in inis:
        texts[k] = vfs.read(k).decode('latin-1')
        an.collect_defines(vfs.files[k].path, texts[k])
    records = []
    for k in inis:
        an.parse_text(vfs.files[k].path, texts[k], records=records, source=vfs.files[k].source)
    final, missing = resolve(an.objects, only=lambda o: o['file'].lower().startswith('data\\ini'))
    pdiff, padded = patch_diffs(schema, vfs, an.defines)
    cp = command_point_sets(vfs)
    os.makedirs(args.out, exist_ok=True)
    mp = mc = None
    if not args.no_maps:
        from . import maps, mapscan
        mp = maps.scan(vfs)
        maps.write(mp, args.out)
        cache_vf = vfs.files.get('maps\\mapcache.ini')
        cache = mapscan.mapcache(vfs.read(cache_vf.path).decode('latin-1')) if cache_vf else {}
        mc = mapscan.summarize(mp, cache, f'{cache_vf.source}:{cache_vf.path}' if cache_vf else '(нет mapcache.ini)',
                               mapscan.kindof_map(records, an.objects), final, schema.zh, args.gp_script_tsv)
        mapscan.write(mc, args.out, args.top)
    from . import syntax, modifiers
    sx = syntax.build(vfs, schema, args.gp_lua_blocks)
    syntax.write(sx, args.out)
    mods = modifiers.scan(syntax.version_sets(vfs))
    rep = report.build(vfs, schema, an, final, missing, pdiff, padded, cp, args.top, time.time() - t0, args.game)
    rep['syntax'] = sx['sets']
    rep['modifiers_production'] = {n: [r for r in rows if r['type'] == 'PRODUCTION'] for n, rows in mods.items()}
    if mc is not None:
        rep['maps_1v1'] = {k: mc[k] for k in ('method', 'mapcache', 'maps_total', 'maps_in_cache', 'cache_numplayers_agree',
                                              'cache_size_mismatch', 'start_spot_hist', 'maps_1v1', 'excluded_2_start',
                                              'blend_1v1')}
        rep['maps_1v1']['script_compare'] = {k: {kk: (len(vv) if isinstance(vv, list) else vv) for kk, vv in v.items()}
                                             for k, v in mc['script_compare'].items()}
    extra, tail = [], []
    if mp is not None:
        from . import maps, mapscan
        extra += maps.md_summary(mp)
        tail += mapscan.md_summary(mc, args.top)
    tail += syntax.md_summary(sx) + modifiers.md_summary(mods)
    report.write(rep, args.out, args.top, extra_md=extra, tail_md=tail)
    g = gameplay.build(vfs, an, final, records, texts, args.gp_scratch if os.path.isdir(args.gp_scratch) else None,
                       top_names={k.lower() for k in schema.top} | {b.lower() for b in schema.zh['blocks']})
    gameplay.write(g, args.out, args.top)
    print(f"archives {rep['vfs']['archives_count']}, files {rep['vfs']['files_total']}, "
          f"ini {rep['ini']['files']}, top-level blocks {rep['ini']['top_level_blocks']}, "
          f"unknown blocks {rep['ini']['unknown_blocks_total']}, unknown fields {rep['ini']['unknown_fields_total']}, "
          f"unknown modules {rep['ini']['unknown_modules_total']}, warnings {rep['ini']['structure_warnings']}; "
          f"report -> {args.out}/report.md")
    return 0


if __name__ == '__main__':
    sys.exit(main())
