/**
 * @file    serial_cmd.h
 * @brief   UART command framework with registration and unified reply for production test
 *
 * Usage:
 *   1. serial_cmd_init(huart);
 *   2. serial_cmd_register_defaults();  or register your own commands via serial_cmd_register()
 *   3. When a full line is received from UART, call serial_cmd_process_line(line);
 *
 * Response format: "cmd:value\r\n" for success, "ng\r\n" for error.
 * Input "help" lists all registered commands and their help strings.
 *
 * Intended use: Factory firmware calls serial_cmd_init + register_defaults (see Factory/start.c).
 * Application firmware should not call these so USART1 remains on ty_link (Core/Src/freertos.c).
 */

#ifndef __SERIAL_CMD_H
#define __SERIAL_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

#define SERIAL_CMD_TX_TIMEOUT_MS      100
#define SERIAL_CMD_LINE_MAX           80
#define SERIAL_CMD_NAME_MAX           16
#define SERIAL_CMD_HELP_MAX           32
#define SERIAL_CMD_MAX_COMMANDS       24
#define SERIAL_CMD_MAX_ARGC           8
#define SERIAL_CMD_STATUS_BUF_SIZE    128

typedef void (*serial_cmd_handler_t)(int argc, const char *argv[]);

typedef struct
{
    char name[SERIAL_CMD_NAME_MAX];
    serial_cmd_handler_t handler;
    char help[SERIAL_CMD_HELP_MAX];
} serial_cmd_entry_t;

void serial_cmd_init(UART_HandleTypeDef *huart);

int serial_cmd_register(const char *name, serial_cmd_handler_t handler, const char *help);

void serial_cmd_reply_ok(const char *cmd, const char *value);

void serial_cmd_reply_ng(void);

void serial_cmd_process_line(const char *line);

bool serial_cmd_send(const uint8_t *data, uint16_t len);

bool serial_cmd_send_str(const char *str);

void serial_cmd_register_defaults(void);

void serial_cmd_uart_lock(void);

void serial_cmd_uart_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
