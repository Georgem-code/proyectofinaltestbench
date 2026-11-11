/*
 * Banco de Pruebas - STM32 Blue Pill (Zephyr)
 * Hilos: FSM, sensor I2C (con mutex), TX telemetria (con cola)
 * Comandos por UART desde el ESP32:  G = arrancar, E = emergencia, R = reiniciar
 * Servo en PB0 con pulsos por software. Motor con jumper en ENA.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- AJUSTES ---------- */
#define PPV        20     /* ranuras del disco = pulsos por vuelta */
#define VIB_SIM    100    /* 100 = simula vibracion fuerte (FALLA -> EMERGENCIA). Pon 0 para volver a IDLE */
#define VIB_LIMITE 50
#define N_PROM     3      /* cuantas lecturas de RPM se promedian */

/* ---------- DISPOSITIVOS ---------- */
static const struct device *uart  = DEVICE_DT_GET(DT_NODELABEL(usart2));
static const struct device *pwm_m = DEVICE_DT_GET(DT_NODELABEL(pwm2));   /* motor  PA0 */
static const struct device *pwm_b = DEVICE_DT_GET(DT_NODELABEL(pwm1));   /* bocina PA8 */
static const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
static const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
static const struct device *i2c1  = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static char diag[24] = "SIN_SENSOR";   /* resultado de la sonda I2C */

#define PIN_IN1   12
#define PIN_IN2   13
#define PIN_ENC   14
#define PIN_SERVO 0    /* PB0: senal del servo */
#define PIN_LED   13   /* PC13, se enciende con 0 */

/* ---------- ESTADOS ---------- */
enum { ST_IDLE, ST_ACCEL, ST_STEADY, ST_FALLA, ST_EMERG };
static const char *nombres[] = {"IDLE", "ACCEL", "STEADY", "FALLA", "EMERGENCIA"};

static volatile int estado = ST_IDLE;
static volatile int duty = 0;
static volatile uint32_t servo_us = 600;   /* ancho del pulso del servo (us) */
static int64_t t_est = 0;
static int64_t t_acc = 0;

/* ---------- SENSOR (protegido con mutex) ---------- */
K_MUTEX_DEFINE(i2c_mutex);
static volatile int32_t val_centi = 0;   /* aceleracion Z x100 */
static volatile int sensor_ok = 0;

/* ---------- ENCODER ---------- */
static atomic_t pulsos = ATOMIC_INIT(0);
K_SEM_DEFINE(enc_sem, 0, 1);
static volatile int64_t t_ultimo_pulso = 0;
static struct gpio_callback enc_cb;

static void enc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	atomic_inc(&pulsos);
	k_sem_give(&enc_sem);
}

/* ---------- TELEMETRIA (cola) ---------- */
struct telem {
	uint8_t  est;
	uint8_t  duty;
	uint16_t rpm;
	int32_t  val;
	uint8_t  ok;
};
K_MSGQ_DEFINE(cola, sizeof(struct telem), 4, 4);

