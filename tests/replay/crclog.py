#!/usr/bin/env python3
# BFME1 Mobile: replay CRC logs (QA, tests/replay). GPL v3, part of bfme-mobile.
#
# Reads per-logic-frame CRC sequences from three sources and compares them:
#   1. "bfme-crclog/1" files: our own machine-readable format (golden files and the
#      engine option -gxCrcLog, GXReplayCheck.cpp). The engine writes "# logic_hz=N"
#      from LOGICFRAMES_PER_SECOND (30 unless the 60 Hz build is on).
#   2. Engine stderr of the MYSOREZ fork with -gxCrcEveryFrame and GX_NET_TRACE=1:
#        [GX-NET] crc every frame N: XXXXXXXX
#        [GX-NET] crc parts frame N: afterObjects=XXXXXXXX seed=XXXXXXXX
#        [GX-NET] crc parts frame N: afterPartition=XXXXXXXX  (etc.)
#      (GameLogic.cpp / GXReplayCheck.h in src/GeneralsZH-Android-Port @ 8ef2349).
#   3. The CRCs a SAGE replay (.rep) recorded itself (MSG_LOGIC_CRC, every C frames,
#      usually 100), see rep_recorded_crcs(); the frame label in the replay is shifted
#      against the logic frame the CRC was computed on (frame_offset, default 1 [проверить]).
#
# No third-party dependencies; Python 3.11+.
from __future__ import annotations

import dataclasses
import re
import struct
from pathlib import Path
from typing import Iterable

FORMAT_TAG = "bfme-crclog/1"

# Section names, in the order GameLogic::getCRC() accumulates them. The fork's
# "crc parts" values are RUNNING CRCs after each section, so the first section
# whose running value differs is where the state first differs.
SECTIONS = ("Objects", "Partition", "PlayerList", "AI")


@dataclasses.dataclass
class FrameCRC:
    frame: int
    crc: int
    parts: dict[str, int] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass
class CRCLog:
    meta: dict[str, str] = dataclasses.field(default_factory=dict)
    frames: dict[int, FrameCRC] = dataclasses.field(default_factory=dict)
    end: dict[str, str] | None = None  # footer: frames=N status=ok|mismatch|...

    @property
    def logic_hz(self) -> int | None:
        """Logic tick rate from the "# logic_hz=N" header line (the replay itself does not store it)."""
        v = self.meta.get("logic_hz")
        return int(v) if v not in (None, "") else None

    @property
    def first_frame(self) -> int | None:
        return min(self.frames) if self.frames else None

    @property
    def last_frame(self) -> int | None:
        return max(self.frames) if self.frames else None

    def add(self, frame: int, crc: int | None = None, parts: dict[str, int] | None = None) -> None:
        fc = self.frames.get(frame)
        if fc is None:
            fc = FrameCRC(frame, crc if crc is not None else -1)
            self.frames[frame] = fc
        elif crc is not None:
            fc.crc = crc
        if parts:
            fc.parts.update(parts)


# ---------------------------------------------------------------------------
# bfme-crclog/1
#
#   # bfme-crclog/1
#   # logic_hz=30          (logic ticks per second; 30 for BFME1 Mobile, Лид 02.10.2026)
#   # key=value            (replay, replay_sha256, engine_build_id, arch, ini_crc, ...)
#   <frame> <crc:8 hex> [Objects=<8 hex> Partition=<8 hex> PlayerList=<8 hex> AI=<8 hex>]
#   # end frames=<N> status=<ok|mismatch|crash|timeout>
# ---------------------------------------------------------------------------
_LINE = re.compile(r"^(\d+)\s+([0-9A-Fa-f]{8})((?:\s+\w+=[0-9A-Fa-f]{8})*)\s*$")
_KV = re.compile(r"(\w+)=([0-9A-Fa-f]{8})")


def parse_crclog(text: str) -> CRCLog:
    log = CRCLog()
    lines = text.splitlines()
    if not lines or lines[0].strip() != f"# {FORMAT_TAG}":
        raise ValueError(f"not a {FORMAT_TAG} file (first line: {lines[0] if lines else ''!r})")
    for raw in lines[1:]:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            body = line[1:].strip()
            if body.startswith("end"):
                log.end = dict(kv.split("=", 1) for kv in body[3:].split() if "=" in kv)
            elif "=" in body:
                k, v = body.split("=", 1)
                log.meta[k.strip()] = v.strip()
            continue
        m = _LINE.match(line)
        if not m:
            raise ValueError(f"bad crclog line: {raw!r}")
        parts = {k: int(v, 16) for k, v in _KV.findall(m.group(3) or "")}
        log.add(int(m.group(1)), int(m.group(2), 16), parts)
    return log


