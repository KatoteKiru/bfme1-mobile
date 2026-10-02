# BFME1 Mobile: parser checks for bfme-crclog/1. No third-party dependencies.
# Run: python3 -m unittest test_crclog   (from tests/replay)
from __future__ import annotations

import unittest

import crclog

# Same shape the engine writes (GXReplayCheck.cpp). frames in the footer is
# the last written frame + 1.
SAMPLE = """\
# bfme-crclog/1
# source=engine
# interval=1
# logic_hz=30
# replay=battle.rep
# ini_crc=00000001
# exe_crc=00000002
0 ABCDEF01 Objects=00000005 Partition=00000006 PlayerList=00000007 AI=00000008
1 00000009 Objects=0000000A Partition=0000000B PlayerList=0000000C AI=0000000D
# end frames=2 status=exit
"""


class ParseCrclog(unittest.TestCase):
    def test_engine_sample(self):
        log = crclog.parse_crclog(SAMPLE)
        self.assertEqual(log.logic_hz, 30)
        self.assertEqual(log.meta["replay"], "battle.rep")
        self.assertEqual(log.meta["ini_crc"], "00000001")
        self.assertEqual(log.first_frame, 0)
        self.assertEqual(log.last_frame, 1)
        self.assertEqual(log.frames[0].crc, 0xABCDEF01)
        self.assertEqual(log.frames[0].parts["Objects"], 0x5)
        self.assertEqual(log.frames[0].parts["AI"], 0x8)
        self.assertEqual(log.frames[1].parts["Partition"], 0xB)
        self.assertEqual(log.end, {"frames": "2", "status": "exit"})

    def test_round_trip_keeps_logic_hz_first(self):
        text = crclog.format_crclog(crclog.parse_crclog(SAMPLE))
        lines = text.splitlines()
        self.assertEqual(lines[0], "# bfme-crclog/1")
        self.assertEqual(lines[1], "# logic_hz=30")
        again = crclog.parse_crclog(text)
        self.assertEqual(again.frames[1].crc, 0x9)
        self.assertEqual(again.logic_hz, 30)

    def test_compare_finds_first_section(self):
        expected = crclog.parse_crclog(SAMPLE)
        # Frame CRC differs, and Partition is the first section whose running CRC differs.
        actual = crclog.parse_crclog(
            SAMPLE.replace("1 00000009 ", "1 FFFFFFFF ", 1).replace("0000000B", "FFFFFFFF", 1))
        result = crclog.compare(expected, actual)
        self.assertFalse(result.ok)
        self.assertEqual(result.divergence.frame, 1)
        self.assertEqual(result.divergence.kind, "crc")
        self.assertEqual(result.divergence.first_section, "Partition")
        self.assertEqual(result.divergence.last_match, 0)

    def test_logic_hz_mismatch_is_not_a_frame_compare(self):
        expected = crclog.parse_crclog(SAMPLE)
        actual = crclog.parse_crclog(SAMPLE.replace("logic_hz=30", "logic_hz=60"))
        result = crclog.compare(expected, actual)
        self.assertEqual(result.divergence.kind, "logic_hz")
        self.assertEqual(result.compared, 0)


if __name__ == "__main__":
    unittest.main()