/* ---------- CRC-16/CCITT (poly 0x1021, inicio 0xFFFF) ---------- */
static uint16_t crc16(const uint8_t *datos, size_t n)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < n; i++) {
		crc ^= (uint16_t)datos[i] << 8;
		for (int b = 0; b < 8; b++) {
			if (crc & 0x8000) {
				crc = (uint16_t)((crc << 1) ^ 0x1021);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}
	return crc;
}

/* ---------- ACTUADORES ---------- */
static void motor(int d)
{
	if (d < 0) d = 0;
	if (d > 100) d = 100;
	duty = d;
	gpio_pin_set_raw(gpiob, PIN_IN1, d > 0 ? 1 : 0);
	gpio_pin_set_raw(gpiob, PIN_IN2, 0);
	pwm_set(pwm_m, 1, 1000000, (1000000U / 100U) * d, PWM_POLARITY_NORMAL);
}

/* Solo guarda el ancho del pulso; el hilo FSM lo genera en PB0 */
static void servo(int grados)
{
	if (grados < 0) grados = 0;
	if (grados > 180) grados = 180;
	servo_us = 600U + 10U * (uint32_t)grados;   /* 600 a 2400 us */
}

/* Bocina en PA8: hz = 0 silencia, otro valor = tono a esa frecuencia */
static void tono(uint32_t hz)
{
	if (!device_is_ready(pwm_b)) {
		return;
	}
	if (hz == 0) {
		if (pwm_set(pwm_b, 1, 0, 0, PWM_POLARITY_NORMAL) != 0) {
			pwm_set(pwm_b, 1, 1000000, 0, PWM_POLARITY_NORMAL);
		}
		return;
	}
	uint32_t periodo = 1000000000U / hz;   /* ns */
	pwm_set(pwm_b, 1, periodo, periodo / 2, PWM_POLARITY_NORMAL);
}

static void cambiar(int nuevo)
{
	estado = nuevo;
	t_est = k_uptime_get();
	switch (nuevo) {
	case ST_IDLE:   motor(0); servo(0);   break;
	case ST_ACCEL:  motor(0); servo(90);  t_acc = t_est; break;
	case ST_STEADY: servo(180);           break;
	case ST_FALLA:                        break;
	case ST_EMERG:  motor(0); servo(0);   break;
	}
}

/* ---------- HILO FSM ---------- */
#define STACK_FSM 1024
K_THREAD_STACK_DEFINE(stack_fsm, STACK_FSM);
static struct k_thread hilo_fsm;

static void fsm_fn(void *a, void *b, void *c)
{
	int64_t t_led = 0;
	uint32_t f_prev = 0xFFFFFFFFU;

	while (1) {
		unsigned char rx;

		/* comandos que llegan del ESP32 */
		while (uart_poll_in(uart, &rx) == 0) {
			if (rx == 'G' && estado == ST_IDLE) {
				cambiar(ST_ACCEL);
			} else if (rx == 'E') {
				cambiar(ST_EMERG);
			} else if (rx == 'R') {
				cambiar(ST_IDLE);
			}
		}

		int64_t ahora = k_uptime_get();
		int64_t dt = ahora - t_est;

		switch (estado) {
		case ST_IDLE:
			break;
		case ST_ACCEL:
			if (ahora - t_acc >= 200) {
				t_acc = ahora;
				if (duty < 100) {
					motor(duty + 5);
				} else {
					cambiar(ST_STEADY);
				}
			}
			break;
		case ST_STEADY:
			if (dt >= 5000) cambiar(ST_FALLA);
			break;
		case ST_FALLA:
			if (dt >= 2000) {
				if (VIB_SIM > VIB_LIMITE) cambiar(ST_EMERG);
				else cambiar(ST_IDLE);
			}
			break;
		case ST_EMERG:
			/* No vuelve sola a IDLE. Solo sale con la 'R' (boton S3). */
			break;
		}

		/* LED PC13: lento = sistema vivo, rapido = EMERGENCIA */
		int periodo = (estado == ST_EMERG) ? 100 : 500;

		if (ahora - t_led >= periodo) {
			t_led = ahora;
			gpio_pin_toggle(gpioc, PIN_LED);
		}

		/* BOCINA: cada estado suena distinto */
		uint32_t f = 0;

		switch (estado) {
		case ST_ACCEL: f = ((ahora % 1000) < 100) ? 600  : 0; break;
		case ST_FALLA: f = ((ahora % 800)  < 300) ? 1000 : 0; break;
		case ST_EMERG: f = ((ahora % 200)  < 100) ? 2000 : 0; break;
		default:       f = 0; break;
		}
		if (f != f_prev) {
			tono(f);
			f_prev = f;
		}

		/* SERVO en PB0: un pulso por vuelta del bucle (~50 Hz) */
		uint32_t us = servo_us;

		gpio_pin_set_raw(gpiob, PIN_SERVO, 1);
		k_busy_wait(us);
		gpio_pin_set_raw(gpiob, PIN_SERVO, 0);

		k_msleep(20);
	}
}

/* ---------- SONDA I2C: pregunta "quien eres" (registro 0x75) ---------- */
static void sondear(void)
{
	uint8_t id = 0;

	if (!device_is_ready(i2c1)) {
		snprintf(diag, sizeof(diag), "I2C_NO_LISTO");
		return;
	}
	k_mutex_lock(&i2c_mutex, K_FOREVER);
	int r68 = i2c_reg_read_byte(i2c1, 0x68, 0x75, &id);
	if (r68 == 0) {
		snprintf(diag, sizeof(diag), "ACK68_ID%02X", id);
	} else if (i2c_reg_read_byte(i2c1, 0x69, 0x75, &id) == 0) {
		snprintf(diag, sizeof(diag), "ACK69_ID%02X", id);
	} else {
		snprintf(diag, sizeof(diag), "NACK_68_69");
	}
	k_mutex_unlock(&i2c_mutex);
}

/* ---------- HILO SENSOR ---------- */
#define STACK_SEN 1280
K_THREAD_STACK_DEFINE(stack_sen, STACK_SEN);
static struct k_thread hilo_sen;

static void sen_fn(void *a, void *b, void *c)
{
	uint8_t d[6] = {0};
	uint8_t dir = 0x68;
	int listo = 0;

	while (1) {
		int r = -1;

		if (device_is_ready(i2c1)) {
			k_mutex_lock(&i2c_mutex, K_FOREVER);
			if (!listo) {
				if (i2c_reg_write_byte(i2c1, 0x68, 0x6B, 0x00) == 0) {
					dir = 0x68;
					listo = 1;
				} else if (i2c_reg_write_byte(i2c1, 0x69, 0x6B, 0x00) == 0) {
					dir = 0x69;
					listo = 1;
				}
				if (listo) {
					k_msleep(100);
				}
			}
			if (listo) {
				r = i2c_burst_read(i2c1, dir, 0x3B, d, 6);
				if (r != 0) {
					listo = 0;
				}
			}
			k_mutex_unlock(&i2c_mutex);
		}

		if (r == 0) {
			int16_t z = (int16_t)((d[4] << 8) | d[5]);
			val_centi = ((int32_t)z * 981) / 16384;   /* m/s^2 x100 */
			sensor_ok = 1;
		} else {
			sondear();
			sensor_ok = 0;
			val_centi = 0;
		}
		k_msleep(200);
	}
}

/* ---------- HILO ENCODER (usa el semaforo) ---------- */
#define STACK_ENC 512
K_THREAD_STACK_DEFINE(stack_enc, STACK_ENC);
static struct k_thread hilo_enc;

static void enc_fn(void *a, void *b, void *c)
{
	while (1) {
		k_sem_take(&enc_sem, K_FOREVER);
		t_ultimo_pulso = k_uptime_get();
		k_msleep(1);
	}
}

/* ---------- HILO TX ---------- */
#define STACK_TX 1280
K_THREAD_STACK_DEFINE(stack_tx, STACK_TX);
static struct k_thread hilo_tx;

static void tx_fn(void *a, void *b, void *c)
{
	struct telem t;
	char linea[240];

	while (1) {
		k_msgq_get(&cola, &t, K_FOREVER);

		int32_t v = (t.ok == 1) ? t.val : 0;
		const char *sg = (v < 0) ? "-" : "";
		int32_t av = abs(v);

		/* 1 = bien, 2 = error de lectura, 0 = sensor no listo */
		const char *nom = (t.ok == 1) ? "MPU6050" :
				  (t.ok == 2) ? "ERR_LECTURA" : (const char *)diag;

		/* 1) cuerpo del JSON (sin la llave final) */
		int n = snprintf(linea, sizeof(linea),
			"{\"nodo\":\"STM32\",\"planta\":\"BANCO\",\"fsm_state\":\"%s\","
			"\"pwm_duty\":%d,\"rpm\":%d,\"i2c_sensor\":\"%s\","
			"\"val_i2c\":%s%d.%02d",
			nombres[t.est], t.duty, t.rpm, nom, sg, (int)(av / 100), (int)(av % 100));

		if (n < 0 || n >= (int)sizeof(linea) - 24) {
			continue;   /* no cabe: se descarta esta trama */
		}

		/* 2) CRC-16 del cuerpo, y se agrega al final */
		uint16_t crc = crc16((const uint8_t *)linea, (size_t)n);

		n += snprintf(linea + n, sizeof(linea) - n, ",\"crc\":\"%04X\"}\n", crc);

		for (int i = 0; i < n; i++) {
			uart_poll_out(uart, linea[i]);
		}
	}
}

/* ---------- MAIN ---------- */
int main(void)
{
	if (!device_is_ready(uart) || !device_is_ready(pwm_m) ||
	    !device_is_ready(gpiob) || !device_is_ready(gpioc)) {
		return 0;
	}

	gpio_pin_configure(gpiob, PIN_IN1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(gpiob, PIN_IN2, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(gpiob, PIN_SERVO, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(gpioc, PIN_LED, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_raw(gpioc, PIN_LED, 1);

	/* encoder con pull-down para que no cuente ruido */
	gpio_pin_configure(gpiob, PIN_ENC, GPIO_INPUT | GPIO_PULL_DOWN);
	gpio_init_callback(&enc_cb, enc_isr, BIT(PIN_ENC));
	gpio_add_callback(gpiob, &enc_cb);
	gpio_pin_interrupt_configure(gpiob, PIN_ENC, GPIO_INT_EDGE_RISING);

	tono(0);   /* bocina en silencio desde el arranque */
	cambiar(ST_IDLE);

	k_thread_create(&hilo_fsm, stack_fsm, STACK_FSM, fsm_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&hilo_sen, stack_sen, STACK_SEN, sen_fn, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_create(&hilo_enc, stack_enc, STACK_ENC, enc_fn, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_create(&hilo_tx,  stack_tx,  STACK_TX,  tx_fn,  NULL, NULL, NULL, 7, 0, K_NO_WAIT);

	/* historial de las ultimas N_PROM lecturas de RPM */
	int hist[N_PROM] = {0};
	int idx = 0;

	while (1) {
		k_msleep(1000);

		long p = atomic_set(&pulsos, 0);   /* pulsos en 1 segundo */
		struct telem t;

		/* RPM = promedio de las ultimas N_PROM lecturas */
		hist[idx] = (int)((p * 60) / PPV);
		idx = (idx + 1) % N_PROM;

		int suma = 0;

		for (int i = 0; i < N_PROM; i++) {
			suma += hist[i];
		}

		t.est  = (uint8_t)estado;
		t.duty = (uint8_t)duty;
		t.rpm  = (uint16_t)(suma / N_PROM);
		t.val  = val_centi;
		t.ok   = (uint8_t)sensor_ok;

		k_msgq_put(&cola, &t, K_NO_WAIT);
	}
	return 0;
}
