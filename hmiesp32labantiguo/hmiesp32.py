# ESP32 - HMI del Banco de Pruebas
# UART2: TX=33, RX=32 | TM1638: STB=25, CLK=26, DIO=27 | OLED: SDA=13, SCL=14
# Botones: S1 = G (arrancar), S2 = E (emergencia), S3 = R (reiniciar)
# La telemetria llega con CRC-16/CCITT (poly 0x1021, inicio 0xFFFF)
from machine import Pin, UART, SoftI2C
import time
import json
import ssd1306


class TM1638:
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
        for i in range(8):
            self.clk.value(0)
            self.dio.value((b >> i) & 1)
            self.clk.value(1)

    def _cmd(self, c):
        self.stb.value(0)
        self._byte(c)
        self.stb.value(1)

    def flush(self):
        self._cmd(0x40)
        self.stb.value(0)
        self._byte(0xC0)
        for b in self.buf:
            self._byte(b)
        self.stb.value(1)
        self._cmd(0x88 | 7)

    def teclas(self):
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
        return k   # bit0=S1, bit1=S2, bit2=S3 ...

    def celdas(self, s):
        res = []
        for c in s:
            if c == '.' and res:
                res[-1] |= 0x80
            else:
                res.append(self.FONT.get(c, 0))
        return res

    def mostrar(self, izq, der):
        a = self.celdas(izq)[-4:]
        b = self.celdas(der)[-4:]
        a = [0] * (4 - len(a)) + a
        b = [0] * (4 - len(b)) + b
        todo = a + b
        for i in range(8):
            self.buf[2 * i] = todo[i]

    def leds(self, indice):
        for i in range(8):
            self.buf[2 * i + 1] = 1 if i == indice else 0


def crc16(data):
    """CRC-16/CCITT: poly 0x1021, inicio 0xFFFF (igual que en la Blue Pill)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


tm = TM1638(25, 26, 27)
uart = UART(2, baudrate=115200, tx=33, rx=32)

errores_crc = 0

# --- OLED (si falla, el resto sigue funcionando) ---
oled = None
try:
    i2c = SoftI2C(sda=Pin(13), scl=Pin(14))
    oled = ssd1306.SSD1306_I2C(128, 64, i2c, 0x3C)
    oled.fill(0)
    oled.text("Banco de Pruebas", 0, 0)
    oled.text("Esperando datos", 0, 16)
    oled.show()
except Exception as e:
    oled = None
    print("OLED no disponible:", e)


def texto_alerta(est, sen):
    """Linea de alerta del OLED (maximo 16 caracteres)."""
    if est == "EMERGENCIA":
        return "!! EMERGENCIA !!"
    if est == "FALLA":
        return "!! FALLA !!"
    if sen != "MPU6050":
        return "SENSOR NO LEE"
    return "OK"


def mostrar_oled(est, duty, rpm, sen, val):
    if oled is None:
        return
    try:
        oled.fill(0)
        oled.text("Est:" + str(est), 0, 0)
        oled.text("PWM:" + str(duty) + "%", 0, 9)
        oled.text("RPM:" + str(rpm), 0, 18)
        oled.text("Sen:" + str(sen)[:12], 0, 27)
        oled.text("Val:" + "%.2f" % val, 0, 36)
        oled.text(texto_alerta(est, sen), 0, 45)
        oled.text("CRC err:" + str(errores_crc), 0, 54)
        oled.show()
    except Exception as e:
        print("error OLED:", e)


ESTADOS = {"IDLE": 0, "ACCEL": 1, "STEADY": 2, "FALLA": 3, "EMERGENCIA": 4}

resto = b""
prev = 0

tm.mostrar("0", "0.00")
tm.leds(0)
tm.flush()


def crc_ok(linea):
    """Revisa el CRC de la trama. Devuelve True si coincide."""
    marca = b',"crc":"'
    idx = linea.rfind(marca)
    if idx < 0:
        return False
    try:
        recibido = int(linea[idx + 8: idx + 12], 16)
    except Exception:
        return False
    return crc16(linea[:idx]) == recibido


def procesar(linea):
    global errores_crc
    linea = linea.strip()

    if not crc_ok(linea):
        errores_crc += 1
        print("RX:", linea)   # DIAGNOSTICO: muestra la trama sin CRC
        return

    try:
        d = json.loads(linea)
        est = d["fsm_state"]
        duty = d["pwm_duty"]
        rpm = d["rpm"]
        sen = d["i2c_sensor"]
        val = d["val_i2c"]
    except Exception as e:
        print("trama mala:", linea, e)
        return
    print(est, duty, rpm, sen, val)
    tm.mostrar(str(rpm), "%.2f" % val)
    tm.leds(ESTADOS.get(est, 0))
    tm.flush()
    mostrar_oled(est, duty, rpm, sen, val)


while True:
    datos = uart.read()
    if datos:
        resto += datos
    if len(resto) > 400:
        resto = b""

    while b"\n" in resto:
        linea, resto = resto.split(b"\n", 1)
        if linea.strip():
            procesar(linea)

    k = tm.teclas() & 0x07
    nuevas = k & (~prev)
    if nuevas & 0x01:
        uart.write(b"G")
        print("S1 -> G")
    if nuevas & 0x02:
        uart.write(b"E")
        print("S2 -> E")
    if nuevas & 0x04:
        uart.write(b"R")
        print("S3 -> R")
    prev = k

    time.sleep_ms(20)
