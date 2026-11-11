# Protocolo de comunicación

Enlace físico: UART a **115200 baudios, 8N1**, entre `usart2` de la
Blue Pill (PA2 = TX, PA3 = RX) y la UART2 del ESP32 (TX = 33,
RX = 32, cruzados).

## Trama de telemetría (STM32 → ESP32)

Una por segundo (`CFG_TELEM_MS`), terminada en `'\n'`:

```json
{"nodo":"STM32","planta":"BANCO","fsm_state":"IDLE","pwm_duty":0,"rpm":0,"i2c_sensor":"MPU6050","val_i2c":0.00,"crc":"FB3C"}
```

| Campo | Tipo | Valores / unidades |
|---|---|---|
| `nodo` | texto | Siempre `"STM32"` |
| `planta` | texto | Siempre `"BANCO"` |
| `fsm_state` | texto | `IDLE`, `ACCEL`, `STEADY`, `FALLA`, `EMERGENCIA` |
| `pwm_duty` | entero | 0–100 (%) |
| `rpm` | entero | RPM promediadas (últimas `CFG_N_PROM` ventanas) |
| `i2c_sensor` | texto | `MPU6050`, `ERR_LECTURA` o `SIN_SENSOR` |
| `val_i2c` | decimal | Vibración medida en m/s² (desviación de la gravedad, 2 decimales) |
| `crc` | texto | CRC-16 del **cuerpo** de la trama, 4 hexadecimales |

## Comandos (ESP32 → STM32)

Un byte por pulsación, sin marco ni CRC (el STM32 ignora cualquier
otro byte):

| Byte | Tecla | Acción |
|---|---|---|
| `G` | S1 | Arrancar el ensayo (solo desde `IDLE`) |
| `E` | S2 | Parada de emergencia (desde cualquier estado) |
| `R` | S3 | Reiniciar a `IDLE` (única salida de `EMERGENCIA`) |

## CRC-16/CCITT

Polinomio `0x1021`, valor inicial `0xFFFF`, sin reflexión ni XOR
final. Se calcula sobre todos los bytes anteriores a `,"crc":"`
(es decir, el cuerpo sin la llave de cierre). La misma rutina vive en
`stm32_zephyr/src/crc16.c` y `hmiesp32/protocol.py`.

Ejemplo real — cuerpo:

```json
{"nodo":"STM32","planta":"BANCO","fsm_state":"IDLE","pwm_duty":0,"rpm":0,"i2c_sensor":"MPU6050","val_i2c":0.00
```

CRC del cuerpo: `0xFB3C` → trama completa terminada en:

```json
...,"crc":"FB3C"}\n
```

## Comportamiento ante errores

| Situación | Conducta del receptor (ESP32) |
|---|---|
| CRC no coincide | La trama se descarta, se imprime `RX (CRC mal): …` y sube `errores_crc` (visible en el OLED) |
| JSON inválido con CRC bueno | Se descarta y sube `tramas_malas` |
| Buffer > 400 bytes sin `'\n'` | Se vacía entero (línea zombi) |
| Sensor ausente en el STM32 | La trama sigue llegando con `i2c_sensor = "SIN_SENSOR"` y `val_i2c = 0.00` |
