"""Frame encode/decode that mirrors firmware/rail_monitor/src/frame.c.

Frame (big endian): 0xA5 | LEN | SEQ | PAYLOAD[LEN] | CRC16
CRC-16/CCITT-FALSE over LEN, SEQ and PAYLOAD.
Telemetry payload (9 bytes): ts_ms u32, vrail_mv u16, irail_ma i16, flags u8.
"""

import struct
from dataclasses import dataclass

SYNC = 0xA5
MAX_PAYLOAD = 32
TELEM_LEN = 9
OVERHEAD = 5

FLAG_UV = 0x01
FLAG_OV = 0x02
FLAG_OC = 0x04
FLAG_CFG_FIXED = 0x08
FLAG_WDT_RESET = 0x10

FLAG_NAMES = (
    (FLAG_UV, "UV"),
    (FLAG_OV, "OV"),
    (FLAG_OC, "OC"),
    (FLAG_CFG_FIXED, "CFG_FIXED"),
    (FLAG_WDT_RESET, "WDT_RESET"),
)


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode(seq: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    body = bytes([len(payload), seq & 0xFF]) + payload
    return bytes([SYNC]) + body + struct.pack(">H", crc16(body))


def pack_telem(ts_ms: int, vrail_mv: int, irail_ma: int, flags: int) -> bytes:
    return struct.pack(">IHhB", ts_ms & 0xFFFFFFFF, vrail_mv, irail_ma, flags)


@dataclass
class Telem:
    seq: int
    ts_ms: int
    vrail_mv: int
    irail_ma: int
    flags: int

    def flag_text(self) -> str:
        names = [name for bit, name in FLAG_NAMES if self.flags & bit]
        return "|".join(names) if names else "OK"


def unpack_telem(seq: int, payload: bytes) -> Telem:
    ts, mv, ma, flags = struct.unpack(">IHhB", payload)
    return Telem(seq, ts, mv, ma, flags)


class FrameParser:
    """Byte-stream parser. Feed it whatever bytes arrive, get good frames out.

    On a CRC failure it drops only the sync byte it locked onto and rescans, so
    one corrupted frame costs at most that frame, not the rest of the stream.
    """

    def __init__(self, check_crc: bool = True):
        self.buf = bytearray()
        self.check_crc = check_crc
        self.good = 0
        self.crc_errors = 0
        self.resyncs = 0     # bytes thrown away while hunting for a sync byte

    def feed(self, data: bytes):
        self.buf.extend(data)
        out = []
        while True:
            # hunt for sync
            while self.buf and self.buf[0] != SYNC:
                del self.buf[0]
                self.resyncs += 1
            if len(self.buf) < 3:
                break
            length = self.buf[1]
            if length > MAX_PAYLOAD:
                del self.buf[0]
                self.resyncs += 1
                continue
            total = length + OVERHEAD
            if len(self.buf) < total:
                break
            body = bytes(self.buf[1:3 + length])
            rx_crc = (self.buf[3 + length] << 8) | self.buf[4 + length]
            if self.check_crc and crc16(body) != rx_crc:
                self.crc_errors += 1
                del self.buf[0]
                continue
            seq = self.buf[2]
            payload = bytes(self.buf[3:3 + length])
            del self.buf[:total]
            self.good += 1
            out.append((seq, payload))
        return out
