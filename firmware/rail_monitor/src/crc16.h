/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.
 * Check value: crc16("123456789") == 0x29B1
 */
#ifndef RM_CRC16_H
#define RM_CRC16_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t rm_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RM_CRC16_H */
