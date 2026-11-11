/*
 * Banco de Pruebas de Carga - STM32 Blue Pill (Zephyr)
 *
 * El main no lleva logica: configura los modulos, levanta los hilos
 * y se retira. Todo el trabajo ocurre entre hilos, semaforos, colas
 * y mensajes:
 *
 *   comms.c       ISR de UART -> cola de bytes -> hilo -> eventos FSM
 *   fsm.c         FSM dormida en el bus de eventos (k_event)
 *   imu.c         MPU6050 por el driver oficial -> eventos de vibracion
 *   encoder.c     ISR de GPIO -> semaforo contador -> RPM promediadas
 *   indicadores.c LED de latido + patrones de la bocina
 *   telemetria.c  muestreo -> cola de tramas -> TX por UART (JSON+CRC)
 *
 * Comandos por UART desde el ESP32: 'G' arrancar, 'E' emergencia,
 * 'R' reiniciar. El cableado completo esta en app.overlay.
 */
#include <zephyr/kernel.h>

#include "actuadores.h"
#include "comms.h"
#include "encoder.h"
#include "fsm.h"
#include "imu.h"
#include "indicadores.h"
#include "telemetria.h"

int main(void)
{
	/* Sin salidas PWM/GPIO no hay banco que controlar. */
	if (actuadores_init() != 0) {
		return 0;
	}

	/* El resto son de mejor esfuerzo: si el MPU6050 no esta, la
	 * telemetria lo reporta ("SIN_SENSOR") y el banco sigue. */
	imu_init();
	encoder_init();
	comms_init();
	indicadores_init();
	telemetria_init();
	fsm_init();

	/* Nada de bloquear aqui: los hilos llevan el mando. */
	k_sleep(K_FOREVER);
	return 0;
}
