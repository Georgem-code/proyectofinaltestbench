# Montaje, compilación y pruebas

## Cableado

### Blue Pill

| Pin Blue Pill | Va a | Nota |
|---|---|---|
| PA0 (TIM2_CH1) | ENA del puente H (L298N) | PWM de velocidad, 1 kHz |
| PB12 | IN1 del puente H | Dirección |
| PB13 | IN2 del puente H | Dirección (siempre inactivo en este firmware) |
| PB14 | Salida del encoder óptico | Flanco de subida, pull-down interno |
| PB0 (TIM3_CH3) | Señal del servo | 50 Hz, ancho 600–2400 µs |
| PA8 (TIM1_CH1) | Bocina | Tonos de 600–2000 Hz |
| PB6 / PB7 | SCL / SDA del MPU6050 (GY-521) | I2C a 100 kHz; **AD0 a 0** → dirección 0x68 |
| PA2 (TX) | RX del ESP32 (GPIO 32) | Cruzado |
| PA3 (RX) | TX del ESP32 (GPIO 33) | Cruzado |
| PC13 | LED integrado de la placa | Activo bajo |

Alimentación: el GND de la Blue Pill, el ESP32, el puente H y el
MPU6050 deben compartir masa. Para flashear, SWD (ST-Link) o el
cargador de serie de Zephyr.

### ESP32

| Pin ESP32 | Va a |
|---|---|
| 33 (TX) | PA3 de la Blue Pill |
| 32 (RX) | PA2 de la Blue Pill |
| 25 / 26 / 27 | TM1638: STB / CLK / DIO |
| 13 / 14 | OLED SSD1306: SDA / SCL (0x3C) |

## Compilar y flashear el STM32

```bash
west build -b <tu_placa_bluepill> stm32_zephyr
west flash
```

`prj.conf` y `app.overlay` se toman automáticamente por estar en la
raíz de la aplicación. Requisitos de la placa: que existan los nodos
`usart2`, `i2c1`, `pwm1`, `pwm2`, `pwm3`, `gpiob` y `gpioc` (lo mismo
que usaba el overlay original).

El firmware espera **Zephyr ≥ 3.0** (sintaxis pinctrl actual).

## Instalar el ESP32

Necesita la librería `ssd1306` (MicroPython):
`mpremote mip install ssd1306` (o copiar `ssd1306.py` a la placa).
Luego:

```bash
mpremote cp hmiesp32/*.py :/
mpremote reset            # main.py arranca con el boot
```

Con Thonny: copiar los cinco archivos a la raíz de la placa y
rearrancar. La consola REPL muestra cada trama recibida.

## Plan de pruebas

### Sin banco conectado (humo)

1. Encender: el LED PC13 late a 500 ms, el TM1638 muestra `0  0.00`,
   la consola del ESP32 imprime una trama por segundo con
   `fsm_state: IDLE` y `i2c_sensor: SIN_SENSOR` (o `MPU6050` si está
   conectado).
2. Pulsar S1: el LED del TM1638 pasa al índice 1, la trama pasa a
   `ACCEL` y `pwm_duty` sube de 5 en 5 hasta 100.
3. Tras 5 s pasa a `FALLA` (bocina a golpes de 1000 Hz) y 2 s después
   vuelve a `IDLE` (vibración normal).
4. Pulsar S2 en cualquier momento: `EMERGENCIA` (LED rápido de la
   Blue Pill, 2000 Hz, LED del TM1638 en 4) y solo S3 sale.

### Simular el fallo sin motor

Poner `CFG_VIB_SIM` a `100` en `stm32_zephyr/src/app_config.h`,
recompilar y flashear. El ensayo termina siempre en `EMERGENCIA`
tras pasar por `FALLA`. Volver a `0` para usar el sensor real.

### Con el banco completo

1. Arrancar con S1: el motor acelera por la rampa y el TM1638/OLED
   muestran las RPM crecientes.
2. Golpear o desequilibrar el motor durante `STEADY`: la vibración
   medida (`val_i2c`) sube; con `CFG_VIB_MUESTRAS` lecturas seguidas
   sobre `CFG_VIB_LIMITE` el banco declara `FALLA` y, si persiste,
   `EMERGENCIA` (motor a 0 y almohadilla apoyada).
3. S3 reinicia a `IDLE` en cualquier momento.

### Integridad del enlace

Con la línea UART floja o desconectada a mitad de trama, el OLED
muestra el contador `CRC err:` creciendo y el resto del HMI sigue
funcionando.

## Diagnóstico de problemas

| Síntoma | Causa probable / solución |
|---|---|
| `SIN_SENSOR` siempre | MPU6050 sin alimentación o mal cableado; AD0 a 1 → cambiar `reg` a `<0x69>` en `app.overlay` |
| `ERR_LECTURA` intermitente | I2C ruidoso: bajar la frecuencia o acortar cables; el firmware reintenta solo |
| El ESP32 no recibe nada | Cruzar TX/RX (PA2↔32, PA3↔33); baudios 115200; verificar que el TX del STM32 esté en PA2 |
| Tramas basura / `CRC err` alto | Masa común entre placas; baudios; ruido en el cable UART |
| El motor no gira | Jumper ENA del puente H (debe recibir PA0); alimentación del motor; comprobrar PB12/PB13 |
| RPM a 0 con el motor girando | Disco del encoder: ranuras alineadas con PB14; `CFG_PPV` ajustado al disco real |
| El servo tiembla | Alimentación de 5 V dedicada para el servo (no desde el regulador de la Blue Pill) |
| `pwm3`/`tim3_ch3_pb0` no compila | Placa sin el nodo `pwm3`: declarar `pwm3: pwm` bajo `&timers3` como en `app.overlay` |
