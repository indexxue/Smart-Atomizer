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

#define TY_CHG_HUMID          0x01u
#define TY_CHG_LED            0x02u

#define TY_FLAG_OVERHEAT      0x01u

#define TY_ACK_ERR_NONE           0u
#define TY_ACK_ERR_BAD_REQ_ID     1u
#define TY_ACK_ERR_BAD_HUMID      2u
#define TY_ACK_ERR_BAD_LED        3u
#define TY_ACK_ERR_BAD_MASK       4u

/** Placeholder RH until a humidity sensor is wired (see ty_link_set_measured_humidity_pct). */
#define TY_LINK_HUMIDITY_PLACEHOLDER_PCT  70u

typedef struct
{
    uint8_t req_id;
    uint8_t change_mask;
    uint8_t humidifier_level;
    uint8_t led_strip_mode;
} ty_link_set_request_t;

typedef struct
{
    uint8_t status;
    uint8_t change_mask;
    uint8_t humidifier_level;
    uint8_t led_strip_mode;
} ty_link_set_ack_t;

/**
 * Optional hook when a valid 0x21 is received (mask bits already validated).
 * Apply only fields set in @p in->change_mask; fill @p out with current levels.
 * Return true to use @p out as-is; false for built-in shadow-only apply.
 */
typedef bool (*ty_link_apply_settings_fn)(const ty_link_set_request_t *in, ty_link_set_ack_t *out, void *user);

bool ty_link_init(void);
void ty_link_task(void *argument);
/** Drain 0x21 set-request queue and send 0x22; call from the main event thread after event_schedule(). */
void ty_link_service_main_from_event_loop(void);

void ty_link_register_apply(ty_link_apply_settings_fn fn, void *user);

/** Reported RH in 0x20 byte0 (0–100); reserved for future sensor, default placeholder 70%. */
void ty_link_set_measured_humidity_pct(uint8_t pct);

/** Mirror local humidifier gear / LED mode / status_flags into 0x20 (0–3, flags § TY_FLAG_*). */
void ty_link_set_humidifier_level(uint8_t level);
void ty_link_set_led_strip_mode(uint8_t mode);
void ty_link_set_status_flags(uint8_t flags);

bool ty_link_send_raw_frame(uint8_t cmd, const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* TY_LINK_H */
