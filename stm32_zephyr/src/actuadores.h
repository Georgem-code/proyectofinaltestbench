#ifndef ACTUADORES_H
#define ACTUADORES_H

#include <stdint.h>

/* Motor DC, servo (almohadilla de frenado) y bocina.
 * El cableado y los canales de PWM salen del devicetree (app.overlay). */

/* Configura PWM y pines de direccion. 0 = ok. */
int actuadores_init(void);

/* Duty del motor en % (0-100). Tambien arma IN1/IN2. */
void act_motor(uint8_t pct);

/* Duty actual del motor, en %. */
uint8_t act_motor_duty(void);

/* Angulo del servo en grados (0-180). */
void act_servo(uint16_t grados);

/* Tono de la bocina en Hz (0 = silencio). */
void act_tono(uint32_t hz);

#endif /* ACTUADORES_H */
