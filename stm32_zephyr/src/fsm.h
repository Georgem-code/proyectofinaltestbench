#ifndef FSM_H
#define FSM_H

#include <zephyr/kernel.h>
#include <stdint.h>

/*
 * Maquina de estados del banco.
 *
 * La FSM no sondea nada: duerme en el bus de eventos (k_event, un
 * banco de semaforos binarios: un bit por aviso) y despierta solo
 * cuando alguien hace fsm_post() o vence el plazo del estado.
 *
 *   IDLE    en reposo, esperando el comando 'G'
 *   ACCEL   rampa de duty 0 -> 100 % (CFG_RAMPA_MS / CFG_RAMPA_PASO)
 *   STEADY  ensayo a plena carga durante CFG_T_STEADY_MS
 *   FALLA   alarma: se confirma la vibracion durante CFG_T_FALLA_MS
 *   EMERG   parada de emergencia; solo 'R' devuelve a IDLE
 */
enum fsm_estado {
	FSM_IDLE,
	FSM_ACCEL,
	FSM_STEADY,
	FSM_FALLA,
	FSM_EMERG,
};

/* Eventos del bus (cada bit = un semaforo binario) */
#define FSM_EVT_GO        BIT(0)  /* comando 'G' del ESP32      */
#define FSM_EVT_EMERG     BIT(1)  /* comando 'E' del ESP32      */
#define FSM_EVT_RESET     BIT(2)  /* comando 'R' del ESP32      */
#define FSM_EVT_VIB_ALTA  BIT(3)  /* imu: vibracion confirmada  */
#define FSM_EVT_VIB_OK    BIT(4)  /* imu: vibracion normalizada */
#define FSM_EVT_TODOS     (FSM_EVT_GO | FSM_EVT_EMERG | FSM_EVT_RESET | \
			    FSM_EVT_VIB_ALTA | FSM_EVT_VIB_OK)

/* Crea el hilo de la FSM. 0 = ok. */
int fsm_init(void);

/* Publica eventos en el bus (llamable desde cualquier hilo). */
void fsm_post(uint32_t eventos);

/* Estado actual (enum fsm_estado). */
int fsm_estado(void);

/* "IDLE", "ACCEL", "STEADY", "FALLA" o "EMERGENCIA". */
const char *fsm_nombre(int estado);

#endif /* FSM_H */
