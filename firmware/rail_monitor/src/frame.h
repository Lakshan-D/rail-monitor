/* Telemetry frame, all multi-byte fields big endian:
 *
 *   +------+-----+-----+----------------+-----------+
 *   | 0xA5 | LEN | SEQ | PAYLOAD (LEN)  | CRC16 (2) |
 *   +------+-----+-----+----------------+-----------+
 *                 \_________ CRC covers LEN..PAYLOAD _/
 *
 * Telemetry payload (9 bytes):
 *   ts_ms    u32   milliseconds since boot
 *   vrail_mv u16   rail voltage in millivolts
 *   irail_ma i16   rail current in milliamps (0 if no current sensor)
 *   flags    u8    RM_FLAG_* bits
 *
 * A full frame is 1 + 1 + 1 + 9 + 2 = 14 bytes.
 */
#ifndef RM_FRAME_H
#define RM_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RM_SYNC            0xA5u
#define RM_MAX_PAYLOAD     32u
#define RM_TELEM_LEN       9u
#define RM_FRAME_OVERHEAD  5u   /* sync + len + seq + crc16 */
#define RM_MAX_FRAME       (RM_MAX_PAYLOAD + RM_FRAME_OVERHEAD)

#define RM_FLAG_UV         0x01u  /* under-voltage active           */
#define RM_FLAG_OV         0x02u  /* over-voltage active            */
#define RM_FLAG_OC         0x04u  /* over-current active            */
#define RM_FLAG_CFG_FIXED  0x08u  /* EEPROM vote corrected a copy   */
#define RM_FLAG_WDT_RESET  0x10u  /* last reset was the watchdog    */

/* Pack the telemetry payload. out must hold RM_TELEM_LEN bytes. */
void rm_telem_pack(uint8_t *out, uint32_t ts_ms, uint16_t vrail_mv,
                   int16_t irail_ma, uint8_t flags);

/* Build a complete frame. Returns total bytes written, or 0 if payload_len is
 * larger than RM_MAX_PAYLOAD. out must hold payload_len + RM_FRAME_OVERHEAD. */
size_t rm_frame_encode(uint8_t *out, uint8_t seq,
                       const uint8_t *payload, uint8_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* RM_FRAME_H */
