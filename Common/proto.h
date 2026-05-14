#ifndef __PROTO_H
#define __PROTO_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void proto_init(UART_HandleTypeDef *huart);
void proto_on_rx_from_isr(uint8_t byte);

void proto_cmd_tx(int argc, const char *argv[]);
void proto_cmd_rx(int argc, const char *argv[]);
void proto_cmd_clr(int argc, const char *argv[]);
void proto_cmd_xfer(int argc, const char *argv[]);

void proto_enter(void);
void proto_exit(void);
uint8_t proto_is_active(void);
void proto_process_line(const char *line);
void proto_poll_monitor(void);

#ifdef __cplusplus
}
#endif

#endif
