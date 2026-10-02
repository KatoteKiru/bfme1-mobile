/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GXReplayCheck.h ///////////////////////////////////////////////////////////
// GeneralsX @feature Android port 23/09/2026 Replay checking without watching.
//
// A replay can only be reproduced by simulating it from frame 0 -- it holds the
// commands, not the state -- so "start at frame 3400" really means "get to 3400
// fast". Two launch options, set by the launcher's Replay check screen and
// combined with the engine's own -replay:
//
//   -gxFastTo <frame>   run the logic without drawing until <frame>, then play
//                       normally; -1 means the whole replay
//   -gxAutoQuit         when the replay ends, or some frames after the first
//                       checksum mismatch, write gx_replay_check_result.txt in
//                       the user-data folder and quit back to the launcher
//   -gxCrcEveryFrame    also compute the logic checksum on every frame and print it
//                       ("crc every frame N: X"). The recording only carries one per
//                       hundred frames; scripts/tooling/replay/rep_crc_every_frame.py
//                       writes these into a copy of the replay (header C=001), and the
//                       PC client, playing that copy, stops at the first frame where it
//                       disagrees with this device
//   -gxCrcLog <path>    BFME: QA 02/10/2026 write that same per-frame CRC, plus the
//                       running CRC after each section, to a bfme-crclog/1 text file.
//                       Off unless the option is passed. Does not enable GX_NET_TRACE
//                       and does not change the simulation: the CRC is getCRC(CRC_RECALC)
//                       through XferCRC, which only reads, and the logic seed is a local
//                       copy that is only read. The file actually opened is
//                       "<stem>.<replay>.<pid><ext>" so -jobs workers do not share one
//                       path. -jobs 1 is the recommended QA invocation.
//
// Fast-forward runs extra logic frames between rendered ones, exactly what the
// engine's headless replay simulation does per frame (particles, then logic), so
// the simulation and its checksums are the same as when watching.
#pragma once

#include "Lib/BaseType.h"

namespace GXReplayCheck
{
	void setFastForwardTo( Int frame );
	void setAutoQuit( Bool autoQuit );
	void setCrcEveryFrame( Bool everyFrame );
	Bool crcEveryFrame();
	Bool isActive();

	// GeneralsX @feature Android port 24/09/2026 Diagnostic only: extra frames a factory door
	// takes to open, read from the replay's file name ("..._doordelay<N>at<F>.rep": doors that
	// start opening at frame F or later), 0 otherwise.
	// It lets one phone run test "the PC's unit left the factory N frames later" against the
	// PC's own checksums, which the PC cannot be asked for any other way.
	Int doorDelayFrames( UnsignedInt doorOpenedFrame );

	// Diagnostic: the frame named by "rngahead<F>" in the replay's file name, or 0. At that
	// frame's checksum the log lists the logic seed checksum after 0..60 extra draws.
	UnsignedInt rngAheadFrame();

	// Diagnostic: "fpwin<A>to<B>" in the replay's file name. Every floating-point "invalid"
	// event in frames A..B is logged with its module and object, not only the first of each
	// kind. Returns FALSE when the name carries no window.
	Bool fpWindow( UnsignedInt &from, UnsignedInt &to );

	// Diagnostic: "upgshift<N>at<F>" in the replay's file name (N may be negative, written
	// "upgshiftm2at19250" for -2). An upgrade whose research would finish at frame F finishes
	// N frames later instead, to test "the PC finished it on another frame" against the PC's
	// own checksums. "..._upgshift<N>at<F>id<ID>.rep" limits it to the object with that id.
	// Returns the shift for an upgrade that would finish at naturalDoneFrame.
	Int upgradeShiftFrames( UnsignedInt naturalDoneFrame, UnsignedInt objectID );

	// GameEngine::update, after the regular logic update.
	void update();

	// RecorderClass, at every compared checkpoint.
	void noteCheckpoint( UnsignedInt frame, Bool matched, UnsignedInt ours, UnsignedInt recorded );

	// BFME: QA 02/10/2026 Machine-readable CRC of every logic frame for the replay tests
	// (tests/replay/crclog.py, docs/REPLAY-TESTS.md). "-gxCrcLog <path>" writes
	// "bfme-crclog/1": one line per frame "<frame> <crc> Objects=.. Partition=..
	// PlayerList=.. AI=..", independent of GX_NET_TRACE, line-buffered so a crash keeps
	// every finished frame. The header carries "# logic_hz=<LOGICFRAMES_PER_SECOND>"
	// (30 unless the 60 Hz build is on); the replay header does not record the tick rate.
	// The real file is "<path stem>.<replay>.<pid><ext>" so -jobs workers never share it.
	// Use -jobs 1. Off by default.
	void setCrcLogPath( const char *path );
	Bool crcLogEnabled();
	// GameLogic::getCRC, after each section: the running CRC (same values as "crc parts").
	void noteCrcPart( const char *section, UnsignedInt runningCrc );
	void writeCrcLog( UnsignedInt frame, UnsignedInt crc );
	// status is "match", "mismatch" (replay-check finish) or "exit" (atexit). The footer
	// is "frames=<s_lastFrame + 1>" from the last line written, not TheGameLogic->getFrame():
	// atexit runs after GameLogic has been destroyed.
	void closeCrcLog( const char *status );
}
