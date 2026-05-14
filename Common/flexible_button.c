#include "flexible_button.h"
#include <stddef.h>

#ifdef BUTTON_USING_FLEXIBLE

#define EVENT_SET_AND_EXEC_CB(btn, evt)   \
    do                                   \
    {                                    \
        (btn)->event = (evt);            \
        if ((btn)->cb)                   \
        {                                \
            (btn)->cb((flex_button_t *)(btn)); \
        }                                \
    } while (0)

#define BTN_IS_PRESSED(i) (g_btn_status_reg & (1U << (i)))

typedef enum
{
    FLEX_BTN_STAGE_DEFAULT = 0,
    FLEX_BTN_STAGE_DOWN = 1,
    FLEX_BTN_STAGE_MULTIPLE_CLICK = 2
} flex_btn_stage_t;

typedef uint32_t btn_type_t;

static flex_button_t *btn_head = NULL;

btn_type_t g_logic_level = (btn_type_t)0;
btn_type_t g_btn_status_reg = (btn_type_t)0;

static uint8_t button_cnt = 0;

int32_t flex_button_register(flex_button_t *button)
{
    flex_button_t *curr = btn_head;

    if (button == NULL || button_cnt > sizeof(btn_type_t) * 8U)
    {
        return -1;
    }

    while (curr != NULL)
    {
        if (curr == button)
        {
            return -1;
        }
        curr = curr->next;
    }

    button->next = btn_head;
    button->status = FLEX_BTN_STAGE_DEFAULT;
    button->event = FLEX_BTN_PRESS_NONE;
    button->scan_cnt = 0;
    button->click_cnt = 0;
    button->power_on_press = 1U;
    if (button->max_multiple_clicks_interval == 0U)
    {
        button->max_multiple_clicks_interval = MAX_MULTIPLE_CLICKS_INTERVAL;
    }
    btn_head = button;

    g_logic_level |= ((btn_type_t)button->pressed_logic_level << button_cnt);
    button_cnt++;

    return (int32_t)button_cnt;
}

static void flex_button_read(void)
{
    uint8_t i;
    flex_button_t *target;
    btn_type_t raw_data = 0;

    for (target = btn_head, i = (uint8_t)(button_cnt - 1U);
         target != NULL && target->usr_button_read != NULL;
         target = target->next, i--)
    {
        raw_data |= ((btn_type_t)(target->usr_button_read)(target) << i);
    }

    g_btn_status_reg = (~raw_data) ^ g_logic_level;
}

static uint8_t flex_button_process(void)
{
    uint8_t i;
    uint8_t active_btn_cnt = 0;
    flex_button_t *target;

    for (target = btn_head, i = (uint8_t)(button_cnt - 1U);
         target != NULL;
         target = target->next, i--)
    {
        if (target->status > FLEX_BTN_STAGE_DEFAULT)
        {
            target->scan_cnt++;
            if (target->scan_cnt >= ((uint16_t)((1U << (sizeof(target->scan_cnt) * 8U)) - 1U)))
            {
                target->scan_cnt = target->long_hold_start_tick;
            }
        }

        switch ((flex_btn_stage_t)target->status)
        {
            case FLEX_BTN_STAGE_DEFAULT:
                if (BTN_IS_PRESSED(i))
                {
                    target->scan_cnt = 0;
                    target->click_cnt = 0;

                    EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_DOWN);

                    target->status = FLEX_BTN_STAGE_DOWN;
                }
                else
                {
                    target->event = FLEX_BTN_PRESS_NONE;
                    target->power_on_press = 0U;
                }
                break;

            case FLEX_BTN_STAGE_DOWN:
                if (BTN_IS_PRESSED(i))
                {
                    if (target->click_cnt > 0U)
                    {
                        if (target->scan_cnt > target->max_multiple_clicks_interval)
                        {
                            flex_button_event_t evt =
                                (target->click_cnt < FLEX_BTN_PRESS_REPEAT_CLICK)
                                    ? (flex_button_event_t)target->click_cnt
                                    : FLEX_BTN_PRESS_REPEAT_CLICK;
                            EVENT_SET_AND_EXEC_CB(target, evt);

                            target->status = FLEX_BTN_STAGE_DOWN;
                            target->scan_cnt = 0;
                            target->click_cnt = 0;
                        }
                    }
                    else if (target->scan_cnt >= target->long_hold_start_tick)
                    {
                        if (target->event != FLEX_BTN_PRESS_LONG_HOLD)
                        {
                            EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_LONG_HOLD);
                        }
                    }
                    else if (target->scan_cnt >= target->long_press_start_tick)
                    {
                        if (target->event != FLEX_BTN_PRESS_LONG_START)
                        {
                            EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_LONG_START);
                        }
                    }
                    else if (target->scan_cnt >= target->short_press_start_tick)
                    {
                        if (target->event != FLEX_BTN_PRESS_SHORT_START)
                        {
                            EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_SHORT_START);
                        }
                    }
                }
                else
                {
                    if (target->scan_cnt >= target->long_hold_start_tick)
                    {
                        EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_LONG_HOLD_UP);
                        target->status = FLEX_BTN_STAGE_DEFAULT;
                    }
                    else if (target->scan_cnt >= target->long_press_start_tick)
                    {
                        EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_LONG_UP);
                        target->status = FLEX_BTN_STAGE_DEFAULT;
                    }
                    else if (target->scan_cnt >= target->short_press_start_tick)
                    {
                        EVENT_SET_AND_EXEC_CB(target, FLEX_BTN_PRESS_SHORT_UP);
                        target->status = FLEX_BTN_STAGE_DEFAULT;
                    }
                    else
                    {
                        target->status = FLEX_BTN_STAGE_MULTIPLE_CLICK;
                        target->click_cnt++;
                    }
                }
                break;

            case FLEX_BTN_STAGE_MULTIPLE_CLICK:
                if (BTN_IS_PRESSED(i))
                {
                    target->status = FLEX_BTN_STAGE_DOWN;
                    target->scan_cnt = 0;
                }
                else
                {
                    if (target->scan_cnt > target->max_multiple_clicks_interval)
                    {
                        flex_button_event_t evt =
                            (target->click_cnt < FLEX_BTN_PRESS_REPEAT_CLICK)
                                ? (flex_button_event_t)target->click_cnt
                                : FLEX_BTN_PRESS_REPEAT_CLICK;
                        EVENT_SET_AND_EXEC_CB(target, evt);

                        target->status = FLEX_BTN_STAGE_DEFAULT;
                    }
                }
                break;

            default:
                break;
        }

        if (target->status > FLEX_BTN_STAGE_DEFAULT)
        {
            active_btn_cnt++;
        }
    }

    return active_btn_cnt;
}

flex_button_event_t flex_button_event_read(flex_button_t *button)
{
    if (button == NULL)
    {
        return FLEX_BTN_PRESS_NONE;
    }
    return (flex_button_event_t)(button->event);
}

uint8_t flex_button_scan(void)
{
    flex_button_read();
    return flex_button_process();
}

#endif
