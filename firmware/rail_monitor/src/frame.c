#include "frame.h"
#include "crc16.h"

void rm_telem_pack(uint8_t *out, uint32_t ts_ms, uint16_t vrail_mv,
                   int16_t irail_ma, uint8_t flags)
{
    uint16_t i = (uint16_t)irail_ma;

    out[0] = (uint8_t)(ts_ms >> 24);
    out[1] = (uint8_t)(ts_ms >> 16);
    out[2] = (uint8_t)(ts_ms >> 8);
    out[3] = (uint8_t)(ts_ms);
    out[4] = (uint8_t)(vrail_mv >> 8);
    out[5] = (uint8_t)(vrail_mv);
    out[6] = (uint8_t)(i >> 8);
    out[7] = (uint8_t)(i);
    out[8] = flags;
}

size_t rm_frame_encode(uint8_t *out, uint8_t seq,
                       const uint8_t *payload, uint8_t payload_len)
{
    if (payload_len > RM_MAX_PAYLOAD) {
        return 0;
    }

    out[0] = RM_SYNC;
    out[1] = payload_len;
    out[2] = seq;
    for (uint8_t i = 0; i < payload_len; i++) {
        out[3 + i] = payload[i];
    }

    /* CRC covers LEN, SEQ and PAYLOAD, not the sync byte. */
    uint16_t crc = rm_crc16(&out[1], (size_t)payload_len + 2u);
    out[3 + payload_len] = (uint8_t)(crc >> 8);
    out[4 + payload_len] = (uint8_t)(crc);

    return (size_t)payload_len + RM_FRAME_OVERHEAD;
}
