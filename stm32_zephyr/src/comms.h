#ifndef COMMS_H
#define COMMS_H

/*
 * Recepcion de comandos del ESP32 por USART2 (por interrupcion).
 *
 * La ISR del UART solo mete los bytes en una cola (k_msgq); el hilo
 * de comandos los traduce a eventos de la FSM. Ni la ISR ni el main
 * bloquean nunca.
 *
 *   'G' -> arrancar ensayo       'E' -> parada de emergencia
 *   'R' -> reiniciar a IDLE
 */

/* Habilita la IRQ de RX y crea el hilo de comandos. 0 = ok. */
int comms_init(void);

#endif /* COMMS_H */
