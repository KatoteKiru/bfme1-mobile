## 2026-10-16: E1-40 ChildObject, KindOf and map owners

The rohan skirmish started with no castle for anyone. Three of the causes are
in the INI and map loaders, and are fixed here (lenient mode only):

- `ChildObject <name> <parent>` blocks were unknown, so the castle flags
  `CastleFlagNW`/`CastleFlagSE` and the economy plots never became objects.
  They are now a full copy of the parent with all fields.
- A KindOf (any bit-flag list) stopped at the first name Zero Hour does not
  know and dropped every later flag. Unknown names are now skipped one by one.
- BFME maps store the owner as `<player>/<team>`. Zero Hour looked the whole
  string up as a team, so all 1700 objects became neutral. Now 1239 belong to
  `PlyrCivilian`, 6 to `PlyrCreeps`, and 455 (owner `/team`) stay neutral.
  The castle flags (owner `PlyrCivilian/Player_N_Inherit`) go to `PlyrCivilian`.

Headless rohan: 1700 objects at frame 0 (1623 before), heartbeat to frame 2400
in 90 s, no new errors. The castle itself (CastleBehavior, `.bse` unpack) and
the start money are the next step.

### 50 maps crashed at start: InitialHealth

The 406-map run had 50 maps (edoras, helms deep, isengard, minas tirith,
shellmap1, ...) crash with SIGSEGV in `Pathfinder::internal_classifyObjectFootprint`. A BFME
body usually sets only `MaxHealth` (`GondorCastleUpgrade` in gondorbuildings.ini: `MaxHealth =
GONDOR_CASTLE_WALL_UPGRADE_HEALTH`, no `InitialHealth`). Zero Hour's default is 0, so the
object was born as rubble and told the pathfinder before `newMap()` had built its grid. In
the original (and OpenSAGE) `InitialHealth` defaults to `MaxHealth`; lenient mode now does
the same. The pathfinder also ignores footprint changes until its grid exists. The edoras,
helms deep, isengard, minas tirith and "Minas Tirith Evil - 7 Rings" maps now reach frame 300.
