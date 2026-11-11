# Firmware STM32 (Zephyr) — referencia

Aplicación Zephyr estándar en `stm32_zephyr/`:

```
stm32_zephyr/
├── CMakeLists.txt   fuentes de src/ (Zephyr toma prj.conf y app.overlay solos)
├── prj.conf         configuración del kernel y drivers
├── app.overlay      TODO el cableado del banco (devicetree)
├── src/             un par .h/.c por responsabilidad
└── old/             firmware original de referencia
```

## Devicetree (`app.overlay`)

Ningún pin aparece en el C. Tabla completa de nodos:

| Nodo | Hardware | Uso |
|---|---|---|
| `&usart2` | PA2/PA3, 115200 | Enlace con el ESP32 (RX por interrupción) |
| `&timers1` + `&pwm1` | PA8 = TIM1_CH1 (canal 1) | Bocina |
| `&timers2` + `&pwm2` | PA0 = TIM2_CH1 (canal 1) | Velocidad del motor (1 kHz) |
| `&timers3` + `&pwm3` | PB0 = TIM3_CH3 (canal 3) | Servo de la almohadilla (50 Hz, 600–2400 µs) |
| `&i2c1` + `mpu6050@68` | PB6/PB7 | MPU6050 con el **driver oficial** (`invensense,mpu6050`) |
| `status_leds/status_led` | PC13 (`gpio-leds`, activo bajo) | LED de latido |
| `zephyr,user` → `motor-in1-gpios` | PB12 | IN1 del puente H |
| `zephyr,user` → `motor-in2-gpios` | PB13 | IN2 del puente H |
| `zephyr,user` → `encoder-gpios` | PB14 (con pull-down) | Encoder óptico |

Notas:

- `st,prescaler = <71>` deja los timers contando a 1 MHz (72 MHz/72):
  1 kHz de PWM de motor, 50 Hz de servo y tonos de 600–2000 Hz salen
  de resolución entera directa.
- Si el MPU6050 tiene **AD0 a nivel alto**, cambia `reg = <0x68>` por
  `<0x69>` en el nodo `mpu6050`.
- Los GPIO "genéricos" (IN1/IN2, encoder) van en el nodo `zephyr,user`
  y se toman en C con `GPIO_DT_SPEC_GET(DT_PATH(zephyr,user), ...)`.
  El número de canal PWM sí se indica en C (una constante por módulo,
  `MOTOR_CANAL`/`SERVO_CANAL`/`TONO_CANAL`) porque es inherente al
  pin elegido en el overlay.

## `prj.conf`

| Opción | Motivo |
|---|---|
| `CONFIG_SERIAL=y`, `CONFIG_UART_INTERRUPT_DRIVEN=y` | Comandos por IRQ en vez de sondeo |
| `CONFIG_I2C=y`, `CONFIG_SENSOR=y`, `CONFIG_MPU6050=y` | Driver oficial del MPU6050 |
| `CONFIG_GPIO=y`, `CONFIG_PWM=y` | Actuadores y encoder |
| `CONFIG_UART_CONSOLE=n` | Deja `usart2` exclusivamente para el ESP32 |

## Módulos y API

### `main.c`

Solo llama a los `_init()` de cada módulo (aborta si falla
`actuadores_init`, porque sin PWM/GPIO no hay banco) y termina en
`k_sleep(K_FOREVER)`. No hay lógica de aplicación aquí.

### `fsm.h/.c` — máquina de estados

```c
enum fsm_estado { FSM_IDLE, FSM_ACCEL, FSM_STEADY, FSM_FALLA, FSM_EMERG };

#define FSM_EVT_GO        BIT(0)   /* 'G' del ESP32            */
#define FSM_EVT_EMERG     BIT(1)   /* 'E' del ESP32            */
#define FSM_EVT_RESET     BIT(2)   /* 'R' del ESP32            */
#define FSM_EVT_VIB_ALTA  BIT(3)   /* imu: vibración alta      */
#define FSM_EVT_VIB_OK    BIT(4)   /* imu: vibración normal    */

int fsm_init(void);
void fsm_post(uint32_t eventos);   /* publica en el bus (cualquier hilo) */
int fsm_estado(void);              /* estado actual                      */
const char *fsm_nombre(int estado);/* "IDLE".."EMERGENCIA"                */
```

Transiciones (los tiempos son ajustables en `app_config.h`):

```
 IDLE ──'G'──▶ ACCEL ──rampa 5%/200ms──▶ STEADY ──5 s o VIB_ALTA──▶ FALLA
   ▲               │                                                   │
   │◀──────'R'─────┼──────────────────────────'R'───────────── EMERG ◀┼─ 'E' en
   │               'E'──▶ EMERG                          2 s: │       │   cualquier
   └◀── vib normal ─┴─────────────────────────────────────┘       estado
```

Acciones de entrada (`ir_a()`):

| Estado | Motor | Servo |
|---|---|---|
| `IDLE` | 0 % | 0° (almohadilla apoyada) |
| `ACCEL` | 0 % (la rampa arranca) | 90° |
| `STEADY` | (rampa ya al 100 %) | 180° (totalmente apartada) |
| `FALLA` | — | — |
| `EMERG` | 0 % | 0° (frenado de emergencia) |

`EMERGENCIA` solo sale con `'R'` (botón S3). En `FALLA`, si llega
`FSM_EVT_VIB_ALTA` pasa a `EMERGENCIA` de inmediato; si vence el
plazo, decide consultando `imu_vib_alta()`.

### `actuadores.h/.c` — salidas físicas

