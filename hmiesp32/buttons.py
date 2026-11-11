"""Teclas del TM1638 traducidas a comandos del banco.

 S1 -> 'G' (arrancar)   S2 -> 'E' (emergencia)   S3 -> 'R' (reiniciar)
"""


class Teclas:
    """Detector de flanco de subida sobre las teclas del TM1638."""

    def __init__(self, tm1638, mapa=None):
        self.tm = tm1638
        self.mapa = mapa or {0x01: "G", 0x02: "E", 0x04: "R"}
        self._previas = 0

    def barrer(self):
        """Devuelve la lista de comandos recien pulsados."""
        actuales = self.tm.teclas() & 0x07
        nuevas = actuales & ~self._previas
        self._previas = actuales
        return [cmd for bit, cmd in self.mapa.items() if nuevas & bit]
