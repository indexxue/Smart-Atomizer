#ifndef FLEXIBLE_BUTTON_H
#define FLEXIBLE_BUTTON_H

#include <stdint.h>
#include <stdbool.h>

#define BUTTON_USING_FLEXIBLE

#ifndef FLEX_BTN_SCAN_FREQ_HZ
#define FLEX_BTN_SCAN_FREQ_HZ 50
#endif

#define FLEX_MS_TO_SCAN_CNT(ms) (ms / (1000 / FLEX_BTN_SCAN_FREQ_HZ))

#define MAX_MULTIPLE_CLICKS_INTERVAL (FLEX_MS_TO_SCAN_CNT(300))

typedef void (*flex_button_response_callback)(void *);

typedef enum
{
    FLEX_BTN_PRESS_DOWN = 0,
    FLEX_BTN_PRESS_CLICK,
    FLEX_BTN_PRESS_DOUBLE_CLICK,
    FLEX_BTN_PRESS_REPEAT_CLICK,
    FLEX_BTN_PRESS_SHORT_START,
    FLEX_BTN_PRESS_SHORT_UP,
    FLEX_BTN_PRESS_LONG_START,
    FLEX_BTN_PRESS_LONG_UP,
    FLEX_BTN_PRESS_LONG_HOLD,
    FLEX_BTN_PRESS_LONG_HOLD_UP,
    FLEX_BTN_PRESS_MAX,
    FLEX_BTN_PRESS_NONE,
} flex_button_event_t;

typedef struct flex_button
{
    struct flex_button *next;

    uint8_t (*usr_button_read)(void *);
    flex_button_response_callback cb;

    uint16_t scan_cnt;
    uint16_t click_cnt;
    uint16_t max_multiple_clicks_interval;

    uint16_t debounce_tick;
    uint16_t short_press_start_tick;
    uint16_t long_press_start_tick;
    uint16_t long_hold_start_tick;

    uint8_t id;
    uint8_t pressed_logic_level : 1;
    uint8_t event : 4;
    uint8_t status : 3;
    uint8_t power_on_press;
    void *user_data;
} flex_button_t;

#ifdef __cplusplus
extern "C" {
#endif

int32_t flex_button_register(flex_button_t *button);
flex_button_event_t flex_button_event_read(flex_button_t *button);
uint8_t flex_button_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* FLEXIBLE_BUTTON_H */
