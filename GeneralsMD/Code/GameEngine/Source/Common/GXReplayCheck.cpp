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

// GXReplayCheck.cpp /////////////////////////////////////////////////////////
// See GXReplayCheck.h.

#include "PreRTS.h"

#include "Common/GXReplayCheck.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Recorder.h"
#include "Common/ReplaySimulation.h"
#include "Common/StatsExporter.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"

#include <chrono>
#include <cstdlib> // BFME: QA atexit
#include <cstring> // BFME: QA strcmp
// BFME: QA process id for unique -gxCrcLog file names with -jobs N
#ifdef _WIN32
#include <process.h>
#define BFME_GETPID _getpid
#else
#include <unistd.h>
#define BFME_GETPID getpid
#endif
#include <cstdio>

namespace
{
	Int s_fastTo = 0;              // 0: off, -1: whole replay, N: until frame N
	Bool s_autoQuit = FALSE;
	Bool s_crcEveryFrame = FALSE;
	Bool s_sawPlayback = FALSE;
	Bool s_statsStarted = FALSE;
	Bool s_done = FALSE;
	UnsignedInt s_checkpoints = 0;
	UnsignedInt s_matched = 0;
	UnsignedInt s_lastMatched = 0;
	UnsignedInt s_firstMismatch = 0;
	UnsignedInt s_firstOurs = 0, s_firstRecorded = 0;
	UnsignedInt s_lastCheckpoint = 0;
	UnsignedInt s_lastProgress = 0;
	std::chrono::steady_clock::time_point s_start;

	// After the first mismatch, keep going long enough for the per-frame movement trace
	// (GameLogic.cpp, 600 frames) and a few more checkpoints, then stop.
	const UnsignedInt FRAMES_AFTER_MISMATCH = 700;

	Bool playbackRunning()
	{
		return TheRecorder != nullptr && TheRecorder->isPlaybackInProgress()
			&& TheGameLogic != nullptr && TheGameLogic->isInGame();
	}

	Bool fastForwarding()
	{
		if (s_fastTo == 0 || !playbackRunning())
			return FALSE;
		if (s_fastTo < 0)
			return TRUE;
		return TheGameLogic->getFrame() < (UnsignedInt)s_fastTo;
	}

	AsciiString replayName()
	{
		if (TheGlobalData != nullptr && !TheGlobalData->m_simulateReplays.empty())
			return TheGlobalData->m_simulateReplays[0];
		return AsciiString("?");
	}

	void writeResult(const char *status)
	{
		AsciiString path = TheGlobalData->getPath_UserData();
		path.concat("gx_replay_check_result.txt");
		FILE *f = fopen(path.str(), "w");
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start).count();
		const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
		if (f != nullptr)
		{
			fprintf(f, "replay=%s\n", replayName().str());
			fprintf(f, "status=%s\n", status);
			fprintf(f, "frames=%u\n", (unsigned)frame);
			fprintf(f, "checkpoints=%u\n", (unsigned)s_checkpoints);
			fprintf(f, "matched=%u\n", (unsigned)s_matched);
			fprintf(f, "last_matched=%u\n", (unsigned)s_lastMatched);
			fprintf(f, "first_mismatch=%u\n", (unsigned)s_firstMismatch);
			fprintf(f, "seconds=%.1f\n", seconds);
			fclose(f);
		}
		fprintf(stderr, "[GX-NET] replay check %s: %s, %u frames, %u/%u checkpoints matched, last match %u, first mismatch %u (ours %08X, recorded %08X), %.1f s\n",
			replayName().str(), status, (unsigned)frame, (unsigned)s_matched, (unsigned)s_checkpoints,
			(unsigned)s_lastMatched, (unsigned)s_firstMismatch, (unsigned)s_firstOurs, (unsigned)s_firstRecorded, seconds);
		fflush(stderr);
	}

	void finish(const char *status)
	{
		if (s_done)
			return;
		s_done = TRUE;
		writeResult(status);
		GXReplayCheck::closeCrcLog(status); // BFME: QA
		// The same event record the PC client writes with -headless -replay -exportStats,
		// as Replays/<name>.gamestats.json, for a frame-accurate comparison.
		if (s_statsStarted && TheRecorder != nullptr)
			ExportGameStatsJSON(TheRecorder->getReplayDir(), replayName());
		if (s_autoQuit)
		{
			ReplaySimulation::stop();
			if (TheGameLogic != nullptr && TheGameLogic->isInGame())
				TheGameLogic->exitGame();
			TheGameEngine->setQuitting(TRUE);
		}
	}
}

