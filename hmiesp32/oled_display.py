"""Pantalla OLED SSD1306 128x64 por I2C por software.

 Si el OLED no responde, el resto del HMI sigue funcionando.
"""

from machine import Pin, SoftI2C
import ssd1306


class PantallaOLED:
    """Pantalla del banco: 7 lineas con el resumen de la telemetria."""

    def __init__(self, sda=13, scl=14, ancho=128, alto=64, direccion=0x3C):
        self.oled = None
        try:
            i2c = SoftI2C(sda=Pin(sda), scl=Pin(scl))
            self.oled = ssd1306.SSD1306_I2C(ancho, alto, i2c, direccion)
            self.oled.fill(0)
            self.oled.text("Banco de Pruebas", 0, 0)
            self.oled.text("Esperando datos", 0, 16)
            self.oled.show()
        except Exception as e:
            print("OLED no disponible:", e)

    def mostrar(self, est, duty, rpm, sensor, vib, errores_crc):
        """Refresca la pantalla con una trama de telemetria."""
        if self.oled is None:
            return
        try:
            self.oled.fill(0)
            self.oled.text("Est:" + str(est), 0, 0)
            self.oled.text("PWM:" + str(duty) + "%", 0, 9)
            self.oled.text("RPM:" + str(rpm), 0, 18)
            self.oled.text("Sen:" + str(sensor)[:12], 0, 27)
            self.oled.text("Vib:" + "%.2f" % vib, 0, 36)
            self.oled.text(self.alerta(est, sensor), 0, 45)
            self.oled.text("CRC err:" + str(errores_crc), 0, 54)
            self.oled.show()
        except Exception as e:
            print("error OLED:", e)

    @staticmethod
    def alerta(est, sensor):
        """Linea de alerta (maximo 16 caracteres)."""
        if est == "EMERGENCIA":
            return "!! EMERGENCIA !!"
        if est == "FALLA":
            return "!! FALLA !!"
        if sensor != "MPU6050":
            return "SENSOR NO LEE"
        return "OK"
