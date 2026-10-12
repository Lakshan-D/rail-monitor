/* Threshold fault detection with debounce and the config block that is stored
 * three times in EEPROM.
 *
 * Debounce: a limit must be violated for `debounce` samples in a row before the
 * flag sets, and must be healthy for `debounce` samples in a row before it
 * clears. One noisy ADC sample cannot raise or drop a fault.
 */
#ifndef RM_FAULT_H
#define RM_FAULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RM_CFG_BYTES 7u

typedef struct {
    uint16_t uv_mv;      /* under-voltage limit, fault if below */
    uint16_t ov_mv;      /* over-voltage limit, fault if above  */
    uint16_t oc_ma;      /* over-current limit, fault if above  */
    uint8_t  debounce;   /* samples in a row, 1..50             */
} rm_cfg_t;

typedef struct {
    uint8_t bad;
    uint8_t good;
    uint8_t active;
} rm_chan_t;

typedef struct {
    rm_chan_t uv;
    rm_chan_t ov;
    rm_chan_t oc;
} rm_fault_t;

void    rm_cfg_default(rm_cfg_t *cfg);
int     rm_cfg_valid(const rm_cfg_t *cfg);
void    rm_cfg_to_bytes(const rm_cfg_t *cfg, uint8_t *out);   /* RM_CFG_BYTES */
void    rm_cfg_from_bytes(rm_cfg_t *cfg, const uint8_t *in);  /* RM_CFG_BYTES */

void    rm_fault_init(rm_fault_t *f);
/* Feed one sample. Returns the RM_FLAG_UV / OV / OC bits currently active. */
uint8_t rm_fault_update(rm_fault_t *f, const rm_cfg_t *cfg,
                        uint16_t vrail_mv, int16_t irail_ma);

#ifdef __cplusplus
}
#endif

#endif /* RM_FAULT_H */
