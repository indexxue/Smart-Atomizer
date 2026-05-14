/**
 * @file ty_link.h
 * @brief ESP8266 TySerialFrame link: 0x20 / 0x21 / 0x22 (see doc/esp8266_stm32_link_proto.md).
 *
 * Call ty_link_init() once before osThreadNew(ty_link_task, ...). Returns false if RTOS objects fail.
 * If both ty_link and serial_cmd_init() are used, call serial_cmd_init first, then ty_link_init.
 */

#ifndef TY_LINK_H
#define TY_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TY_CMD_SENSOR_REPORT  0x20u
#define TY_CMD_SET_REQUEST    0x21u
#define TY_CMD_SET_ACK        0x22u

#define TY_ACK_ERR_NONE        0u
#define TY_ACK_ERR_BAD_REQ_ID  1u
#define TY_ACK_ERR_BAD_PARAM   2u

typedef struct
{
    uint8_t req_id;
    uint8_t target_humidity_pct;
    uint8_t humidifier_on;
    uint8_t gear;
} ty_link_set_request_t;

typedef struct
{
    uint8_t status;
    uint8_t target_humidity_pct;
    uint8_t humidifier_on;
    uint8_t gear;
} ty_link_set_ack_t;

/**
 * Optional hook when a valid 0x21 is received.
 * Fill @p out for 0x22 payload bytes 1..4; return true to use @p out as-is.
 * Return false to use built-in default (shadow RAM only).
 */
typedef bool (*ty_link_apply_settings_fn)(const ty_link_set_request_t *in, ty_link_set_ack_t *out, void *user);

bool ty_link_init(void);
void ty_link_task(void *argument);
/** Drain 0x21 set-request queue and send 0x22; call from the main event thread after event_schedule(). */
void ty_link_service_main_from_event_loop(void);

void ty_link_register_apply(ty_link_apply_settings_fn fn, void *user);

/** Reported RH in 0x20 byte0; call from sensor task when SHT20 etc. is available (0–100). */
void ty_link_set_measured_humidity_pct(uint8_t pct);

bool ty_link_send_raw_frame(uint8_t cmd, const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* TY_LINK_H */
