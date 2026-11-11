# Firmware ESP32 (MicroPython) — referencia

HMI del banco en `hmiesp32/`. Cinco archivos que se copian juntos a
la placa (raíz del sistema de archivos) y `main.py` arranca con el
boot. Requiere la librería `ssd1306.py` (habitual en MicroPython; si
no está, ver [montaje.md](montaje.md)).

```
hmiesp32/
├── main.py         bucle principal no bloqueante (20 ms)
├── tm1638.py       driver del display TM1638
├── oled_display.py pantalla OLED SSD1306
├── protocol.py     CRC-16 + parseo de tramas JSON
└── buttons.py      teclas S1/S2/S3 → comandos G/E/R
```

## Pines

| Pin | Función |
|---|---|
| 33 / 32 | UART2 TX / RX (cruzados con PA3 / PA2 de la Blue Pill) |
| 25 / 26 / 27 | TM1638: STB / CLK / DIO |
| 13 / 14 | OLED: SDA / SCL (SoftI2C, dirección 0x3C) |

## `main.py`

Constantes de cableado arriba, luego:

```python
def actualizar_hmi(tm, oled, receptor, trama):   # TM1638 + OLED + consola
def main():
    tm = TM1638(TM_STB, TM_CLK, TM_DIO)
    oled = PantallaOLED(OLED_SDA, OLED_SCL)     # opcional: puede fallar
    teclas = Teclas(tm)
    receptor = Receptor()
    uart = UART(2, baudrate=115200, tx=UART_TX, rx=UART_RX)

    while True:
        for trama in receptor.alimentar(uart.read()):
            actualizar_hmi(tm, oled, receptor, trama)
        for comando in teclas.barrer():
            uart.write(comando.encode())
        time.sleep_ms(20)
```

Salida de consola por cada trama válida:

```
EST:STEADY    PWM:100% RPM: 1420 Sen:MPU6050      Vib: 0.12 m/s2
```

## `tm1638.py` — clase `TM1638`

| Método | Descripción |
|---|---|
| `__init__(stb, clk, dio)` | Tres pines de salida; limpia el buffer |
| `mostrar(izq, der)` | Escribe hasta 4 dígitos a cada lado (RPM \| vibración) |
| `leds(indice)` | Enciende un único LED (0-7); el índice es el estado del banco |
| `teclas()` | Lee las 8 teclas: bit 0 = S1, bit 1 = S2, … |
| `flush()` | Vuelca el buffer al display (hay que llamarlo tras `mostrar`/`leds`) |
| `celdas(s)` | Texto → segmentos; el punto decimal se fusiona con el dígito anterior |

Comandos de nivel bajo (`_byte`, `_cmd`) y la rutina de lectura de
teclas (DIO conmutado a entrada) están documentados en el propio
archivo.

## `protocol.py` — `crc16()` y clase `Receptor`

```python
crc16(data: bytes) -> int      # CRC-16/CCITT, igual que la Blue Pill

class Receptor:
    errores_crc   # tramas rechazadas por CRC (se muestra en el OLED)
    tramas_malas  # tramas con CRC bueno pero JSON incorrecto

    alimentar(datos) -> list[dict]
```

`alimentar()` acumula los bytes crudos del UART, corta por `'\n'`,
valida el CRC y parsea el JSON. Cada trama válida sale como dict con
claves `estado`, `duty`, `rpm`, `sensor` y `vib`. Una trama sin CRC
válido se imprime como diagnóstico `RX (CRC mal): …` y se cuenta. Si
el buffer acumulado supera 400 bytes (media trama corrupta sin fin de
línea) se descarta entero.

## `oled_display.py` — clase `PantallaOLED`

| Método | Descripción |
|---|---|
| `__init__(sda, scl, ...)` | Inicializa el SSD1306; si falla, deja el objeto inerte (el resto del HMI sigue) |
| `mostrar(est, duty, rpm, sensor, vib, errores_crc)` | Repinta las 7 líneas |
| `alerta(est, sensor)` | Línea de estado: `!! EMERGENCIA !!`, `!! FALLA !!`, `SENSOR NO LEE` o `OK` |

Pantalla (128×64):

```
 Est:STEADY
 PWM:100%
 RPM:1420
 Sen:MPU6050
 Vib:0.12
 OK
 CRC err:0
```

## `buttons.py` — clase `Teclas`

```python
Teclas(tm1638, mapa=None)   # mapa por defecto: S1→'G', S2→'E', S3→'R'
barrer() -> list[str]       # comandos en flanco de subida, sin repetición
```

La detección de flanco evita que una pulsación mantenida envíe el
comando en cada vuelta del bucle.
