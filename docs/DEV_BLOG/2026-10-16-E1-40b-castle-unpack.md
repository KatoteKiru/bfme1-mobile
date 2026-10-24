## 2026-10-16: E1-40b skirmish start castle

BFME1 factions have no `StartingBuilding` (playertemplate.ini: `;StartingBuilding = ...`,
`StartMoney = 0`). The start is the map's castle flag: the flag owned by
`<player>/Player_<N>_Inherit` unpacks, for the player at start position N, the base
`CastleToUnpackForFaction <side> <base> <cost>` from the flag's `CastleBehavior`
(civilianbuildings.ini CastleFlag and its ChildObjects) out of `bases\<base>\<base>.bse`.
The `.bse` is a map file; its `CastleTemplates` chunk lists (TemplateName, Offset, Angle). As
in OpenSAGE (`Scene3D.CreateSkirmishPlayerStartingBuilding`, `CastleBehavior.Unpack`) each part
is placed at flag position + Offset rotated by the flag's `PlacementViewAngle`, instantly and free.

Headless rohan: `CastleFlagSE -> 'player0' (side Rohan, start 2): Castle_RohanSE, 66 of 66
parts`; player0 owns 67 objects at frame 0 (was 0). Start money is 1000: gamedata.ini
`DefaultStartingCash = 1000`, which is multiplayer.ini `InitialCreditsLow`.

Open: every part is owned by the player (`FilterValidOwnedEntries` names BFME KindOf bits Zero
Hour does not have); the flag is not hidden; offsets in the .bse are relative to the base origin,
not the flag (castle_rohan.bse: the flag sits at the floor's offset), which OpenSAGE also ignores.
