#ifndef TELEMETRIA_H
#define TELEMETRIA_H

#include <stdint.h>

/* Trama que viaja de los hilos de medida al hilo TX (por cola). */
struct trama {
	uint8_t estado;   /* enum fsm_estado */
	uint8_t duty;     /* %              */
	uint16_t rpm;     /* promediadas    */
	int32_t vib;      /* m/s^2 x100     */
	uint8_t sensor;   /* enum imu_estado */
};

/* Crea el hilo de muestreo y el hilo TX. 0 = ok. */
int telemetria_init(void);

#endif /* TELEMETRIA_H */
