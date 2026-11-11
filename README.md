# Banco de Pruebas de Carga

Banco de pruebas de un motor DC con freno de emergencia por servo:

- **STM32 Blue Pill (STM32F103C8, Zephyr)** — control en tiempo real:
  FSM del ensayo, motor, servo (almohadilla de frenado), bocina,
  MPU6050 (vibración) y encoder (velocidad). Todo con hilos,
  semáforos, colas y mensajes: **el `main` no ejecuta nada
  bloqueante**.
- **ESP32 (MicroPython)** — HMI: pantalla OLED, display TM1638,
  teclas y consola. Envía los comandos `G` (arrancar), `E`
  (emergencia) y `R` (reiniciar).

Se hablan por UART a 115200 baudios con tramas JSON protegidas con
CRC-16/CCITT.

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/arquitectura.md`](docs/arquitectura.md) | Modelo de concurrencia, hilos, sincronización y flujos de datos |
| [`docs/stm32.md`](docs/stm32.md) | Referencia del firmware Zephyr: devicetree, módulos, FSM y ajustes |
| [`docs/esp32.md`](docs/esp32.md) | Referencia del HMI MicroPython: clases y pines |
| [`docs/protocolo.md`](docs/protocolo.md) | Trama JSON + CRC-16 y comandos `G/E/R` |
| [`docs/montaje.md`](docs/montaje.md) | Cableado, compilación, plan de pruebas y diagnóstico |

## Estructura

```
proyectofinal/
├── stm32_zephyr/          firmware de la Blue Pill (aplicación Zephyr)
│   ├── CMakeLists.txt
│   ├── prj.conf           configuración del kernel/drivers
│   ├── app.overlay        TODO el cableado (pines, PWM, I2C, MPU6050)
│   ├── src/               un módulo por responsabilidad (ver docs/stm32.md)
│   └── old/               código original de referencia
├── hmiesp32/              firmware del ESP32 (MicroPython, modular)
│   ├── main.py            bucle principal no bloqueante
│   ├── tm1638.py          driver del display TM1638
│   ├── oled_display.py    pantalla OLED SSD1306
│   ├── protocol.py        CRC-16 + parseo de tramas JSON
│   └── buttons.py          teclas S1/S2/S3 -> comandos G/E/R
└── hmiesp32labantiguo/    HMI original de referencia
```

## Arranque rápido

```bash
# STM32
west build -b <tu_placa_bluepill> stm32_zephyr
west flash

# ESP32 (requiere libreria ssd1306)
mpremote mip install ssd1306
mpremote cp hmiesp32/*.py :/
mpremote reset
```

Los ajustes del ensayo (umbrales, tiempos, simulación de fallo) están
en `stm32_zephyr/src/app_config.h`; el cableado, en
`stm32_zephyr/app.overlay`.
