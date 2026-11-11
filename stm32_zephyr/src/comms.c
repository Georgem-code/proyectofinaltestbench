#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "comms.h"
#include "fsm.h"

#define RX_STACK     640
#define RX_PRIO      6
#define RX_COLA_PROF 8

K_THREAD_STACK_DEFINE(rx_stack, RX_STACK);
static struct k_thread rx_hilo;

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(usart2));

/* Cola de bytes crudos entre la ISR y el hilo de comandos */
K_MSGQ_DEFINE(cola_rx, sizeof(uint8_t), RX_COLA_PROF, 1);

/* ISR del UART: cada byte recibido pasa a la cola. */
static void rx_isr(const struct device *dev, void *user_data)
{
	uint8_t c;

	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uart_fifo_read(dev, &c, 1);
		k_msgq_put(&cola_rx, &c, K_NO_WAIT);
	}
}

/* Hilo de comandos: cola de bytes -> eventos de la FSM. */
static void rx_fn(void *a, void *b, void *c)
{
	uint8_t cmd;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_msgq_get(&cola_rx, &cmd, K_FOREVER);

		switch (cmd) {
		case 'G':
			fsm_post(FSM_EVT_GO);
			break;
		case 'E':
			fsm_post(FSM_EVT_EMERG);
			break;
		case 'R':
			fsm_post(FSM_EVT_RESET);
			break;
		default:
			break;   /* ruido de linea: se ignora */
		}
	}
}

int comms_init(void)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	int err = uart_irq_callback_user_data_set(uart, rx_isr, NULL);

	if (err != 0) {
		return err;
	}

	uart_irq_rx_enable(uart);

	k_thread_create(&rx_hilo, rx_stack, RX_STACK, rx_fn, NULL, NULL, NULL,
			RX_PRIO, 0, K_NO_WAIT);
	return 0;
}
