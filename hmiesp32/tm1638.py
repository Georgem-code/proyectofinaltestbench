"""Driver bit-bang del TM1638: 8 digitos, 8 LEDs y 8 teclas.

 Conexion de 3 pines: STB (strobe), CLK y DIO.
"""

from machine import Pin
import time


class TM1638:
    """Display + teclado TM1638."""

    FONT = {
        '0': 0x3F, '1': 0x06, '2': 0x5B, '3': 0x4F, '4': 0x66,
        '5': 0x6D, '6': 0x7D, '7': 0x07, '8': 0x7F, '9': 0x6F,
        ' ': 0x00, '-': 0x40,
    }

    def __init__(self, stb, clk, dio):
        self.stb = Pin(stb, Pin.OUT, value=1)
        self.clk = Pin(clk, Pin.OUT, value=1)
        self.dio = Pin(dio, Pin.OUT, value=1)
        self.buf = bytearray(16)   # pares: digito, led
        self.flush()

    def _byte(self, b):
        """Envia un byte, LSB primero."""
        for i in range(8):
            self.clk.value(0)
            self.dio.value((b >> i) & 1)
            self.clk.value(1)

    def _cmd(self, c):
        """Envia un comando suelto."""
        self.stb.value(0)
        self._byte(c)
        self.stb.value(1)

    def flush(self):
        """Vuelca el buffer al display y enciende el brillo."""
        self._cmd(0x40)              # modo escritura de datos
        self.stb.value(0)
        self._byte(0xC0)             # direccion inicial 0
        for b in self.buf:
            self._byte(b)
        self.stb.value(1)
        self._cmd(0x88 | 7)           # encendido, brillo maximo

    def teclas(self):
        """Lee las 8 teclas: bit0 = S1, bit1 = S2, ..."""
        self.stb.value(0)
        self._byte(0x42)
        self.dio.init(Pin.IN, Pin.PULL_UP)
        time.sleep_us(5)
        k = 0
        for i in range(4):
            b = 0
            for j in range(8):
                self.clk.value(0)
                time.sleep_us(2)
                b |= self.dio.value() << j
                self.clk.value(1)
                time.sleep_us(2)
            k |= b << i
        self.dio.init(Pin.OUT)
        self.dio.value(1)
        self.stb.value(1)
        return k

    def celdas(self, s):
        """Convierte un texto en codigos de 7 segmentos.

        El punto decimal se fusiona con el digito anterior.
        """
        res = []
        for c in s:
            if c == '.' and res:
                res[-1] |= 0x80
            else:
                res.append(self.FONT.get(c, 0))
        return res

    def mostrar(self, izq, der):
        """Escribe 4 digitos a la izquierda y 4 a la derecha."""
        a = self.celdas(izq)[-4:]
        b = self.celdas(der)[-4:]
        a = [0] * (4 - len(a)) + a
        b = [0] * (4 - len(b)) + b
        for i, celda in enumerate(a + b):
            self.buf[2 * i] = celda

    def leds(self, indice):
        """Enciende un unico LED (indice 0-7); apaga el resto."""
        for i in range(8):
            self.buf[2 * i + 1] = 1 if i == indice else 0
