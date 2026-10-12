#include "vote.h"

uint8_t rm_vote8(uint8_t a, uint8_t b, uint8_t c)
{
    return (uint8_t)((a & b) | (a & c) | (b & c));
}

size_t rm_vote_buf(const uint8_t *a, const uint8_t *b, const uint8_t *c,
                   uint8_t *out, size_t len)
{
    size_t disagree = 0;

    for (size_t i = 0; i < len; i++) {
        out[i] = rm_vote8(a[i], b[i], c[i]);
        if ((a[i] != out[i]) || (b[i] != out[i]) || (c[i] != out[i])) {
            disagree++;
        }
    }
    return disagree;
}
