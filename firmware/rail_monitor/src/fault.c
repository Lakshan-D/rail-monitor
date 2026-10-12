#include "fault.h"
#include "frame.h"   /* RM_FLAG_* */

void rm_cfg_default(rm_cfg_t *cfg)
{
    cfg->uv_mv    = 4500u;   /* 5 V rail, -10 %  */
    cfg->ov_mv    = 5500u;   /* 5 V rail, +10 %  */
    cfg->oc_ma    = 3000u;   /* 3 A, matches the buck converter project */
    cfg->debounce = 5u;      /* 50 ms at 100 Hz sampling */
}

int rm_cfg_valid(const rm_cfg_t *cfg)
{
    return (cfg->uv_mv < cfg->ov_mv) &&
           (cfg->debounce >= 1u) && (cfg->debounce <= 50u);
}

void rm_cfg_to_bytes(const rm_cfg_t *cfg, uint8_t *out)
{
    out[0] = (uint8_t)(cfg->uv_mv >> 8);
    out[1] = (uint8_t)(cfg->uv_mv);
    out[2] = (uint8_t)(cfg->ov_mv >> 8);
    out[3] = (uint8_t)(cfg->ov_mv);
    out[4] = (uint8_t)(cfg->oc_ma >> 8);
    out[5] = (uint8_t)(cfg->oc_ma);
    out[6] = cfg->debounce;
}

void rm_cfg_from_bytes(rm_cfg_t *cfg, const uint8_t *in)
{
    cfg->uv_mv    = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    cfg->ov_mv    = (uint16_t)(((uint16_t)in[2] << 8) | in[3]);
    cfg->oc_ma    = (uint16_t)(((uint16_t)in[4] << 8) | in[5]);
    cfg->debounce = in[6];
}

void rm_fault_init(rm_fault_t *f)
{
    f->uv.bad = f->uv.good = f->uv.active = 0;
    f->ov.bad = f->ov.good = f->ov.active = 0;
    f->oc.bad = f->oc.good = f->oc.active = 0;
}

static void chan_step(rm_chan_t *c, int violated, uint8_t debounce)
{
    if (violated) {
        c->good = 0;
        if (c->bad < 255u) {
            c->bad++;
        }
        if (c->bad >= debounce) {
            c->active = 1u;
        }
    } else {
        c->bad = 0;
        if (c->good < 255u) {
            c->good++;
        }
        if (c->good >= debounce) {
            c->active = 0u;
        }
    }
}

uint8_t rm_fault_update(rm_fault_t *f, const rm_cfg_t *cfg,
                        uint16_t vrail_mv, int16_t irail_ma)
{
    uint8_t flags = 0;

    chan_step(&f->uv, vrail_mv < cfg->uv_mv, cfg->debounce);
    chan_step(&f->ov, vrail_mv > cfg->ov_mv, cfg->debounce);
    chan_step(&f->oc, irail_ma > (int16_t)cfg->oc_ma, cfg->debounce);

    if (f->uv.active) flags |= RM_FLAG_UV;
    if (f->ov.active) flags |= RM_FLAG_OV;
    if (f->oc.active) flags |= RM_FLAG_OC;
    return flags;
}
