/*
 * Encoder optico del motor.
 *
 * Cada flanco de subida en PB14 dispara la ISR, que solo hace
 * k_sem_give() de un semaforo contador. El hilo drena el semaforo
 * una vez por ventana de 1 s: el numero de "take" exitosos es el
 * numero de pulsos de la ventana, con lo que se calculan las RPM y
 * un promedio movil de CFG_N_PROM ventanas.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "app_config.h"
#include "encoder.h"

#define ENC_STACK 512
#define ENC_PRIO  4

K_THREAD_STACK_DEFINE(enc_stack, ENC_STACK);
static struct k_thread enc_hilo;

/* Pin del encoder (nodo zephyr,user del overlay) */
static const struct gpio_dt_spec pin_enc =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr,user), encoder_gpios);

static atomic_t rpm_prom = ATOMIC_INIT(0);

/* Semaforo contador: la ISR lo da por cada pulso del disco. */
K_SEM_DEFINE(enc_sem, 0, 8192);

static struct gpio_callback enc_cb;

static void enc_isr(const struct device *port, struct gpio_callback *cb,
		    uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&enc_sem);
}

/* Cuanta los pulsos pendientes y deja el semaforo a cero. */
static uint32_t drenar_pulsos(void)
{
	uint32_t n = 0;

	while (k_sem_take(&enc_sem, K_NO_WAIT) == 0) {
		n++;
	}
	return n;
}

static void enc_fn(void *a, void *b, void *c)
{
	uint16_t hist[CFG_N_PROM] = {0};
	int idx = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		int64_t t0 = k_uptime_get();

		k_sleep(K_SECONDS(1));

		uint32_t ventana_ms = (uint32_t)(k_uptime_get() - t0);
		uint32_t pulsos = drenar_pulsos();
		uint32_t rpm = 0;

		/* RPM = pulsos * (60000 ms/min) / (PPV * ventana en ms) */
		if (ventana_ms > 0) {
			rpm = (uint32_t)((uint64_t)pulsos * 60000ULL /
					 ((uint64_t)CFG_PPV * ventana_ms));
		}

		/* promedio movil de las ultimas CFG_N_PROM ventanas */
		hist[idx] = (uint16_t)rpm;
		idx = (idx + 1) % CFG_N_PROM;

		uint32_t suma = 0;

		for (int i = 0; i < CFG_N_PROM; i++) {
			suma += hist[i];
		}
		atomic_set(&rpm_prom, (long)(suma / CFG_N_PROM));
	}
}

uint16_t encoder_rpm(void)
{
	return (uint16_t)atomic_get(&rpm_prom);
}

int encoder_init(void)
{
	if (!device_is_ready(pin_enc.port)) {
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(&pin_enc, GPIO_INPUT);

	if (err != 0) {
		return err;
	}

	gpio_init_callback(&enc_cb, enc_isr, BIT(pin_enc.pin));
	err = gpio_add_callback(pin_enc.port, &enc_cb);
	if (err != 0) {
		return err;
	}

	/* flanco de subida; el pull-down del devicetree evita contar ruido */
	err = gpio_pin_interrupt_configure_dt(&pin_enc, GPIO_INT_EDGE_RISING);
	if (err != 0) {
		return err;
	}

	k_thread_create(&enc_hilo, enc_stack, ENC_STACK, enc_fn, NULL, NULL, NULL,
			ENC_PRIO, 0, K_NO_WAIT);
	return 0;
}
