#include "button.h"
#include "event.h"
#include "flexible_button.h"
#include "main.h"
#include <string.h>

#ifdef BUTTON_USE_LOG
#include "log.h"
#endif

#define BTN_NAME_WAKE           "Wake"
#define BTN_NAME_NAV_UP         "NavUp"
#define BTN_NAME_NAV_DOWN       "NavDown"
#define BTN_NAME_NAV_LEFT       "NavLeft"
#define BTN_NAME_NAV_RIGHT      "NavRight"

#define BTN_NUM                5

typedef struct
{
    btn_id_e id;
    const char *name;
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
    uint16_t permission;
    flex_button_t flex;
} button_list_t;

typedef struct
{
    uint8_t num;
    button_list_t *list;
    btn_notify_t notify;
} button_item_t;

static button_list_t s_button_list[BTN_NUM];
static button_item_t self = {0};

static btn_id_e last_button_id = BTN_ID_MAX_NUMBER;
static btn_event_e last_button_event = BTN_EVENT_NONE;

static uint8_t button_read_gpio(GPIO_TypeDef *port, uint16_t pin)
{
    return (uint8_t)HAL_GPIO_ReadPin(port, pin);
}

static uint8_t button_flex_read(void *flex)
{
    flex_button_t *btn = (flex_button_t *)flex;
    button_list_t *list = (button_list_t *)btn->user_data;
    return button_read_gpio(list->port, list->pin);
}

static bool button_is_permission(button_list_t *list, btn_permission_e permission)
{
    return (list->permission & (uint16_t)permission) != 0u;
}

static void button_flex_press_long_process(flex_button_t *flex, button_list_t *list)
{
    (void)flex;
    if (!button_is_permission(list, BTN_PERMISSION_RESET))
    {
        return;
    }
}

static void button_flex_event_callback(void *arg)
{
    flex_button_t *flex = (flex_button_t *)arg;
    button_list_t *list = (button_list_t *)flex->user_data;
    flex_button_event_t fevt = flex_button_event_read(flex);
    btn_event_e bevt = BTN_EVENT_NONE;

    switch (fevt)
    {
        case FLEX_BTN_PRESS_DOWN:
            bevt = BTN_EVENT_PRESS_DOWN;
            break;
        case FLEX_BTN_PRESS_CLICK:
            bevt = BTN_EVENT_SINGLE_CLICK;
            break;
        case FLEX_BTN_PRESS_DOUBLE_CLICK:
            bevt = BTN_EVENT_DOUBLE_CLICK;
            break;
        case FLEX_BTN_PRESS_REPEAT_CLICK:
            bevt = BTN_EVENT_REPEAT_CLICK;
            break;
        case FLEX_BTN_PRESS_LONG_START:
            bevt = BTN_EVENT_LONG_PRESS;
            break;
        case FLEX_BTN_PRESS_LONG_HOLD:
            bevt = BTN_EVENT_LONG_HOLD;
            break;
        case FLEX_BTN_PRESS_LONG_HOLD_UP:
            bevt = BTN_EVENT_LONG_HOLD_UP;
            break;
        default:
            bevt = BTN_EVENT_NONE;
            break;
    }

    if (bevt != BTN_EVENT_NONE)
    {
        last_button_id = list->id;
        last_button_event = bevt;
        if (self.notify != NULL)
        {
            self.notify(list->id, list->name, (btn_permission_e)list->permission, bevt);
        }
        event_set(EVT_ID_BUTTON);
        button_flex_press_long_process(flex, list);
    }
}

static void button_flex_init(button_list_t *list)
{
    memset(&list->flex, 0, sizeof(flex_button_t));
    list->flex.usr_button_read = button_flex_read;
    list->flex.cb = button_flex_event_callback;
    list->flex.pressed_logic_level = (list->active_level ? 1u : 0u);
    list->flex.debounce_tick = FLEX_MS_TO_SCAN_CNT(80);
    list->flex.max_multiple_clicks_interval = FLEX_MS_TO_SCAN_CNT(600);
    list->flex.short_press_start_tick = FLEX_MS_TO_SCAN_CNT(2000);
    list->flex.long_press_start_tick = FLEX_MS_TO_SCAN_CNT(10000);
    list->flex.long_hold_start_tick = FLEX_MS_TO_SCAN_CNT(11000);
    list->flex.user_data = list;

    flex_button_register(&list->flex);
}

static void button_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    for (uint8_t i = 0; i < BTN_NUM; i++)
    {
        button_list_t *p = &s_button_list[i];
        gpio.Pin = p->pin;
        HAL_GPIO_Init(p->port, &gpio);
        button_flex_init(p);
    }
}

