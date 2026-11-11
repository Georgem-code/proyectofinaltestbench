/*
 * FSM del banco de pruebas.
 *
 * Flujo: IDLE --G--> ACCEL --rampa al 100%--> STEADY --CFG_T_STEADY_MS
 *        o vibracion alta--> FALLA --CFG_T_FALLA_MS--> EMERG o IDLE
 *        segun la vibracion. EMERG solo sale con 'R'.
 *
 * La FSM se bloquea en k_event_wait() (el bus de eventos es un banco
 * de semaforos binarios): no sondea UART ni sensores, no imprime y no
 * genera pulsos por software. Los plazos de cada estado se implementan
 * con el timeout del propio k_event_wait().
 */
#include <errno.h>
#include <zephyr/kernel.h>

#include "actuadores.h"
#include "app_config.h"
#include "fsm.h"
#include "imu.h"

#define FSM_STACK 768
#define FSM_PRIO  5

K_THREAD_STACK_DEFINE(fsm_stack, FSM_STACK);
static struct k_thread fsm_hilo;

static atomic_t estado = ATOMIC_INIT(FSM_IDLE);
static int64_t t_entrada;   /* instante en que entro al estado actual */

/* Bus de eventos de la FSM */
static K_EVENT_DEFINE(fsm_eventos);

void fsm_post(uint32_t eventos)
{
	k_event_post(&fsm_eventos, eventos);
}

int fsm_estado(void)
{
	return (int)atomic_get(&estado);
}

const char *fsm_nombre(int est)
{
	static const char *const nombres[] = {
		"IDLE", "ACCEL", "STEADY", "FALLA", "EMERGENCIA"
	};

	if (est < 0 || est > FSM_EMERG) {
		return "?";
	}
	return nombres[est];
}

/* Salidas que se arman al entrar a cada estado. */
static void ir_a(enum fsm_estado nuevo)
{
	atomic_set(&estado, nuevo);
	t_entrada = k_uptime_get();

	switch (nuevo) {
	case FSM_IDLE:
		act_motor(0);
		act_servo(0);
		break;
	case FSM_ACCEL:
		act_motor(0);       /* la rampa arranca desde cero */
		act_servo(90);
		break;
	case FSM_STEADY:
		act_servo(180);     /* almohadilla totalmente apartada */
		break;
	case FSM_FALLA:
		/* bocina y LED los lleva el modulo indicadores */
		break;
	case FSM_EMERG:
		act_motor(0);
		act_servo(0);       /* la almohadilla frena el eje */
		break;
	}
}

/* Espera cualquier evento hasta el instante "limite" (en ms).
 * Devuelve los eventos recibidos (0 = solo vencio el plazo). */
static uint32_t esperar_hasta(int64_t limite)
{
	int64_t resta = limite - k_uptime_get();

	if (resta <= 0) {
		return 0;
	}
	return k_event_wait(&fsm_eventos, FSM_EVT_TODOS, false,
			    K_MSEC((uint32_t)resta));
}

static void fsm_fn(void *a, void *b, void *c)
{
	uint32_t ev;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	ir_a(FSM_IDLE);

	while (1) {
		switch ((int)atomic_get(&estado)) {

		case FSM_IDLE:
			ev = k_event_wait(&fsm_eventos, FSM_EVT_TODOS, false,
					  K_FOREVER);
			if (ev & FSM_EVT_EMERG) {
				ir_a(FSM_EMERG);
			} else if (ev & FSM_EVT_RESET) {
				ir_a(FSM_IDLE);
			} else if (ev & FSM_EVT_GO) {
				ir_a(FSM_ACCEL);
			}
			break;

		case FSM_ACCEL:
			if (act_motor_duty() >= 100) {
				ir_a(FSM_STEADY);
				break;
			}
			/* siguiente paso de la rampa */
			ev = esperar_hasta(k_uptime_get() + CFG_RAMPA_MS);
			if (ev & FSM_EVT_EMERG) {
				ir_a(FSM_EMERG);
			} else if (ev & FSM_EVT_RESET) {
				ir_a(FSM_IDLE);
			} else if (ev == 0) {
				act_motor(act_motor_duty() + CFG_RAMPA_PASO);
			}
			/* los avisos de vibracion no aplican en la rampa */
			break;

		case FSM_STEADY:
			ev = esperar_hasta(t_entrada + CFG_T_STEADY_MS);
			if (ev & FSM_EVT_EMERG) {
				ir_a(FSM_EMERG);
			} else if (ev & FSM_EVT_RESET) {
				ir_a(FSM_IDLE);
			} else if (ev & FSM_EVT_VIB_ALTA) {
				ir_a(FSM_FALLA);   /* fallo anticipado */
			} else if (ev == 0) {
				ir_a(FSM_FALLA);   /* fin del tiempo de ensayo */
			}
			break;

		case FSM_FALLA:
			ev = esperar_hasta(t_entrada + CFG_T_FALLA_MS);
			if (ev & FSM_EVT_EMERG) {
				ir_a(FSM_EMERG);
			} else if (ev & FSM_EVT_RESET) {
				ir_a(FSM_IDLE);
			} else if (ev & FSM_EVT_VIB_ALTA) {
				ir_a(FSM_EMERG);   /* vibracion confirmada */
			} else if (ev == 0) {
				/* decide con la medicion vigente */
				if (imu_vib_alta()) {
					ir_a(FSM_EMERG);
				} else {
					ir_a(FSM_IDLE);
				}
			}
			break;

		case FSM_EMERG:
			/* no sale sola: hace falta el boton S3 ('R') */
			ev = k_event_wait(&fsm_eventos, FSM_EVT_TODOS, false,
					  K_FOREVER);
			if (ev & FSM_EVT_RESET) {
				ir_a(FSM_IDLE);
			}
			break;
		}
	}
}

int fsm_init(void)
{
	k_thread_create(&fsm_hilo, fsm_stack, FSM_STACK, fsm_fn, NULL, NULL, NULL,
			FSM_PRIO, 0, K_NO_WAIT);
	return 0;
}
