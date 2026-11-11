"""Protocolo del banco: tramas JSON con CRC-16/CCITT.

 La Blue Pill manda por UART lineas terminadas en '\\n':

   {"nodo":"STM32","planta":"BANCO","fsm_state":"IDLE","pwm_duty":0,
    "rpm":0,"i2c_sensor":"MPU6050","val_i2c":0.00,"crc":"1A2B"}

 El CRC cubre el cuerpo de la trama hasta ',"crc":"'.
"""

import json


def crc16(data):
    """CRC-16/CCITT: poly 0x1021, inicio 0xFFFF (igual que la Blue Pill)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class Receptor:
    """Acumula los bytes del UART y extrae las tramas validas."""

    def __init__(self, max_buffer=400):
        self.max_buffer = max_buffer
        self._resto = b""
        self.errores_crc = 0
        self.tramas_malas = 0

    def alimentar(self, datos):
        """Pasa los bytes leidos del UART y devuelve las tramas validas."""
        if datos:
            self._resto += datos
        if len(self._resto) > self.max_buffer:
            self._resto = b""    # trama zombi: se descarta entera

        tramas = []
        while b"\n" in self._resto:
            linea, self._resto = self._resto.split(b"\n", 1)
            if linea.strip():
                trama = self._validar(linea)
                if trama is not None:
                    tramas.append(trama)
        return tramas

    def _validar(self, linea):
        """Chequea CRC y parsea el JSON; None si la trama no vale."""
        if not self._crc_ok(linea):
            self.errores_crc += 1
            print("RX (CRC mal):", linea)
            return None
        try:
            d = json.loads(linea)
            return {
                "estado": d["fsm_state"],
                "duty": d["pwm_duty"],
                "rpm": d["rpm"],
                "sensor": d["i2c_sensor"],
                "vib": d["val_i2c"],
            }
        except (KeyError, ValueError) as e:
            self.tramas_malas += 1
            print("trama mala:", linea, e)
            return None

    def _crc_ok(self, linea):
        marca = b',"crc":"'
        idx = linea.rfind(marca)
        if idx < 0:
            return False
        try:
            recibido = int(linea[idx + 8:idx + 12], 16)
        except ValueError:
            return False
        return crc16(linea[:idx]) == recibido