```c
int  actuadores_init(void);      /* configura PWM y dirección; 0 = ok  */
void act_motor(uint8_t pct);     /* duty 0-100 %, arma IN1/IN2          */
uint8_t act_motor_duty(void);    /* duty vigente                        */
void act_servo(uint16_t grados); /* 0-180° (0° = almohadilla apoyada)   */
void act_tono(uint32_t hz);      /* Hz de la bocina; 0 = silencio      */
```

Los tres PWM son por hardware: el servo ya no se genera con
`k_busy_wait` como en el firmware original.

### `imu.h/.c` — MPU6050 (driver oficial)

```c
enum imu_estado { IMU_SIN_SENSOR = 0, IMU_OK = 1, IMU_ERR_LECTURA = 2 };

int     imu_init(void);       /* crea el hilo; 0 = ok                  */
int32_t imu_vib(void);        /* vibración: |acel_z − gravedad| ×100   */
bool    imu_vib_alta(void);   /* ¿supera CFG_VIB_LIMITE?               */
uint8_t imu_estado(void);     /* último resultado (enum imu_estado)    */
```

- Lectura con `sensor_sample_fetch()` + `sensor_channel_get(..., SENSOR_CHAN_ACCEL_Z, ...)`
  y rango ±2 g fijado con `sensor_attr_set()`. No hay acceso raw al bus I2C.
- Periodo de 200 ms. La vibración es la **desviación respecto de la
  gravedad** (en reposo ≈ 0,00 m/s²), lo que permite un umbral bajo y
  absoluto.
- El aviso a la FSM exige `CFG_VIB_MUESTRAS` lecturas consecutivas
  sobre el umbral (antirruido) y se publica una sola vez por cambio.
- Con `CFG_VIB_SIM > 0` la medición se sustituye por ese valor
  (probar la EMERGENCIA sin motor).

### `encoder.h/.c` — velocidad

```c
int encoder_init(void);        /* pin, IRQ y hilo; 0 = ok   */
uint16_t encoder_rpm(void);   /* RPM promediadas           */
```

La ISR solo hace `k_sem_give()` del semáforo contador. El hilo, con
ventanas de 1 s, drena el semáforo y calcula
`RPM = pulsos · 60000 / (CFG_PPV · ventana_ms)`, luego promedia las
últimas `CFG_N_PROM` ventanas.

### `comms.h/.c` — comandos por UART

```c
int comms_init(void);   /* habilita IRQ de RX y crea el hilo; 0 = ok */
```

ISR → `cola_rx` (`k_msgq`) → hilo de comandos → `fsm_post()`.
Bytes que no son `G`/`E`/`R` se ignoran como ruido.

### `telemetria.h/.c` — trama periódica

```c
struct trama {
    uint8_t  estado;   /* enum fsm_estado */
    uint8_t  duty;     /* %              */
    uint16_t rpm;      /* promediadas    */
    int32_t  vib;      /* m/s² ×100      */
    uint8_t  sensor;   /* enum imu_estado */
};

int telemetria_init(void);   /* hilos muestreo y TX; 0 = ok */
```

Hilo muestreo (cada `CFG_TELEM_MS`) → `cola_telem` → hilo TX:
formatea el JSON, calcula el CRC-16 del cuerpo y lo manda con
`uart_poll_out()`. El formato exacto está en
[protocolo.md](protocolo.md).

### `indicadores.h/.c` — percepción

```c
int indicadores_init(void);   /* LED y hilo; 0 = ok */
```

LED PC13: parpadeo de 500 ms (sistema vivo) o 100 ms (`EMERGENCIA`).
Bocina: 600 Hz en `ACCEL` (pitido corto por segundo), 1000 Hz en
`FALLA`, 2000 Hz intermitente en `EMERGENCIA`, silencio en el resto.
Lee `fsm_estado()`: no participa en las decisiones.

### `crc16.h/.c`

```c
uint16_t crc16(const uint8_t *datos, size_t n);
```

CRC-16/CCITT (poly `0x1021`, inicio `0xFFFF`), idéntico al del ESP32.

## Ajustes (`src/app_config.h`)

| Constante | Valor por defecto | Efecto |
|---|---:|---|
| `CFG_T_STEADY_MS` | 5000 | Duración del ensayo antes de `FALLA` |
| `CFG_T_FALLA_MS` | 2000 | Tiempo de alarma antes de decidir |
| `CFG_RAMPA_MS` / `CFG_RAMPA_PASO` | 200 / 5 | Ritmo de la rampa (5 % cada 200 ms) |
| `CFG_TELEM_MS` | 1000 | Periodo de la trama de telemetría |
| `CFG_PPV` | 20 | Pulsos por vuelta del disco del encoder |
| `CFG_N_PROM` | 3 | Ventanas de RPM promediadas |
| `CFG_G_CENTI` | 981 | Gravedad de referencia (m/s² ×100) |
| `CFG_VIB_LIMITE` | 50 | Umbral de vibración (m/s² ×100) |
| `CFG_VIB_MUESTRAS` | 3 | Lecturas consecutivas para confirmar |
| `CFG_VIB_SIM` | 0 | `0` = sensor real; `100` = simula fallo |
| `CFG_SERVO_US_MIN` | 600 | Pulso del servo a 0° (µs) |

## Consumo de recursos

- Stacks de aplicación: 5504 B (ver tabla en
  [arquitectura.md](arquitectura.md)).
- Buffer de TX: `char linea[240]` estático del hilo TX.
- Colas: 8 bytes (`cola_rx`) + 4 tramas (`cola_telem`).
- Sin allocations dinámicas ni `printk`: todo estático, pensado para
  los 20 KB de RAM de la Blue Pill.
