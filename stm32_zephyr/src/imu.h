#ifndef IMU_H
#define IMU_H

#include <stdbool.h>
#include <stdint.h>

/* Lectura del MPU6050 con el driver oficial de Zephyr (sensor
 * subsystem, CONFIG_MPU6050). El hilo se crea con imu_init(); el
 * resto de los modulos solo usa los getters (atomicos, sin bloqueo).
 */

enum imu_estado {
	IMU_SIN_SENSOR = 0,   /* no respondio al arrancar            */
	IMU_OK = 1,           /* "MPU6050"                            */
	IMU_ERR_LECTURA = 2,  /* fallo una lectura (se reintenta)    */
};

/* Crea el hilo del sensor. 0 = ok. */
int imu_init(void);

/* Vibracion medida: desviacion de la gravedad, m/s^2 x100. */
int32_t imu_vib(void);

/* true si la ultima medicion supera el umbral CFG_VIB_LIMITE. */
bool imu_vib_alta(void);

/* Ultimo estado del sensor (enum imu_estado). */
uint8_t imu_estado(void);

#endif /* IMU_H */
