/*
 * IMU del banco: MPU6050 por I2C1 usando el driver oficial de Zephyr
 * (no hay acceso "raw" al bus: sensor_sample_fetch/sensor_channel_get).
 *
 * El hilo publica la vibracion en el bus de eventos de la FSM
 * (FSM_EVT_VIB_ALTA / FSM_EVT_VIB_OK) cuando el cambio esta
 * confirmado por CFG_VIB_MUESTRAS lecturas consecutivas, para no dar
 * falsas alarmas por ruido del sensor.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "app_config.h"
#include "fsm.h"
#include "imu.h"

#define IMU_STACK      1024
#define IMU_PRIO       6
#define IMU_PERIODO_MS 200

K_THREAD_STACK_DEFINE(imu_stack, IMU_STACK);
static struct k_thread imu_hilo;

/* Nodo del devicetree: &i2c1/mpu6050@68 (ver app.overlay) */
static const struct device *const imu = DEVICE_DT_GET(DT_NODELABEL(mpu6050));

static atomic_t vib = ATOMIC_INIT(0);
static atomic_t estado = ATOMIC_INIT(IMU_SIN_SENSOR);

int32_t imu_vib(void)
{
	return (int32_t)atomic_get(&vib);
}

bool imu_vib_alta(void)
{
	return atomic_get(&vib) > CFG_VIB_LIMITE;
}

uint8_t imu_estado(void)
{
	return (uint8_t)atomic_get(&estado);
}

/* Rango del acelerometro: +/-2 g. El driver oficial del MPU6050 no
 * expone attr_set (el rango real se fija por devicetree, accel-fs=2 en
 * app.overlay); esta llamada usa el canal correcto SENSOR_CHAN_ACCEL_XYZ
 * por si la plataforma trae un driver con attr_set, y su retorno se
 * ignora adrede cuando no esta soportado. */
static void imu_configurar(void)
{
	struct sensor_value rango = {.val1 = 2};

	(void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_FULL_SCALE, &rango);
}

static void imu_fn(void *a, void *b, void *c)
{
	struct sensor_value az;
	int altas = 0;        /* lecturas seguidas sobre el umbral   */
	int normales = 0;      /* lecturas seguidas bajo el umbral    */
	bool listo = false;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (device_is_ready(imu)) {
		imu_configurar();
		listo = true;
	}

	while (1) {
		int r = -1;

		if (listo) {
			r = sensor_sample_fetch(imu);
		}
		if (r == 0 &&
		    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_Z, &az) == 0) {
			/* sensor_value: val1 = parte entera,
			 * val2 = fraccion en millonimas */
			int32_t az_centi = az.val1 * 100 + az.val2 / 10000;
			int32_t desvio = az_centi - CFG_G_CENTI;

			if (desvio < 0) {
				desvio = -desvio;
			}
			/* knob de laboratorio: con CFG_VIB_SIM != 0 la
			 * medicion se sustituye (para probar sin motor) */
			if (CFG_VIB_SIM > 0) {
				desvio = CFG_VIB_SIM;
			}
			atomic_set(&vib, desvio);
			atomic_set(&estado, IMU_OK);
		} else {
			/* sin dato fresco: se reporta y se sigue intentando */
			atomic_set(&vib, 0);
			atomic_set(&estado, listo ? IMU_ERR_LECTURA : IMU_SIN_SENSOR);
		}

		/* Aviso a la FSM: solo en el cambio confirmado */
		if (atomic_get(&vib) > CFG_VIB_LIMITE) {
			altas++;
			normales = 0;
		} else {
			normales++;
			altas = 0;
		}
		if (altas == CFG_VIB_MUESTRAS) {
			fsm_post(FSM_EVT_VIB_ALTA);
		}
		if (normales == CFG_VIB_MUESTRAS) {
			fsm_post(FSM_EVT_VIB_OK);
		}

		k_msleep(IMU_PERIODO_MS);
	}
}

int imu_init(void)
{
	k_thread_create(&imu_hilo, imu_stack, IMU_STACK, imu_fn, NULL, NULL, NULL,
			IMU_PRIO, 0, K_NO_WAIT);
	return 0;
}
