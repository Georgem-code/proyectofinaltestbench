"""ESP32 - HMI del Banco de Pruebas.

 Modulos (todo vive en este directorio, copiado a la placa):
   tm1638.py       display de 8 digitos + LEDs + teclas
   oled_display.py pantalla OLED SSD1306
   protocol.py     tramas JSON con CRC-16
   buttons.py      teclas S1/S2/S3 -> comandos G/E/R

 Bucle principal no bloqueante: UART -> HMI -> consola -> teclas,
 todo en ventanas de 20 ms.
"""

from machine import UART
import time

from buttons import Teclas
from oled_display import PantallaOLED
from protocol import Receptor
from tm1638 import TM1638

# --- Cableado ---
UART_TX = 33      # hacia RX de la Blue Pill (PA3)
UART_RX = 32      # desde TX de la Blue Pill (PA2)
TM_STB = 25
TM_CLK = 26
TM_DIO = 27
OLED_SDA = 13
OLED_SCL = 14

# LED del TM1638 encendido segun el estado del banco
LED_POR_ESTADO = {
    "IDLE": 0, "ACCEL": 1, "STEADY": 2, "FALLA": 3, "EMERGENCIA": 4,
}


def actualizar_hmi(tm, oled, receptor, trama):
    """Pinta una trama valida en TM1638, OLED y consola."""
    est = trama["estado"]
    duty = trama["duty"]
    rpm = trama["rpm"]
    sensor = trama["sensor"]
    vib = trama["vib"]

    print("EST:%-9s PWM:%3d%% RPM:%5d Sen:%-12s Vib:%5.2f m/s2"
          % (est, duty, rpm, sensor, vib))

    tm.mostrar(str(rpm), "%.2f" % vib)
    tm.leds(LED_POR_ESTADO.get(est, 0))
    tm.flush()
    oled.mostrar(est, duty, rpm, sensor, vib, receptor.errores_crc)


def main():
    tm = TM1638(TM_STB, TM_CLK, TM_DIO)
    oled = PantallaOLED(OLED_SDA, OLED_SCL)
    teclas = Teclas(tm)
    receptor = Receptor()
    uart = UART(2, baudrate=115200, tx=UART_TX, rx=UART_RX)

    tm.mostrar("0", "0.00")
    tm.leds(0)
    tm.flush()

    while True:
        for trama in receptor.alimentar(uart.read()):
            actualizar_hmi(tm, oled, receptor, trama)

        for comando in teclas.barrer():
            uart.write(comando.encode())
            print("tecla ->", comando)

        time.sleep_ms(20)


main()
