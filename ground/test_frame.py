"""Python tests. Run: python3 -m unittest discover -s ground -v"""

import os
import unittest

import frame

HERE = os.path.dirname(os.path.abspath(__file__))
C_HEX = os.path.join(HERE, "..", "firmware", "tests", "c_frame.hex")


class TestFrame(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(frame.crc16(b"123456789"), 0x29B1)

    def test_roundtrip(self):
        payload = frame.pack_telem(1000, 5000, -250, frame.FLAG_UV)
        raw = frame.encode(3, payload)
        self.assertEqual(len(raw), 14)
        p = frame.FrameParser()
        out = p.feed(raw)
        self.assertEqual(len(out), 1)
        t = frame.unpack_telem(*out[0])
        self.assertEqual((t.seq, t.ts_ms, t.vrail_mv, t.irail_ma), (3, 1000, 5000, -250))
        self.assertEqual(t.flag_text(), "UV")

    def test_split_across_reads(self):
        raw = frame.encode(1, frame.pack_telem(1, 5000, 0, 0))
        p = frame.FrameParser()
        got = []
        for b in raw:
            got += p.feed(bytes([b]))
        self.assertEqual(len(got), 1)

    def test_garbage_before_frame(self):
        raw = b"\x00\x13\x99" + frame.encode(1, frame.pack_telem(1, 5000, 0, 0))
        p = frame.FrameParser()
        self.assertEqual(len(p.feed(raw)), 1)

    def test_every_single_bit_flip_detected(self):
        raw = frame.encode(9, frame.pack_telem(555, 4900, 100, 0))
        for byte in range(1, len(raw)):
            for bit in range(8):
                bad = bytearray(raw)
                bad[byte] ^= 1 << bit
                p = frame.FrameParser()
                out = p.feed(bytes(bad))
                self.assertEqual(out, [], f"flip byte {byte} bit {bit} slipped through")

    def test_recovers_after_bad_frame(self):
        f1 = bytearray(frame.encode(1, frame.pack_telem(1, 5000, 0, 0)))
        f1[6] ^= 0x10
        f2 = frame.encode(2, frame.pack_telem(2, 5001, 0, 0))
        p = frame.FrameParser()
        out = p.feed(bytes(f1) + f2)
        self.assertEqual([s for s, _ in out], [2])
        self.assertEqual(p.crc_errors, 1)

    @unittest.skipUnless(os.path.exists(C_HEX), "run ./run_tests.sh first to build the C test")
    def test_matches_c_firmware(self):
        with open(C_HEX) as f:
            c_bytes = bytes.fromhex(f.read().strip())
        py_bytes = frame.encode(42, frame.pack_telem(123456, 4987, 1500, 0))
        self.assertEqual(c_bytes, py_bytes)


if __name__ == "__main__":
    unittest.main()
