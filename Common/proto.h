#ifndef __PROTO_H
#define __PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** USART1 consumer: factory ring buffer + CLI, vs binary ESP8266 link (TySerialFrame layer hooks in later). */
typedef enum
{
    PROTO_UART_MODE_FACTORY = 0,
    PROTO_UART_MODE_ESP8266 = 1,
} proto_uart_mode_t;

/** One byte from USART1 while in PROTO_UART_MODE_ESP8266; runs from UART ISR — keep minimal. */
typedef void (*proto_esp_rx_byte_fn)(uint8_t byte, void *user);

void proto_init(UART_HandleTypeDef *huart);
void proto_on_rx_from_isr(uint8_t byte);

UART_HandleTypeDef *proto_uart1_handle(void);

void proto_uart_set_mode(proto_uart_mode_t mode);
proto_uart_mode_t proto_uart_get_mode(void);

void proto_esp_register_rx_byte_handler(proto_esp_rx_byte_fn fn, void *user);

bool proto_uart1_send(const uint8_t *data, uint16_t len);
bool proto_uart1_send_timeout(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

void proto_cmd_tx(int argc, const char *argv[]);
void proto_cmd_rx(int argc, const char *argv[]);
void proto_cmd_clr(int argc, const char *argv[]);
void proto_cmd_xfer(int argc, const char *argv[]);

void proto_enter(void);
void proto_exit(void);
uint8_t proto_is_active(void);
void proto_process_line(const char *line);
void proto_poll_monitor(void);

void proto_app_dispatch_from_event_loop(void);

#ifdef __cplusplus
}
#endif

#endif
