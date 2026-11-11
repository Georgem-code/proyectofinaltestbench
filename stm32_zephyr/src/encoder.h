#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/* Encoder optico del motor: ISR por flanco + semaforo contador +
 * hilo que calcula las RPM promediadas por ventana. */

/* Configura el pin y crea el hilo. 0 = ok. */
int encoder_init(void);

/* RPM promediadas de las ultimas CFG_N_PROM ventanas de 1 s. */
uint16_t encoder_rpm(void);

#endif /* ENCODER_H */
