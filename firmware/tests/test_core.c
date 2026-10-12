/* Host-side tests for the portable C core (no Arduino needed).
 * Build and run with: ./run_tests.sh   (from the repo root)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rail_monitor/src/crc16.h"
#include "../rail_monitor/src/frame.h"
#include "../rail_monitor/src/fault.h"
#include "../rail_monitor/src/vote.h"

static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { \
    printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

static int test_crc(void)
{
    const uint8_t msg[] = "123456789";
    CHECK(rm_crc16(msg, 9) == 0x29B1u);       /* CRC-16/CCITT-FALSE check value */
    CHECK(rm_crc16(msg, 0) == 0xFFFFu);       /* empty input = init value */
    return 0;
}

static int test_frame(void)
{
    uint8_t payload[RM_TELEM_LEN];
    uint8_t frame[RM_MAX_FRAME];

    rm_telem_pack(payload, 0x01020304u, 5000u, -250, RM_FLAG_UV | RM_FLAG_OC);
    CHECK(payload[0] == 0x01 && payload[3] == 0x04);
    CHECK(payload[4] == 0x13 && payload[5] == 0x88);   /* 5000 = 0x1388 */
    CHECK(payload[6] == 0xFF && payload[7] == 0x06);   /* -250 = 0xFF06 */
    CHECK(payload[8] == 0x05);

    size_t n = rm_frame_encode(frame, 7u, payload, RM_TELEM_LEN);
    CHECK(n == 14u);
    CHECK(frame[0] == RM_SYNC && frame[1] == RM_TELEM_LEN && frame[2] == 7u);

    /* CRC over LEN..PAYLOAD must equal the trailing two bytes */
    uint16_t crc = rm_crc16(&frame[1], 2u + RM_TELEM_LEN);
    CHECK(frame[12] == (uint8_t)(crc >> 8) && frame[13] == (uint8_t)crc);

    /* any single bit flip anywhere after the sync byte must change the CRC check */
    for (size_t byte = 1; byte < n; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t bad[RM_MAX_FRAME];
            memcpy(bad, frame, n);
            bad[byte] ^= (uint8_t)(1u << bit);
            if (byte < n - 2) {
                CHECK(rm_crc16(&bad[1], 2u + RM_TELEM_LEN) != crc);
            }
        }
    }

    CHECK(rm_frame_encode(frame, 0, payload, RM_MAX_PAYLOAD + 1) == 0u);

    /* machine readable line for the Python cross-check */
    rm_telem_pack(payload, 123456u, 4987u, 1500, 0);
    n = rm_frame_encode(frame, 42u, payload, RM_TELEM_LEN);
    FILE *f = fopen("c_frame.hex", "w");
    CHECK(f != NULL);
    for (size_t i = 0; i < n; i++) {
        fprintf(f, "%02x", frame[i]);
    }
    fprintf(f, "\n");
    fclose(f);
    return 0;
}

static int test_vote(void)
{
    CHECK(rm_vote8(0xFF, 0xFF, 0x00) == 0xFF);
    CHECK(rm_vote8(0xAA, 0xAA, 0x55) == 0xAA);
    CHECK(rm_vote8(0x12, 0x12, 0x12) == 0x12);

    /* every single-bit flip in one copy, in every byte, is corrected */
    uint8_t good[RM_CFG_BYTES] = {0x11, 0x94, 0x15, 0x7C, 0x0B, 0xB8, 0x05};
    for (size_t byte = 0; byte < RM_CFG_BYTES; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            for (int copy = 0; copy < 3; copy++) {
                uint8_t c[3][RM_CFG_BYTES], out[RM_CFG_BYTES];
                memcpy(c[0], good, RM_CFG_BYTES);
                memcpy(c[1], good, RM_CFG_BYTES);
                memcpy(c[2], good, RM_CFG_BYTES);
                c[copy][byte] ^= (uint8_t)(1u << bit);
                size_t d = rm_vote_buf(c[0], c[1], c[2], out, RM_CFG_BYTES);
                CHECK(d == 1u);
                CHECK(memcmp(out, good, RM_CFG_BYTES) == 0);
            }
        }
    }

    /* limit: same bit flipped in two copies CANNOT be corrected */
    uint8_t a[RM_CFG_BYTES], b[RM_CFG_BYTES], c[RM_CFG_BYTES], out[RM_CFG_BYTES];
    memcpy(a, good, RM_CFG_BYTES);
    memcpy(b, good, RM_CFG_BYTES);
    memcpy(c, good, RM_CFG_BYTES);
    a[0] ^= 0x01;
    b[0] ^= 0x01;
    rm_vote_buf(a, b, c, out, RM_CFG_BYTES);
    CHECK(out[0] != good[0]);

    /* clean copies report zero disagreement */
    CHECK(rm_vote_buf(good, good, good, out, RM_CFG_BYTES) == 0u);
    return 0;
}

static int test_cfg(void)
{
    rm_cfg_t cfg, back;
    uint8_t raw[RM_CFG_BYTES];

    rm_cfg_default(&cfg);
    CHECK(rm_cfg_valid(&cfg));
    rm_cfg_to_bytes(&cfg, raw);
    rm_cfg_from_bytes(&back, raw);
    CHECK(back.uv_mv == cfg.uv_mv && back.ov_mv == cfg.ov_mv);
    CHECK(back.oc_ma == cfg.oc_ma && back.debounce == cfg.debounce);

    back.debounce = 0;
    CHECK(!rm_cfg_valid(&back));
    back.debounce = 5;
    back.uv_mv = 6000;
    CHECK(!rm_cfg_valid(&back));
    return 0;
}

static int test_fault(void)
{
    rm_cfg_t cfg;
    rm_fault_t f;
    uint8_t flags = 0;

    rm_cfg_default(&cfg);      /* debounce = 5 */
    rm_fault_init(&f);

    /* healthy rail: no flags */
    for (int i = 0; i < 20; i++) {
        flags = rm_fault_update(&f, &cfg, 5000, 500);
        CHECK(flags == 0);
    }

    /* one glitch sample: ignored */
    flags = rm_fault_update(&f, &cfg, 3000, 500);
    CHECK(flags == 0);
    flags = rm_fault_update(&f, &cfg, 5000, 500);
    CHECK(flags == 0);

    /* 4 low samples: still not a fault, 5th sets it */
    for (int i = 0; i < 4; i++) {
        flags = rm_fault_update(&f, &cfg, 4000, 500);
        CHECK(flags == 0);
    }
    flags = rm_fault_update(&f, &cfg, 4000, 500);
    CHECK(flags == RM_FLAG_UV);

    /* recovery needs 5 good samples in a row */
    for (int i = 0; i < 4; i++) {
        flags = rm_fault_update(&f, &cfg, 5000, 500);
        CHECK(flags == RM_FLAG_UV);
    }
    flags = rm_fault_update(&f, &cfg, 5000, 500);
    CHECK(flags == 0);

    /* over-voltage and over-current */
    for (int i = 0; i < 5; i++) {
        flags = rm_fault_update(&f, &cfg, 5800, 3500);
    }
    CHECK(flags == (RM_FLAG_OV | RM_FLAG_OC));
    return 0;
}

int main(void)
{
    if (test_crc())   return 1;
    if (test_frame()) return 1;
    if (test_vote())  return 1;
    if (test_cfg())   return 1;
    if (test_fault()) return 1;
    printf("C core: %d checks passed\n", checks);
    return 0;
}