def format_crclog(log: CRCLog) -> str:
    out = [f"# {FORMAT_TAG}"]
    if "logic_hz" in log.meta:  # right after the tag: needed to read frame numbers as time
        out.append(f"# logic_hz={log.meta['logic_hz']}")
    for k in sorted(log.meta):
        if k != "logic_hz":
            out.append(f"# {k}={log.meta[k]}")
    for f in sorted(log.frames):
        fc = log.frames[f]
        line = f"{f} {fc.crc & 0xFFFFFFFF:08X}"
        for s in SECTIONS:
            if s in fc.parts:
                line += f" {s}={fc.parts[s]:08X}"
        out.append(line)
    if log.end is not None:
        out.append("# end " + " ".join(f"{k}={v}" for k, v in log.end.items()))
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Fork stderr (generals-stderr.log or captured stdout/stderr of a headless run)
# ---------------------------------------------------------------------------
_GX_EVERY = re.compile(r"crc every frame (\d+): ([0-9A-Fa-f]{8})")
_GX_PARTS = re.compile(r"crc parts frame (\d+): after(\w+)=([0-9A-Fa-f]{8})")
_GX_RESULT = re.compile(r"replay check .*?: (\w+), (\d+) frames")


def parse_gx_stderr(text: str) -> CRCLog:
    log = CRCLog(meta={"source": "gx-stderr"})
    pending_parts: dict[int, dict[str, int]] = {}
    for line in text.splitlines():
        m = _GX_EVERY.search(line)
        if m:
            frame = int(m.group(1))
            log.add(frame, int(m.group(2), 16), pending_parts.pop(frame, None))
            continue
        m = _GX_PARTS.search(line)
        if m:
            frame, sect, val = int(m.group(1)), m.group(2), int(m.group(3), 16)
            # parts are printed while the CRC is being computed, i.e. BEFORE the
            # "crc every frame" line of the same frame.
            pending_parts.setdefault(frame, {})[sect] = val
            continue
        m = _GX_RESULT.search(line)
        if m:
            log.end = {"status": m.group(1), "frames": m.group(2)}
    for frame, parts in pending_parts.items():
        if frame in log.frames:
            log.frames[frame].parts.update(parts)
    return log


def load_any(path: Path, rep_frame_offset: int = 1) -> CRCLog:
    """Load a crclog, a fork stderr log or a .rep (recorded CRCs only, see rep_recorded_crcs)."""
    if path.suffix.lower() == ".rep":
        return rep_recorded_crcs(path.read_bytes(), frame_offset=rep_frame_offset)
    text = path.read_text(encoding="utf-8", errors="replace")
    if text.startswith(f"# {FORMAT_TAG}"):
        return parse_crclog(text)
    return parse_gx_stderr(text)


# ---------------------------------------------------------------------------
# Recorded CRCs inside a SAGE replay (.rep)
#
# The command stream after the header is a list of records:
#   int32 frame, int32 msgType, int32 playerIndex, uint8 nArgTypes,
#   nArgTypes x (uint8 type, uint8 count), then the arguments.
# MSG_LOGIC_CRC (1095 in Zero Hour) carries (int crc, bool isPlayback).
# Header layout: GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp
# (startRecording / readReplayHeader). Locating the stream start by a
# plausibility scan is the same approach as the fork's
# scripts/tooling/replay/rep_crc_every_frame.py.
# TODO(QA, Э2): BFME1-mode replays may get a different header (our own Recorder
# changes); keep this function in sync with Recorder.cpp of the bfme branch.
# Frame offset: the ZH test replays carry MSG_LOGIC_CRC at frames 1, 101, 201, ...
# Hypothesis (to be confirmed on the first headless run, Лид 02.10.2026): the CRC is
# computed at the end of logic frame N and recorded under frame N+1 (after the frame
# increment) -- upstream behaviour, so the comparison is shifted here, not the engine.
# frame_offset=K: recorded label L is compared with engine frame L-K.
# ---------------------------------------------------------------------------
MSG_LOGIC_CRC = 1095
_ARG_SIZES = {0: 4, 1: 4, 2: 1, 3: 4, 4: 4, 5: 4, 6: 12, 7: 8, 8: 16, 9: 4, 10: 2}


