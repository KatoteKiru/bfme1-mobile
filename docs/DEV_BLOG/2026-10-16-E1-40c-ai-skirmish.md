## 2026-10-16: E1-40c AI vs AI on rohan

`BFME_AI=Mordor,Isengard BFME_AI_OBSERVER=1` starts rohan with two medium AIs and an observer.
Two loader bugs had to go first. BFME has no `Data\Scripts\SkirmishScripts.scb`, so ZH crashed
in `ScriptList::duplicateAndQualify` (null list). Then the AI player was renamed to a skirmish
name with no team and lost `team<player>`, so its castle was neutral. With both fixed, both
AIs get their castle (Castle_MordorNW 66 parts, Castle_IsengardSE 50 parts).

The AIs then do nothing for 22500 frames (12.5 game minutes): 21 structures, 0 units, +60
money per 30 s each, no fights, no defeat. BFME skirmish AI is a script library map per
PlayerAIType (playeraitypes.ini: `PlayerAIType RohanSkirmishAI` with `LibraryMap =
"Libraries\AI_RohanSkirmish\AI_RohanSkirmish.map"`; PlayerTemplate `DefaultPlayerAIType`).
The engine reads neither, and the ZH AI's dozer base building does not fit BFME's plot and
castle building.
