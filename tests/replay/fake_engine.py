#!/usr/bin/env python3
# BFME1 Mobile QA: a stand-in for the headless engine, so the replay runner and
# the CI job can be tested before the real headless build exists (E1-43).
# It mimics the command line and output of the MYSOREZ fork:
#   fake_engine.py -headless -replay NAME.rep -gxCrcEveryFrame [-gxAutoQuit] [-gxCrcLog PATH]
# and prints "[GX-NET] crc every frame N: XXXXXXXX" (+ "crc parts") on stderr.
# Behaviour is controlled by environment variables:
#   FAKE_FRAMES=N          number of logic frames to simulate (default 600)
#   FAKE_SEED=S            changes every CRC (default: derived from the replay name)
#   FAKE_DIVERGE_AT=F      from frame F on, the "AI" section differs (a desync)
#   FAKE_CRASH_AT=F        die with SIGSEGV-like exit code 139 at frame F
#   FAKE_HANG_AT=F         stop producing output and sleep (for the timeout path)
#   FAKE_SLEEP=S           sleep S seconds (float) before the first frame, then run normally
#   FAKE_LOGIC_HZ=N        value of "# logic_hz=" in the crclog (default 30)
#   FAKE_NO_REP=1          scenario mode: do not write the scenario's own .rep
#   FAKE_WRITE_CWD=NAME    try to create NAME in the cwd (like gx_replay_check_result.txt);
#                          on failure print the OS error; FAKE_WRITE_CWD_FATAL=1: then exit 1;
#                          FAKE_EROFS=1: fail with EROFS like a :ro mount (/gamedata in CI)
# Scenario mode (REPLAY-TESTS §9.1, accepted flags -qaScenario/-qaOut/-seed):
#   fake_engine.py -headless -qaScenario NAME [-seed S] [-qaOut DIR] [-gxCrcLog PATH] [-gxAutoQuit]
# runs without a replay; CRCs depend on NAME and S only; writes DIR/last.rep
# (FAKE_QA_REP=other.rep) with MSG_LOGIC_CRC records every 100 frames under label f+1
# and a wall-clock timestamp in the header (so its SHA-256 changes every run, like a real .rep).
# Like the engine (CommandLine.cpp parseReplay) it exits with 1 if the replay name does
# not end with ".rep"; like engine-hooks/0001 it writes -gxCrcLog PATH to
# "<stem>.<replay>.<pid><ext>" (one file per -jobs worker).
import errno
import os
import re
import struct
import sys
import time
import zlib


MSG_LOGIC_CRC = 1095   # same as crclog.MSG_LOGIC_CRC (Zero Hour)


def write_rep(path: str, crcs: dict[int, int]) -> None:
    hdr = b"GENREP" + f"T={time.time_ns():020d}".encode().ljust(40, b"\0") + b"S=X;;\0" + b"0\0"
    recs = b""
    for f in sorted(crcs):
        recs += struct.pack("<iiiB", f, 1001, 0, 1) + bytes([0, 1]) + struct.pack("<i", 5)  # some command
        recs += struct.pack("<iiiB", f + 1, MSG_LOGIC_CRC, 0, 2) + bytes([0, 1, 2, 1]) \
            + struct.pack("<I", crcs[f]) + b"\x01"
    with open(path, "wb") as fh:
        fh.write(hdr + recs)