def _parse_records(data: bytes, off: int, limit: int | None = None) -> list[tuple[int, int, int, int, int]] | None:
    recs = []
    p, last = off, -1
    n = len(data)
    while p < n:
        if p + 13 > n:
            return None
        start = p
        frame, mtype, player = struct.unpack_from("<iii", data, p)
        p += 12
        if frame < last or frame > 10**7 or not (0 <= mtype <= 5000):
            return None
        ntypes = data[p]
        p += 1
        if ntypes > 12:
            return None
        types = []
        for _ in range(ntypes):
            if p + 2 > n:
                return None
            t, c = data[p], data[p + 1]
            p += 2
            if t not in _ARG_SIZES:
                return None
            types.append((t, c))
        args_off = p
        for t, c in types:
            p += _ARG_SIZES[t] * c
        if p > n:
            return None
        recs.append((frame, mtype, player, args_off, start))
        last = frame
        if limit is not None and len(recs) >= limit:
            return recs
    return recs


def rep_recorded_crcs(data: bytes, player: int | None = None, frame_offset: int = 1) -> CRCLog:
    if not data.startswith(b"GENREP"):
        raise ValueError("not a SAGE replay (no GENREP signature)")
    base = data.find(b"S=")
    if base < 0:
        raise ValueError("replay has no slot list (S=)")
    recs = None
    for off in range(base, min(base + 8000, len(data))):
        probe = _parse_records(data, off, limit=6)
        if probe and len(probe) >= 6:
            recs = _parse_records(data, off)
            if recs:
                break
    if not recs:
        raise ValueError("could not locate the command stream")
    log = CRCLog(meta={"source": "rep-recorded", "frame_offset": str(frame_offset)})
    for frame, mtype, pl, args_off, _ in recs:
        if mtype != MSG_LOGIC_CRC or (player is not None and pl != player):
            continue
        (crc,) = struct.unpack_from("<I", data, args_off)
        log.add(frame - frame_offset, crc)
    return log


def shifted(log: CRCLog, delta: int) -> CRCLog:
    """Copy of `log` with every frame number moved by `delta`."""
    out = CRCLog(meta=dict(log.meta), end=dict(log.end) if log.end else None)
    for f, fc in log.frames.items():
        out.add(f + delta, fc.crc, dict(fc.parts))
    return out


# ---------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class Divergence:
    frame: int
    expected: int | None
    actual: int | None
    first_section: str | None
    last_match: int | None
    kind: str  # "crc" | "missing_in_actual" | "extra_in_actual" | "logic_hz"


@dataclasses.dataclass
class CompareResult:
    compared: int
    divergence: Divergence | None
    expected_last: int | None
    actual_last: int | None

    @property
    def ok(self) -> bool:
        return self.divergence is None


def compare(expected: CRCLog, actual: CRCLog, frames: Iterable[int] | None = None,
            allow_actual_longer: bool = True) -> CompareResult:
    """First frame where `actual` disagrees with `expected` (golden)."""
    if expected.logic_hz is not None and actual.logic_hz is not None and expected.logic_hz != actual.logic_hz:
        # different tick rates: frame N is not the same moment of the game, nothing to compare
        return CompareResult(0, Divergence(-1, expected.logic_hz, actual.logic_hz, None, None, "logic_hz"),
                             expected.last_frame, actual.last_frame)
    keys = sorted(frames) if frames is not None else sorted(expected.frames)
    last_match = None
    compared = 0
    for f in keys:
        e = expected.frames.get(f)
        a = actual.frames.get(f)
        if e is None:
            continue
        if a is None:
            return CompareResult(compared, Divergence(f, e.crc, None, None, last_match, "missing_in_actual"),
                                 expected.last_frame, actual.last_frame)
        compared += 1
        if (e.crc & 0xFFFFFFFF) != (a.crc & 0xFFFFFFFF):
            section = None
            for s in SECTIONS:
                if s in e.parts and s in a.parts and e.parts[s] != a.parts[s]:
                    section = s
                    break
            return CompareResult(compared, Divergence(f, e.crc, a.crc, section, last_match, "crc"),
                                 expected.last_frame, actual.last_frame)
        last_match = f
    if not allow_actual_longer and actual.last_frame is not None and expected.last_frame is not None \
            and actual.last_frame > expected.last_frame:
        f = min(x for x in actual.frames if x > expected.last_frame)
        return CompareResult(compared, Divergence(f, None, actual.frames[f].crc, None, last_match, "extra_in_actual"),
                             expected.last_frame, actual.last_frame)
    return CompareResult(compared, None, expected.last_frame, actual.last_frame)
