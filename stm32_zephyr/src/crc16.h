#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

/* CRC-16/CCITT: polinomio 0x1021, inicio 0xFFFF.
 * El mismo calculo se repite en el ESP32 (protocol.py). */
uint16_t crc16(const uint8_t *datos, size_t n);

#endif /* CRC16_H */
