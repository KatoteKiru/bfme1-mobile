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