static void button_config(void)
{
    button_list_t *list = s_button_list;

    /* PA0: only FTM — long-hold-up handled in app to switch firmware slot */
    list[0].id = BTN_ID_WAKE;
    list[0].name = BTN_NAME_WAKE;
    list[0].port = GPIOA;
    list[0].pin = GPIO_PIN_0;
    list[0].active_level = 0;
    list[0].permission = (uint16_t)BTN_PERMISSION_FTM;

    /* User nav: PC5=上, PC4=下, PA7=左+确定, PA6=右+返回 — no FTM */
    list[1].id = BTN_ID_NAV_UP;
    list[1].name = BTN_NAME_NAV_UP;
    list[1].port = GPIOC;
    list[1].pin = GPIO_PIN_4;
    list[1].active_level = 0;
    list[1].permission = (uint16_t)(BTN_PERMISSION_UP | BTN_PERMISSION_RESET | BTN_PERMISSION_PAIR);

    list[2].id = BTN_ID_NAV_DOWN;
    list[2].name = BTN_NAME_NAV_DOWN;
    list[2].port = GPIOC;
    list[2].pin = GPIO_PIN_5;
    list[2].active_level = 0;
    list[2].permission = (uint16_t)(BTN_PERMISSION_DOWN | BTN_PERMISSION_RESET | BTN_PERMISSION_PAIR);

    list[3].id = BTN_ID_NAV_LEFT;
    list[3].name = BTN_NAME_NAV_LEFT;
    list[3].port = GPIOA;
    list[3].pin = GPIO_PIN_7;
    list[3].active_level = 0;
    list[3].permission =
        (uint16_t)(BTN_PERMISSION_LEFT | BTN_PERMISSION_CONFIRM | BTN_PERMISSION_RESET | BTN_PERMISSION_PAIR);

    list[4].id = BTN_ID_NAV_RIGHT;
    list[4].name = BTN_NAME_NAV_RIGHT;
    list[4].port = GPIOA;
    list[4].pin = GPIO_PIN_6;
    list[4].active_level = 0;
    list[4].permission =
        (uint16_t)(BTN_PERMISSION_RIGHT | BTN_PERMISSION_BACK | BTN_PERMISSION_RESET | BTN_PERMISSION_PAIR);

    self.num = BTN_NUM;
    self.list = s_button_list;
}

void button_init(btn_notify_t notify)
{
    memset(&self, 0, sizeof(button_item_t));
    button_config();
    self.notify = notify;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    button_gpio_init();

#ifdef BUTTON_USE_LOG
    LOG_INFO("Button driver initialized, %d buttons", self.num);
#endif
}

void button_schedule(void)
{
    if (self.list == NULL || self.num == 0)
    {
        return;
    }
    flex_button_scan();
}

void button_deinit(void)
{
    self.list = NULL;
    self.num = 0;
    memset(&self, 0, sizeof(button_item_t));
#ifdef BUTTON_USE_LOG
    LOG_INFO("Button driver deinitialized");
#endif
}

void button_last_event_get(btn_id_e *id, btn_event_e *event)
{
    if (id != NULL)
    {
        *id = last_button_id;
    }
    if (event != NULL)
    {
        *event = last_button_event;
    }
}

void button_last_event_clear(void)
{
    last_button_id = BTN_ID_MAX_NUMBER;
    last_button_event = BTN_EVENT_NONE;
}

const char *button_id_to_str(btn_id_e id)
{
    switch (id)
    {
        case BTN_ID_WAKE:
            return "WAKE";
        case BTN_ID_NAV_UP:
            return "NAV_UP";
        case BTN_ID_NAV_DOWN:
            return "NAV_DOWN";
        case BTN_ID_NAV_LEFT:
            return "NAV_LEFT";
        case BTN_ID_NAV_RIGHT:
            return "NAV_RIGHT";
        default:
            return "UNKNOWN";
    }
}

const char *button_event_to_str(btn_event_e event)
{
    switch (event)
    {
        case BTN_EVENT_NONE:
            return "NONE";
        case BTN_EVENT_PRESS_DOWN:
            return "PRESS_DOWN";
        case BTN_EVENT_PRESS_UP:
            return "PRESS_UP";
        case BTN_EVENT_SINGLE_CLICK:
            return "SINGLE_CLICK";
        case BTN_EVENT_DOUBLE_CLICK:
            return "DOUBLE_CLICK";
        case BTN_EVENT_REPEAT_CLICK:
            return "REPEAT_CLICK";
        case BTN_EVENT_LONG_PRESS:
            return "LONG_PRESS";
        case BTN_EVENT_LONG_HOLD:
            return "LONG_HOLD";
        case BTN_EVENT_LONG_HOLD_UP:
            return "LONG_HOLD_UP";
        default:
            return "UNKNOWN";
    }
}

void button_log_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
    (void)permission;

    if (event == BTN_EVENT_SINGLE_CLICK ||
        event == BTN_EVENT_DOUBLE_CLICK ||
        event == BTN_EVENT_REPEAT_CLICK ||
        event == BTN_EVENT_LONG_PRESS ||
        event == BTN_EVENT_LONG_HOLD ||
        event == BTN_EVENT_LONG_HOLD_UP)
    {
#ifdef BUTTON_USE_LOG
        LOG_INFO("Button: id=%d(%s), name=%s, event=%d(%s)",
                 (int)id,
                 button_id_to_str(id),
                 (name != NULL) ? name : "NULL",
                 (int)event,
                 button_event_to_str(event));
#endif
    }
}

uint8_t button_get_level(btn_id_e id)
{
    if (id >= BTN_ID_MAX_NUMBER || self.list == NULL)
    {
        return 0xFF;
    }

    for (uint8_t i = 0; i < self.num; i++)
    {
        button_list_t *p = &self.list[i];
        if (p->id == id)
        {
            return (uint8_t)HAL_GPIO_ReadPin(p->port, p->pin);
        }
    }

    return 0xFF;
}
