// BFME1 Mobile
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BFME: E1-39. Direct map start for BFME data, which has no Zero Hour shell menus.
#pragma once

#include "Common/AsciiString.h"

// The map a BFME run starts in when no -file is given (BfmeDefaultMapPathString() in
// Common/BfmeMapPath.h: "maps\map mp rohan\map mp rohan.map"). Single place to change it.
AsciiString BfmeDefaultMapPath();

// Starts mapPath the way the Zero Hour skirmish menu does (TheSkirmishGameInfo + MSG_NEW_GAME
// GAME_SKIRMISH): slot 0 is the local human with the first playable PlayerTemplate, all other
// slots are open (no AI). Logs one [launch] line.
void BfmeStartSkirmish(const AsciiString &mapPath);
// BFME: E1-40c. Optional AI players: aiSides is a comma list of PlayerTemplate sides (e.g.
// "Rohan,Mordor"), one medium AI per entry in slots 1..; observer makes slot 0 an observer
// (AI vs AI). The engine call site reads them from BFME_AI / BFME_AI_OBSERVER.
void BfmeStartSkirmish(const AsciiString &mapPath, const char *aiSides, Bool observer);
