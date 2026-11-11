/*
 * Banco de Pruebas - ajustes del ensayo (STM32 Blue Pill)
 *
 * Todos los numeros "de laboratorio" viven aqui: se cambia el ensayo
 * sin tocar la logica de los modulos.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Ensayo ---- */
#define CFG_T_STEADY_MS   5000  /* tiempo en STEADY antes de FALLA        */
#define CFG_T_FALLA_MS    2000  /* tiempo en FALLA antes de decidir        */
#define CFG_RAMPA_MS       200  /* cada cuanto sube el duty durante ACCEL  */
#define CFG_RAMPA_PASO       5  /* cuanto sube el duty en cada paso (%)    */
#define CFG_TELEM_MS       1000  /* periodo de la trama de telemetria (ms) */

/* ---- Encoder ---- */
#define CFG_PPV             20   /* pulsos por vuelta del disco            */
#define CFG_N_PROM           3   /* ventanas de RPM que se promedian       */

/* ---- Vibracion (MPU6050) ---- */
#define CFG_G_CENTI        981   /* gravedad en m/s^2 x100 (se descuenta)  */
#define CFG_VIB_LIMITE      50   /* umbral de vibracion, m/s^2 x100        */
#define CFG_VIB_MUESTRAS     3   /* lecturas seguidas para confirmar       */
#define CFG_VIB_SIM           0   /* 0 = sensor real. 100 = simula vibracion
				     fuerte y fuerza la EMERGENCIA          */

/* ---- Servo ---- */
#define CFG_SERVO_US_MIN   600   /* ancho de pulso a 0 grados, en us       */

#endif /* APP_CONFIG_H */
