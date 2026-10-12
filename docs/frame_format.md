# Frame format

All multi-byte fields are big endian.

```
byte 0     1     2     3 ........ 3+LEN-1     3+LEN   4+LEN
  +------+-----+-----+--------------------+----------+---------+
  | 0xA5 | LEN | SEQ |      PAYLOAD       | CRC hi   | CRC lo  |
  +------+-----+-----+--------------------+----------+---------+
         \_______ CRC-16 covers these bytes _______/
```

| Field | Size | Meaning |
|---|---|---|
| SYNC | 1 | Always `0xA5`. Start of frame marker. Not covered by the CRC. |
| LEN | 1 | Payload length in bytes, 0 to 32. A larger value is treated as noise. |
| SEQ | 1 | Counter that wraps at 255. Gaps show lost frames. |
| PAYLOAD | LEN | Telemetry, see below. |
| CRC | 2 | CRC-16/CCITT-FALSE over LEN, SEQ and PAYLOAD. |

CRC-16/CCITT-FALSE: polynomial `0x1021`, init `0xFFFF`, no bit reflection, no final xor. Check value for ASCII `123456789` is `0x29B1`.

## Telemetry payload (9 bytes)

| Offset | Type | Field | Unit |
|---|---|---|---|
| 0 | u32 | ts_ms | milliseconds since boot |
| 4 | u16 | vrail_mv | millivolts |
| 6 | i16 | irail_ma | milliamps, 0 if no current sensor |
| 8 | u8 | flags | bit field |

Flags:

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 0 | 0x01 | UV | under-voltage active |
| 1 | 0x02 | OV | over-voltage active |
| 2 | 0x04 | OC | over-current active |
| 3 | 0x08 | CFG_FIXED | an EEPROM vote repaired a config copy in the last 5 s |
| 4 | 0x10 | WDT_RESET | last reset was the watchdog |

A complete telemetry frame is 14 bytes: 1 + 1 + 1 + 9 + 2.

## Example

`a5 09 2a 00 01 e2 40 13 7b 05 dc 00 5b 9f`
is sequence 42, 123456 ms, 4987 mV, 1500 mA, no flags. The test suite checks that the C firmware and the Python code produce identical bytes for this frame.

## Receiver rules

1. Discard bytes until `0xA5`.
2. If LEN is above 32, discard that sync byte and rescan.
3. Wait for `LEN + 5` bytes in total.
4. If the CRC does not match, discard only the sync byte and rescan. Never throw away the whole buffer.
5. If it matches, consume the frame and emit it.
