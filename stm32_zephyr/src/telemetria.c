/*
 * Telemetria del banco hacia el ESP32.
 *
 *   hilo muestreo -> cola de tramas (k_msgq) -> hilo TX -> UART
 *
 * La trama es JSON con un CRC-16/CCITT del cuerpo, identico al del
 * original para que el ESP32 no tenga que cambiar el parser:
 *
 *   {"nodo":"STM32","planta":"BANCO","fsm_state":"IDLE","pwm_duty":0,
 *    "rpm":0,"i2c_sensor":"MPU6050","val_i2c":0.00,"crc":"1A2B"}
 *
 * "val_i2c" lleva la vibracion medida (desviacion de la gravedad),
 * en m/s^2 con dos decimales.
 */
#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "actuadores.h"
#include "app_config.h"
#include "crc16.h"
#include "encoder.h"
#include "fsm.h"
#include "imu.h"
#include "telemetria.h"

#define MUESTREO_STACK 512
#define MUESTREO_PRIO  3
#define TX_STACK       1536
#define TX_PRIO        7
#define TX_COLA_PROF   4

K_THREAD_STACK_DEFINE(muestreo_stack, MUESTREO_STACK);
static struct k_thread muestreo_hilo;
K_THREAD_STACK_DEFINE(tx_stack, TX_STACK);
static struct k_thread tx_hilo;

/* Cola productor/consumor: muestreo produce, TX consume */
K_MSGQ_DEFINE(cola_telem, sizeof(struct trama), TX_COLA_PROF, 4);

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(usart2));

static const char *nombre_sensor(uint8_t s)
{
	switch (s) {
	case IMU_OK:          return "MPU6050";
	case IMU_ERR_LECTURA: return "ERR_LECTURA";
	default:              return "SIN_SENSOR";
	}
}

/* Hilo muestreo: fotografía del banco cada CFG_TELEM_MS. */
static void muestreo_fn(void *a, void *b, void *c)
{
	struct trama t;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_sleep(K_MSEC(CFG_TELEM_MS));

		t.estado = (uint8_t)fsm_estado();
		t.duty   = act_motor_duty();
		t.rpm    = encoder_rpm();
		t.vib    = imu_vib();
		t.sensor = imu_estado();

		/* cola llena: se tira la trama, la siguiente llega en 1 s */
		k_msgq_put(&cola_telem, &t, K_NO_WAIT);
	}
}

/* Hilo TX: formatea, protege con CRC y manda por UART. */
static void tx_fn(void *a, void *b, void *c)
{
	static char linea[240];
	struct trama t;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_msgq_get(&cola_telem, &t, K_FOREVER);

		int32_t v = t.vib;

		/* 1) cuerpo del JSON (sin cerrar la llave) */
		int n = snprintf(linea, sizeof(linea),
			"{\"nodo\":\"STM32\",\"planta\":\"BANCO\",\"fsm_state\":\"%s\","
			"\"pwm_duty\":%d,\"rpm\":%d,\"i2c_sensor\":\"%s\","
			"\"val_i2c\":%d.%02d",
			fsm_nombre(t.estado), t.duty, t.rpm,
			nombre_sensor(t.sensor), (int)(v / 100), (int)(v % 100));

		if (n < 0 || n >= (int)sizeof(linea) - 24) {
			continue;   /* no cabe: se descarta la trama */
		}

		/* 2) CRC del cuerpo, agregado antes de la llave final */
		uint16_t crc = crc16((const uint8_t *)linea, (size_t)n);

		n += snprintf(linea + n, sizeof(linea) - n,
			      ",\"crc\":\"%04X\"}\n", crc);
		if (n < 0) {
			continue;
		}

		for (int i = 0; i < n; i++) {
			uart_poll_out(uart, linea[i]);
		}
	}
}

int telemetria_init(void)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	k_thread_create(&muestreo_hilo, muestreo_stack, MUESTREO_STACK,
			muestreo_fn, NULL, NULL, NULL, MUESTREO_PRIO, 0,
			K_NO_WAIT);
	k_thread_create(&tx_hilo, tx_stack, TX_STACK, tx_fn, NULL, NULL, NULL,
			TX_PRIO, 0, K_NO_WAIT);
	return 0;
}
