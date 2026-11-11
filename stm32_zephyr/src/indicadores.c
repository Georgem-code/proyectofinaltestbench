/*
 * Indicadores del banco: LED PC13 (gpio-leds, activo bajo) y bocina.
 *
 *   IDLE / STEADY : LED al ritmo cardiaco, bocina en silencio
 *   ACCEL         : pitido corto de 600 Hz cada segundo
 *   FALLA         : 1000 Hz a golpes
 *   EMERG         : LED rapido + 2000 Hz intermitente
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "actuadores.h"
#include "fsm.h"
#include "indicadores.h"

#define IND_STACK 512
#define IND_PRIO  2

K_THREAD_STACK_DEFINE(ind_stack, IND_STACK);
static struct k_thread ind_hilo;

static const struct gpio_dt_spec led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(status_led), gpios);

static void ind_fn(void *a, void *b, void *c)
{
	int64_t t_led = 0;
	uint32_t f_prev = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		int64_t ahora = k_uptime_get();
		int est = fsm_estado();

		/* LED: lento = sistema vivo, rapido = EMERGENCIA */
		uint32_t periodo = (est == FSM_EMERG) ? 100 : 500;

		if (ahora - t_led >= periodo) {
			t_led = ahora;
			gpio_pin_toggle_dt(&led);
		}

		/* Bocina: patron propio de cada estado */
		uint32_t f = 0;

		switch (est) {
		case FSM_ACCEL: f = ((ahora % 1000) < 100) ? 600  : 0; break;
		case FSM_FALLA: f = ((ahora % 800)  < 300) ? 1000 : 0; break;
		case FSM_EMERG: f = ((ahora % 200)  < 100) ? 2000 : 0; break;
		default:        f = 0;                            break;
		}
		if (f != f_prev) {
			act_tono(f);
			f_prev = f;
		}

		k_msleep(20);
	}
}

int indicadores_init(void)
{
	if (!device_is_ready(led.port)) {
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (err != 0) {
		return err;
	}

	k_thread_create(&ind_hilo, ind_stack, IND_STACK, ind_fn, NULL, NULL, NULL,
			IND_PRIO, 0, K_NO_WAIT);
	return 0;
}
