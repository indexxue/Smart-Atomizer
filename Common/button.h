#ifndef __BUTTON_H
#define __BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    BTN_STATUS_UP = 0,
    BTN_STATUS_DOWN,
} btn_status_e;

typedef enum
{
    BTN_ID_WAKE = 0,
    BTN_ID_NAV_UP,
    BTN_ID_NAV_DOWN,
    BTN_ID_NAV_LEFT,
    BTN_ID_NAV_RIGHT,
    BTN_ID_MAX_NUMBER,
} btn_id_e;

typedef enum
{
    BTN_EVENT_NONE = 0,
    BTN_EVENT_PRESS_DOWN,
    BTN_EVENT_PRESS_UP,
    BTN_EVENT_SINGLE_CLICK,
    BTN_EVENT_DOUBLE_CLICK,
    BTN_EVENT_REPEAT_CLICK,
    BTN_EVENT_LONG_PRESS,
    BTN_EVENT_LONG_HOLD,
    BTN_EVENT_LONG_HOLD_UP,
} btn_event_e;

typedef enum
{
    BTN_PERMISSION_NONE = 0,
    BTN_PERMISSION_RESET = (1 << 0),
    BTN_PERMISSION_PAIR = (1 << 1),
    BTN_PERMISSION_FTM = (1 << 2),
    BTN_PERMISSION_BYPASS = (1 << 3),
    BTN_PERMISSION_UNPAIR = (1 << 4),
    BTN_PERMISSION_ALARM = (1 << 5),
    BTN_PERMISSION_UP = (1 << 6),
    BTN_PERMISSION_DOWN = (1 << 7),
    BTN_PERMISSION_LEFT = (1 << 8),
    BTN_PERMISSION_RIGHT = (1 << 9),
    BTN_PERMISSION_CONFIRM = (1 << 10),
    BTN_PERMISSION_BACK = (1 << 11),
} btn_permission_e;

typedef void (*btn_notify_t)(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);

void button_init(btn_notify_t notify);
void button_deinit(void);
void button_schedule(void);
void button_last_event_get(btn_id_e *id, btn_event_e *event);
void button_last_event_clear(void);

const char *button_id_to_str(btn_id_e id);
const char *button_event_to_str(btn_event_e event);

void button_log_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);

uint8_t button_get_level(btn_id_e id);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_H */
