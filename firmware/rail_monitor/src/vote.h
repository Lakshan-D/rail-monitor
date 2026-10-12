/* Bitwise 2-of-3 majority voting, same idea as the TMR voter in the buck
 * converter project (tmr_voter.v), here applied to the EEPROM config copies.
 *
 * A bit in the output is 1 when at least two of the three input bits are 1.
 * A single flipped bit in any one copy is outvoted and corrected. Two copies
 * flipped at the same bit position cannot be corrected.
 */
#ifndef RM_VOTE_H
#define RM_VOTE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t rm_vote8(uint8_t a, uint8_t b, uint8_t c);

/* Vote across three equal-length buffers into out.
 * Returns how many bytes had at least one disagreeing copy (0 = all agree).
 * out may alias none of the inputs. */
size_t rm_vote_buf(const uint8_t *a, const uint8_t *b, const uint8_t *c,
                   uint8_t *out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RM_VOTE_H */