def main(argv: list[str]) -> int:
    args = argv[1:]
    scenario = args[args.index("-qaScenario") + 1] if "-qaScenario" in args else None
    qa_out = args[args.index("-qaOut") + 1] if "-qaOut" in args else None
    scen_seed = args[args.index("-seed") + 1] if "-seed" in args else "0"
    if scenario and "-replay" not in args:
        replay = f"{scenario}.rep"
    else:
        replay = args[args.index("-replay") + 1] if "-replay" in args else "none.rep"
    crclog = args[args.index("-gxCrcLog") + 1] if "-gxCrcLog" in args else None
    every = "-gxCrcEveryFrame" in args or crclog is not None
    frames = int(os.environ.get("FAKE_FRAMES", "600"))
    seed_src = f"{scenario}:{scen_seed}" if scenario else os.path.basename(replay)
    seed = int(os.environ.get("FAKE_SEED", str(zlib.crc32(seed_src.encode()))))
    if os.environ.get("FAKE_WRITE_CWD"):
        try:
            if os.environ.get("FAKE_EROFS") == "1":
                raise OSError(errno.EROFS, os.strerror(errno.EROFS))
            with open(os.environ["FAKE_WRITE_CWD"], "w", encoding="utf-8") as fh:
                fh.write("result\n")
        except OSError as e:
            print(f"[GX] cannot open {os.path.abspath(os.environ['FAKE_WRITE_CWD'])} for writing: "
                  f"{e.strerror} (errno {e.errno})", flush=True)
            if os.environ.get("FAKE_WRITE_CWD_FATAL") == "1":
                return 1
    diverge = int(os.environ.get("FAKE_DIVERGE_AT", "-1"))
    crash = int(os.environ.get("FAKE_CRASH_AT", "-1"))
    hang = int(os.environ.get("FAKE_HANG_AT", "-1"))
    if float(os.environ.get("FAKE_SLEEP", "0")) > 0:
        time.sleep(float(os.environ["FAKE_SLEEP"]))

    if not replay.lower().endswith(".rep"):
        print(f'Invalid replay name "{replay}"', flush=True)
        return 1
    print(f'Running scenario "{scenario}" seed {scen_seed}' if scenario else f'Simulating Replay "{replay}"',
          flush=True)
    out = None
    if crclog:
        stem, ext = os.path.splitext(crclog)
        name = re.sub(r"[^A-Za-z0-9_.-]", "_", os.path.basename(replay))
        name = name[:-4] if name.lower().endswith(".rep") else name
        out = open(f"{stem}.{name}.{os.getpid()}{ext}", "w", encoding="utf-8")
        out.write("# bfme-crclog/1\n# source=fake_engine\n")
        out.write(f"# logic_hz={os.environ.get('FAKE_LOGIC_HZ', '30')}\n")
        out.write(f"# replay={replay}\n# interval=1\n")
    state = seed & 0xFFFFFFFF
    checkpoints: dict[int, int] = {}
    for f in range(frames):
        if f == crash:
            sys.stderr.write(f"[GX] fatal signal 11 at frame {f}\n")
            sys.stderr.flush()
            os._exit(139)
        if f == hang:
            time.sleep(3600)
        parts = {}
        acc = state
        for i, name in enumerate(("Objects", "Partition", "PlayerList", "AI")):
            salt = 1 if (name == "AI" and diverge >= 0 and f >= diverge) else 0
            acc = zlib.crc32(f"{name}:{f}:{salt}".encode(), acc) & 0xFFFFFFFF
            parts[name] = acc
        crc = acc
        state = crc
        if f % 100 == 0:
            checkpoints[f] = crc
        if every:
            sys.stderr.write(f"[GX-NET] crc parts frame {f}: afterObjects={parts['Objects']:08X} seed={seed & 0xFFFFFFFF:08X}\n")
            sys.stderr.write(f"[GX-NET] crc parts frame {f}: afterPartition={parts['Partition']:08X}\n")
            sys.stderr.write(f"[GX-NET] crc parts frame {f}: afterPlayerList={parts['PlayerList']:08X}\n")
            sys.stderr.write(f"[GX-NET] crc parts frame {f}: afterAI={parts['AI']:08X}  (objects counted: 0)\n")
            sys.stderr.write(f"[GX-NET] crc every frame {f}: {crc:08X}\n")
        if out:
            out.write(f"{f} {crc:08X} " + " ".join(f"{k}={v:08X}" for k, v in parts.items()) + "\n")
    status = "match"
    sys.stderr.write(f"[GX-NET] replay check {os.path.basename(replay)}: {status}, {frames} frames, 0/0 checkpoints matched\n")
    if out:
        out.write(f"# end frames={frames} status=ok\n")
        out.close()
    if scenario and qa_out and os.environ.get("FAKE_NO_REP") != "1":
        os.makedirs(qa_out, exist_ok=True)
        write_rep(os.path.join(qa_out, os.environ.get("FAKE_QA_REP", "last.rep")), checkpoints)
    print("Simulation of all replays completed. Errors occurred: 0", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