namespace GXReplayCheck
{

void setFastForwardTo( Int frame ) { s_fastTo = frame; }
void setAutoQuit( Bool autoQuit ) { s_autoQuit = autoQuit; }
void setCrcEveryFrame( Bool everyFrame ) { s_crcEveryFrame = everyFrame; }

// BFME: QA 02/10/2026 begin -- per-frame CRC file for tests/replay (see GXReplayCheck.h)
// The file is opened lazily on the first written frame: by then the replay name is known,
// and the -jobs parent process (which only spawns workers) never creates a file.
// Final name: "<path>.<replay>.<pid>" + extension of <path>, e.g.
// out/x.actual.crclog -> out/x.actual.e2-battle.12345.crclog (unique per -jobs worker).
// -jobs 1 is the recommended invocation; see docs/ETC/COMMAND_LINE_PARAMETERS.md.
static AsciiString s_crcLogPath;
static FILE *s_crcLog = nullptr;
static Bool s_crcLogClosed = FALSE; // do not reopen and truncate after the footer is written
static UnsignedInt s_lastFrame = 0; // closeCrcLog may run from atexit after GameLogic is gone
static Bool s_haveFrame = FALSE;
static const char *const s_crcPartNames[4] = { "Objects", "Partition", "PlayerList", "AI" };
static UnsignedInt s_crcParts[4];
static Bool s_crcHavePart[4] = { FALSE, FALSE, FALSE, FALSE };

static AsciiString crcLogFileName()
{
	// replay name without directories, '.rep' and characters unsafe for file names
	AsciiString rep = replayName();
	const char *base = rep.str();
	for (const char *c = base; *c; ++c)
		if (*c == '/' || *c == '\\')
			base = c + 1;
	char name[128];
	size_t n = 0;
	for (const char *c = base; *c && n + 1 < sizeof(name); ++c)
	{
		const char ch = *c;
		const Bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
			|| ch == '-' || ch == '_' || ch == '.';
		name[n++] = ok ? ch : '_';
	}
	name[n] = 0;
	if (n >= 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'r' && (name[n - 2] | 0x20) == 'e'
		&& (name[n - 1] | 0x20) == 'p')
		name[n - 4] = 0;  // ".rep", any case (CommandLine.cpp parseReplay: endsWithNoCase)

	// split <path> into stem + extension (only an extension of the last path component)
	const char *path = s_crcLogPath.str();
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/');
	const char *bslash = strrchr(path, '\\');
	if (bslash != nullptr && (slash == nullptr || bslash > slash))
		slash = bslash;
	if (dot != nullptr && slash != nullptr && dot < slash)
		dot = nullptr;
	char buf[1024];
	snprintf(buf, sizeof(buf), "%.*s.%s.%d%s",
		(int)(dot != nullptr ? dot - path : (int)strlen(path)), path, name, (int)BFME_GETPID(),
		dot != nullptr ? dot : "");
	return AsciiString(buf);
}

void setCrcLogPath( const char *path )
{
	if (path == nullptr || path[0] == '\0')
		return;
	s_crcLogPath = path;
	// BFME: QA the parent of -jobs N does not simulate; workers are a fresh argv and
	// would otherwise never see -gxCrcLog. Each worker still opens its own file.
	ReplaySimulation::setCrcLogPath(path);
	static Bool s_atexit = FALSE;
	if (!s_atexit)
	{
		s_atexit = TRUE;
		// No capture: GameLogic is already destroyed by the time this runs.
		atexit([]() { closeCrcLog("exit"); });
	}
}

Bool crcLogEnabled() { return !s_crcLogPath.isEmpty(); }

void noteCrcPart( const char *section, UnsignedInt runningCrc )
{
	if (section == nullptr)
		return;
	for (Int i = 0; i < 4; ++i)
	{
		if (strcmp(section, s_crcPartNames[i]) == 0)
		{
			s_crcParts[i] = runningCrc;
			s_crcHavePart[i] = TRUE;
			return;
		}
	}
}

void writeCrcLog( UnsignedInt frame, UnsignedInt crc )
{
	if (s_crcLogClosed || s_crcLogPath.isEmpty())
		return;
	if (s_crcLog == nullptr)
	{
		const AsciiString file = crcLogFileName();
		s_crcLog = fopen(file.str(), "w");
		if (s_crcLog == nullptr)
		{
			fprintf(stderr, "[BFME-QA] cannot open crc log %s\n", file.str());
			s_crcLogPath.clear();
			return;
		}
		setvbuf(s_crcLog, nullptr, _IOLBF, 0);
		fprintf(stderr, "[BFME-QA] crc log %s\n", file.str());
		fprintf(s_crcLog, "# bfme-crclog/1\n# source=engine\n# interval=1\n");
		// Logic tick rate. The .rep header does not store it. 30 unless SAGE_HIGH_FPS_SIM.
		fprintf(s_crcLog, "# logic_hz=%d\n", (int)LOGICFRAMES_PER_SECOND);
		fprintf(s_crcLog, "# replay=%s\n", replayName().str());
		if (TheGlobalData != nullptr)
			fprintf(s_crcLog, "# ini_crc=%08X\n# exe_crc=%08X\n",
				(unsigned)TheGlobalData->m_iniCRC, (unsigned)TheGlobalData->m_exeCRC);
	}
	fprintf(s_crcLog, "%u %08X", (unsigned)frame, (unsigned)crc);
	for (Int i = 0; i < 4; ++i)
	{
		if (s_crcHavePart[i])
			fprintf(s_crcLog, " %s=%08X", s_crcPartNames[i], (unsigned)s_crcParts[i]);
		s_crcHavePart[i] = FALSE;
	}
	fputc('\n', s_crcLog);
	s_lastFrame = frame;
	s_haveFrame = TRUE;
}

void closeCrcLog( const char *status )
{
	if (s_crcLog == nullptr)
	{
		s_crcLogClosed = TRUE;
		return;
	}
	s_crcLogClosed = TRUE;
	// frames = span of written lines: last written frame + 1. Frames are 0-based and
	// m_frame is incremented after the CRC, so this matches getFrame() at a normal
	// exit. Do not call TheGameLogic here: atexit runs after it has been destroyed.
	fprintf(s_crcLog, "# end frames=%u status=%s\n",
		(unsigned)(s_haveFrame ? s_lastFrame + 1 : 0), status != nullptr ? status : "exit");
	fclose(s_crcLog);
	s_crcLog = nullptr;
}
// BFME: QA 02/10/2026 end
Int doorDelayFrames( UnsignedInt doorOpenedFrame )
{
	static Int delay = -1;
	static UnsignedInt fromFrame = 0;
	if (delay < 0)
	{
		delay = 0;
		AsciiString name = replayName();
		name.toLower();
		const char *tag = strstr(name.str(), "doordelay");
		if (tag != nullptr)
		{
			delay = atoi(tag + 9);
			const char *at = strstr(tag, "at");
			if (at != nullptr)
				fromFrame = (UnsignedInt)atoi(at + 2);
		}
		if (delay > 0)
		{
			fprintf(stderr, "[GX-NET] replay check: factory doors that start opening from frame %u open %d frames late (diagnostic, from the file name)\n",
				(unsigned)fromFrame, (int)delay);
			fflush(stderr);
		}
	}
	return doorOpenedFrame >= fromFrame ? delay : 0;
}

UnsignedInt rngAheadFrame()
{
	static Int frame = -1;
	if (frame < 0)
	{
		frame = 0;
		AsciiString name = replayName();
		name.toLower();
		const char *tag = strstr(name.str(), "rngahead");
		if (tag != nullptr)
			frame = atoi(tag + 8);
	}
	return (UnsignedInt)frame;
}

Int upgradeShiftFrames( UnsignedInt naturalDoneFrame, UnsignedInt objectID )
{
	static Bool parsed = FALSE;
	static Int shift = 0;
	static UnsignedInt atFrame = 0;
	static UnsignedInt onlyID = 0;
	if (!parsed)
	{
		parsed = TRUE;
		AsciiString name = replayName();
		name.toLower();
		const char *tag = strstr(name.str(), "upgshift");
		if (tag != nullptr)
		{
			const char *num = tag + 8;
			Int sign = 1;
			if (*num == 'm')
			{
				sign = -1;
				++num;
			}
			shift = sign * atoi(num);
			const char *at = strstr(num, "at");
			atFrame = at != nullptr ? (UnsignedInt)atoi(at + 2) : 0;
			const char *id = at != nullptr ? strstr(at, "id") : nullptr;
			onlyID = id != nullptr ? (UnsignedInt)atoi(id + 2) : 0;
			fprintf(stderr, "[GX-NET] replay check: upgrades that would finish at frame %u finish %+d frames later, object id %u (0 = any) (diagnostic, from the file name)\n",
				(unsigned)atFrame, (int)shift, (unsigned)onlyID);
			fflush(stderr);
		}
	}
	if (onlyID != 0 && objectID != onlyID)
		return 0;
	return naturalDoneFrame == atFrame ? shift : 0;
}

Bool fpWindow( UnsignedInt &from, UnsignedInt &to )
{
	static Int state = -1;
	static UnsignedInt a = 0, b = 0;
	if (state < 0)
	{
		state = 0;
		AsciiString name = replayName();
		name.toLower();
		const char *tag = strstr(name.str(), "fpwin");
		if (tag != nullptr)
		{
			a = (UnsignedInt)atoi(tag + 5);
			const char *sep = strstr(tag, "to");
			b = sep != nullptr ? (UnsignedInt)atoi(sep + 2) : a;
			state = 1;
		}
	}
	from = a;
	to = b;
	return state == 1;
}

Bool crcEveryFrame()
{
	// The launcher also drops a marker file in the game folder (the working directory),
	// so the option survives a launch whose arguments were lost on the way.
	static const Bool marker = []() {
		FILE *f = fopen("gx_crc_every_frame.txt", "r");
		if (f != nullptr)
			fclose(f);
		return f != nullptr;
	}();
	return s_crcEveryFrame || marker;
}
Bool isActive() { return s_fastTo != 0 || s_autoQuit; }

void noteCheckpoint( UnsignedInt frame, Bool matched, UnsignedInt ours, UnsignedInt recorded )
{
	++s_checkpoints;
	s_lastCheckpoint = frame;
	if (matched)
	{
		++s_matched;
		if (s_firstMismatch == 0)
			s_lastMatched = frame;
	}
	else if (s_firstMismatch == 0)
	{
		s_firstMismatch = frame;
		s_firstOurs = ours;
		s_firstRecorded = recorded;
	}
}

void update()
{
	if (!isActive() || s_done)
		return;

	// Before the game starts, so the starting buildings and units are recorded too.
	if (!s_statsStarted)
	{
		s_statsStarted = TRUE;
		StatsExporterBeginRecording();
	}
	if (playbackRunning())
		StatsExporterCollectSnapshot();

	if (playbackRunning())
	{
		if (!s_sawPlayback)
		{
			s_sawPlayback = TRUE;
			s_start = std::chrono::steady_clock::now();
			fprintf(stderr, "[GX-NET] replay check %s: started, fast-forward %s%d, auto-quit %s, checksum of every frame %s\n",
				replayName().str(), s_fastTo < 0 ? "to the end " : "to frame ", (int)s_fastTo,
				s_autoQuit ? "on" : "off", crcEveryFrame() ? "on" : "off");
			fflush(stderr);
		}

		// Extra logic frames for up to ~250 ms per rendered frame, in the order
		// GameEngine::update uses: client-side bookkeeping, then the message stream,
		// then logic. The message stream is not optional: during a watched playback
		// the local checksum travels as MSG_LOGIC_CRC through TheMessageStream
		// (GameLogic.cpp only bypasses it in headless simulation), so skipping
		// propagateMessages() delayed our checksums by a checkpoint and paired each
		// with the next recorded one -- the first run reported 0/5 matched on a
		// replay that matches to 3500.
		//
		// GeneralsX @performance Android port 24/09/2026 The budget was 40 ms, so a
		// rendered frame (tens of ms on a phone) came after every 40 ms of logic and took a
		// large share of a run that nobody watches. 250 ms keeps the screen alive at about
		// four frames a second and spends the rest on logic.
		if (fastForwarding())
		{
			const std::chrono::steady_clock::time_point until =
				std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
			while (fastForwarding() && !s_done && std::chrono::steady_clock::now() < until)
			{
				TheGameClient->updateHeadless();
				TheMessageStream->propagateMessages();
				TheGameLogic->UPDATE();
				StatsExporterCollectSnapshot();
				if (s_autoQuit && s_firstMismatch != 0 && TheGameLogic->getFrame() >= s_firstMismatch + FRAMES_AFTER_MISMATCH)
					break;
			}
			const UnsignedInt frame = TheGameLogic->getFrame();
			if (frame >= s_lastProgress + 1000)
			{
				s_lastProgress = frame - frame % 1000;
				fprintf(stderr, "[GX-NET] replay check: fast-forward at frame %u\n", (unsigned)frame);
				fflush(stderr);
			}
		}

		if (s_autoQuit && s_firstMismatch != 0 && TheGameLogic->getFrame() >= s_firstMismatch + FRAMES_AFTER_MISMATCH)
			finish("mismatch");
	}
	else if (s_sawPlayback)
	{
		// The replay ran out (or the player left it).
		finish(s_firstMismatch != 0 ? "mismatch" : "match");
	}
}

} // namespace GXReplayCheck
