#ifndef INDICADORES_H
#define INDICADORES_H

/* LED de estado de la placa y bocina de alarma.
 * El hilo observa el estado de la FSM y marca el ritmo que toca,
 * sin intervenir en la logica de control. */

/* Configura el LED y crea el hilo. 0 = ok. */
int indicadores_init(void);

#endif /* INDICADORES_H */
