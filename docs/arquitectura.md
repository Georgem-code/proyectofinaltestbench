# Arquitectura del Banco de Pruebas

El sistema son dos microcontroladores con papeles bien separados:

| Nodo | Papel |
|---|---|
| **STM32 Blue Pill** (STM32F103C8, Zephyr) | Control en tiempo real: FSM del ensayo, actuadores (motor, servo, bocina), adquisición (encoder, MPU6050) y telemetría. |
| **ESP32** (MicroPython) | HMI: pantalla OLED, display TM1638, teclas y consola. No controla nada: traduce tramas a pantalla y pulsaciones a comandos. |

```
        comandos 'G'/'E'/'R'                     tramas JSON + CRC-16
 ESP32 ────────────────────────▶ STM32 ──────────────────────────▶ ESP32
 (teclas S1/S2/S3, UART 115200)   (usart2 PA2/PA3)                 (OLED, TM1638, consola)
```

## Modelo de concurrencia del STM32

**Regla del proyecto: el `main` no ejecuta lógica ni bloquea.** Solo
inicializa los módulos, crea los hilos y se duerme para siempre
(`k_sleep(K_FOREVER)`). Todo el trabajo ocurre entre hilos.

### Hilos

| Hilo | Módulo | Stack | Prio | Se bloquea en |
|---|---|---:|---:|---|
| FSM | `fsm.c` | 768 | 5 | `k_event_wait()` (eventos o plazo del estado) |
| IMU | `imu.c` | 1024 | 6 | `k_msleep(200)` entre lecturas del MPU6050 |
| Comandos RX | `comms.c` | 640 | 6 | `k_msgq_get()` sobre la cola de bytes |
| Encoder | `encoder.c` | 512 | 4 | `k_sleep(1 s)` (ventana de conteo) |
| Muestreo | `telemetria.c` | 512 | 3 | `k_sleep(CFG_TELEM_MS)` |
| TX | `telemetria.c` | 1536 | 7 | `k_msgq_get()` sobre la cola de tramas |
| Indicadores | `indicadores.c` | 512 | 2 | `k_msleep(20)` entre refrescos |

Prioridades preemptivas de Zephyr (número mayor = prioridad mayor).
Total de stacks de aplicación: ~5,5 KB, dentro del margen de los
20 KB de RAM de la Blue Pill.

### Objetos de sincronización

| Objeto | Tipo | Productor | Consumidor | Para qué |
|---|---|---|---|---|
| `fsm_eventos` | `k_event` | hilos comms e IMU | hilo FSM | Bus de eventos: cada bit (`FSM_EVT_*`) es un semáforo binario. La FSM duerme aquí. |
| `enc_sem` | `k_sem` (contador, límite 8192) | ISR del encoder | hilo encoder | Cada pulso del disco es un `give`; el hilo drena la ventana y los `take` exitosos son los pulsos contados. |
| `cola_rx` | `k_msgq` (8 × byte) | ISR del UART | hilo comandos | Pasar los bytes recibidos de la ISR al hilo sin bloquear la ISR. |
| `cola_telem` | `k_msgq` (4 × `struct trama`) | hilo muestreo | hilo TX | Productor/consumor de telemetría. Si se llena, se descarta la trama. |

### Estado compartido

Los valores que varios hilos leen y uno solo escribe (estado de la
FSM, duty, RPM, vibración, estado del sensor) son `atomic_t`: lectura
consistente sin mutex y sin bloquear a nadie.

## Flujos de datos

**Camino de un comando** (pulsar S1 en el ESP32):

```
S1 ─▶ uart.write('G') ─▶ ISR UART del STM32 ─▶ cola_rx (k_msgq)
   ─▶ hilo comandos: 'G' ─▶ fsm_post(FSM_EVT_GO) ─▶ despierta la FSM
   ─▶ ir_a(FSM_ACCEL): motor a 0, servo a 90°
```

**Camino de la vibración** (fallo del ensayo):

```
MPU6050 ─ sensor_sample_fetch ─▶ |acel_z − gravedad| ─▶ 3 lecturas
   seguidas sobre CFG_VIB_LIMITE ─▶ fsm_post(FSM_EVT_VIB_ALTA)
   ─▶ FSM (STEADY) ─▶ FALLA ─▶ EMERGENCIA (motor a 0, almohadilla a 0°)
```

**Camino de la telemetría:**

```
hilo muestreo (1 s) ─▶ fotografía {estado, duty, rpm, vib, sensor}
   ─▶ cola_telem ─▶ hilo TX: JSON + CRC-16 ─▶ uart_poll_out ─▶ ESP32
   ─▶ Receptor (CRC + JSON) ─▶ TM1638 + OLED + consola
```

**Camino del encoder:**

```
flanco en PB14 ─▶ ISR: k_sem_give ─▶ ... pulso a pulso ...
hilo encoder (ventana 1 s): drena el semáforo ─▶ pulsos ─▶ RPM
   ─▶ promedio móvil de CFG_N_PROM ventanas ─▶ atomic rpm_prom
```

## Modelo del ESP32

Bucle único no bloqueante en `main.py`, con periodo de 20 ms:

1. `Receptor.alimentar(uart.read())` — lee el UART, valida CRC,
   parsea el JSON y devuelve las tramas válidas del ciclo.
2. Por cada trama: TM1638 (RPM | vibración + LED del estado),
   OLED y una línea en la consola.
3. `Teclas.barrer()` — flancos de subida de S1/S2/S3 → `'G'/'E'/'R'`.

No hay hilos ni interrupciones en el ESP32: el bucle es corto
(bit-bang del TM1638 incluido) y cabe holgado en los 20 ms.

## Decisiones de diseño

- **FSM dirigida por eventos, no por sondeo.** La FSM no lee UART ni
  sensores: recibe avisos y usa el timeout de `k_event_wait()` como
  reloj de los plazos de cada estado. Una sola primitiva cubre
  "esperar un comando" y "esperar 5 segundos".
- **ISR mínimas.** Las interrupciones solo hacen `k_msgq_put` /
  `k_sem_give` y vuelven; el trabajo pesado queda en hilos.
- **Un módulo = una responsabilidad = una carpeta de pares `.h/.c`.**
  Ver [`stm32.md`](stm32.md) y [`esp32.md`](esp32.md).
- **Protocolo con CRC** para tolerar ruido en la línea UART: una
  trama corrupta se descarta y se cuenta, nunca se muestra.
- **Degradación elegante:** si el MPU6050 no responde, el banco sigue
  y la telemetría reporta `SIN_SENSOR`; si el OLED del ESP32 falla, el
  TM1638 y la consola siguen funcionando.
