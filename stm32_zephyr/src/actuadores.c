/*
 * Actuadores del banco: motor DC, servo y bocina.
 *
 * Hardware (todo declarado en app.overlay):
 *   motor  PA0 (TIM2_CH1, canal 1) + IN1/IN2 en PB12/PB13
 *   servo  PB0 (TIM3_CH3, canal 3)
 *   bocina PA8 (TIM1_CH1, canal 1)
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

#include "actuadores.h"
#include "app_config.h"

/* Canal del timer que usa cada salida (ver pinctrl en app.overlay) */
#define MOTOR_CANAL 1
#define SERVO_CANAL 3
#define TONO_CANAL  1

#define MOTOR_PERIODO_NS 1000000u   /* 1 kHz  */
#define SERVO_PERIODO_NS 20000000u  /* 50 Hz  */

static const struct device *const pwm_motor = DEVICE_DT_GET(DT_NODELABEL(pwm2));
static const struct device *const pwm_servo = DEVICE_DT_GET(DT_NODELABEL(pwm3));
static const struct device *const pwm_tono  = DEVICE_DT_GET(DT_NODELABEL(pwm1));

/* Direccion del puente H (nodo zephyr,user del overlay) */
static const struct gpio_dt_spec in1 =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr,user), motor_in1_gpios);
static const struct gpio_dt_spec in2 =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr,user), motor_in2_gpios);

static atomic_t duty = ATOMIC_INIT(0);

void act_motor(uint8_t pct)
{
	if (pct > 100) {
		pct = 100;
	}
	atomic_set(&duty, (long)pct);

	gpio_pin_set_dt(&in1, pct > 0);
	gpio_pin_set_dt(&in2, false);
	(void)pwm_set(pwm_motor, MOTOR_CANAL, MOTOR_PERIODO_NS,
		      (MOTOR_PERIODO_NS / 100u) * pct, PWM_POLARITY_NORMAL);
}

uint8_t act_motor_duty(void)
{
	return (uint8_t)atomic_get(&duty);
}

void act_servo(uint16_t grados)
{
	if (grados > 180) {
		grados = 180;
	}
	/* 0 grados = CFG_SERVO_US_MIN, 180 grados = +1800 us */
	uint32_t ancho_ns = (CFG_SERVO_US_MIN + 10u * grados) * 1000u;

	(void)pwm_set(pwm_servo, SERVO_CANAL, SERVO_PERIODO_NS, ancho_ns,
		      PWM_POLARITY_NORMAL);
}

void act_tono(uint32_t hz)
{
	if (hz == 0) {
		/* silencio: pulso nulo */
		(void)pwm_set(pwm_tono, TONO_CANAL, MOTOR_PERIODO_NS, 0,
			      PWM_POLARITY_NORMAL);
		return;
	}
	uint32_t periodo_ns = 1000000000u / hz;

	(void)pwm_set(pwm_tono, TONO_CANAL, periodo_ns, periodo_ns / 2u,
		      PWM_POLARITY_NORMAL);
}

int actuadores_init(void)
{
	if (!device_is_ready(pwm_motor) || !device_is_ready(pwm_servo) ||
	    !device_is_ready(pwm_tono)) {
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(&in1, GPIO_OUTPUT_INACTIVE);

	err |= gpio_pin_configure_dt(&in2, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	act_motor(0);
	act_servo(0);
	act_tono(0);
	return 0;
}
