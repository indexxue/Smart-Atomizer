/**
 * @file    led_scene.c
 * @brief   LED scene driver implementation for STM32F103RCT6
 */

#include "led_scene.h"
#include "main.h"
#include <string.h>

#ifdef LED_SCENE_USE_LOG
#include "log.h"
#endif

#define LED_SCENE_TICK_MS    50

#define LED_R_PORT           GPIOB
#define LED_R_PIN            GPIO_PIN_8
#define LED_B_PORT           GPIOB
#define LED_B_PIN            GPIO_PIN_9

typedef enum
{
    LED_IDX_R = 0,
    LED_IDX_B,
    LED_IDX_MAX
} led_idx_e;

typedef struct
{
    led_scene_id_e id;
    bool running;
    uint8_t current_cycle;
    uint8_t current_action;
    uint8_t action_cycle;
    uint32_t action_time;
    led_rgb_value_t current_rgb;
} led_scene_state_t;

typedef struct
{
    led_scene_state_t states[LED_SCENE_ID_MAX_NUM];
    led_scene_id_e active_scene;
    bool initialized;
} led_scene_self_t;

static led_scene_self_t self;

static const led_scene_t led_scene_bootup =
{
    .cycle = 1,
    .num = 1,
    .action[0] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 2000 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_pairing =
{
    .cycle = 30,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 1000 * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 1000 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_trigger =
{
    .cycle = 10,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_error =
{
    .cycle = 10,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100 * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_success =
{
    .cycle = 20,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_tab_t scene_table[LED_SCENE_ID_MAX_NUM] =
{
    [LED_SCENE_ID_BOOTUP]  = {.prio = LED_SCENE_PRIO_LOW,       .scene = &led_scene_bootup,},
    [LED_SCENE_ID_PAIRING] = {.prio = LED_SCENE_PRIO_PAIR,      .scene = &led_scene_pairing,},
    [LED_SCENE_ID_TRIGGER] = {.prio = LED_SCENE_PRIO_NORMAL,    .scene = &led_scene_trigger,},
    [LED_SCENE_ID_ERROR]   = {.prio = LED_SCENE_PRIO_NORMAL,    .scene = &led_scene_error,},
    [LED_SCENE_ID_SUCCESS] = {.prio = LED_SCENE_PRIO_NORMAL,    .scene = &led_scene_success,},
};

static void led_drv_onoff(led_idx_e idx, bool onoff)
{
    if (idx >= LED_IDX_MAX)
    {
        return;
    }

    if (idx == LED_IDX_R)
    {
        HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, onoff ? GPIO_PIN_RESET : GPIO_PIN_SET);
        return;
    }

    if (idx == LED_IDX_B)
    {
        HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, onoff ? GPIO_PIN_RESET : GPIO_PIN_SET);
        return;
    }
}

static void led_scene_output(const led_rgb_value_t *rgb)
{
    if (rgb == NULL)
    {
        led_drv_onoff(LED_IDX_R, false);
        led_drv_onoff(LED_IDX_B, false);
        return;
    }

    led_drv_onoff(LED_IDX_R, rgb->r ? true : false);
    led_drv_onoff(LED_IDX_B, rgb->b ? true : false);
}

static led_scene_id_e led_scene_find_highest_priority(void)
{
    led_scene_id_e highest_id = LED_SCENE_ID_MAX_NUM;
    led_scene_prio_e highest_prio = LED_SCENE_PRIO_MAX_NUM;

    for (uint8_t i = 0; i < LED_SCENE_ID_MAX_NUM; i++)
    {
        if (self.states[i].running)
        {
            led_scene_prio_e prio = scene_table[i].prio;
            if (prio < highest_prio)
            {
                highest_prio = prio;
                highest_id = (led_scene_id_e)i;
            }
        }
    }

    return highest_id;
}

void led_scene_update(void)
{
    if (!self.initialized)
    {
        return;
    }

    if (self.active_scene >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];
    const led_scene_t *scene = scene_table[self.active_scene].scene;

    if (!state->running || scene == NULL)
    {
        return;
    }

    state->action_time += LED_SCENE_TICK_MS;

    const led_scene_action_t *action = &scene->action[state->current_action];
    bool action_complete = false;

    if (action->type == ACTION_ONOFF)
    {
        if (state->action_time >= action->sub.onoff.lifetime)
        {
            action_complete = true;
        }
        else
        {
            led_scene_output(&action->sub.onoff.value);
        }
    }
    else if (action->type == ACTION_FADE)
    {
        if (state->action_time >= action->sub.fade.interval)
        {
            bool fade_complete = false;

            if (action->sub.fade.start_value.r < action->sub.fade.end_value.r)
            {
                if (state->current_rgb.r + action->sub.fade.step >= action->sub.fade.end_value.r)
                {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.r += action->sub.fade.step;
                }
            }
            else if (action->sub.fade.start_value.r > action->sub.fade.end_value.r)
            {
                if (state->current_rgb.r <= action->sub.fade.step ||
                    state->current_rgb.r - action->sub.fade.step <= action->sub.fade.end_value.r)
                {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.r -= action->sub.fade.step;
                }
            }

            if (action->sub.fade.start_value.b < action->sub.fade.end_value.b)
            {
                if (state->current_rgb.b + action->sub.fade.step >= action->sub.fade.end_value.b)
                {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.b += action->sub.fade.step;
                }
            }
            else if (action->sub.fade.start_value.b > action->sub.fade.end_value.b)
            {
                if (state->current_rgb.b <= action->sub.fade.step ||
                    state->current_rgb.b - action->sub.fade.step <= action->sub.fade.end_value.b)
                {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.b -= action->sub.fade.step;
                }
            }

            led_scene_output(&state->current_rgb);
            state->action_time = 0;

            if (fade_complete)
            {
                action_complete = true;
            }
        }
    }

    if (action_complete)
    {
        state->action_cycle++;
        if (state->action_cycle >= action->cycle)
        {
            state->action_cycle = 0;
            state->current_action++;
            if (state->current_action >= scene->num)
            {
                state->current_action = 0;
                state->current_cycle++;
                if (scene->cycle != CYCLE_ALWAYS && state->current_cycle >= scene->cycle)
                {
                    state->running = false;
                    led_scene_output(NULL);
                    self.active_scene = led_scene_find_highest_priority();
                    if (self.active_scene >= LED_SCENE_ID_MAX_NUM)
                    {
                        return;
                    }
                    state = &self.states[self.active_scene];
                    scene = scene_table[self.active_scene].scene;
                }
            }
        }
        state->action_time = 0;
        if (action->type == ACTION_FADE)
        {
            state->current_rgb = action->sub.fade.start_value;
        }
    }
}

void led_scene_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    memset(&self, 0, sizeof(led_scene_self_t));
    self.active_scene = LED_SCENE_ID_MAX_NUM;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LED_R_PIN;
    HAL_GPIO_Init(LED_R_PORT, &gpio);
    HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, GPIO_PIN_SET);

    gpio.Pin = LED_B_PIN;
    HAL_GPIO_Init(LED_B_PORT, &gpio);
    HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, GPIO_PIN_SET);

    self.initialized = true;

#ifdef LED_SCENE_USE_LOG
    LOG_INFO("LED scene driver initialized (LED_R: PB8, LED_B: PB9)");
#endif
}

void led_scene_led_direct_set(led_scene_led_e led, bool on)
{
    if (!self.initialized)
    {
        return;
    }
    if (led == LED_SCENE_LED_0)
    {
        led_drv_onoff(LED_IDX_R, on);
    }
    else if (led == LED_SCENE_LED_1)
    {
        led_drv_onoff(LED_IDX_B, on);
    }
}

void led_scene_run(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    if (!self.initialized)
    {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    const led_scene_t *scene = scene_table[id].scene;

    if (scene == NULL)
    {
        return;
    }

    if (state->running)
    {
        return;
    }

    state->running = true;
    state->current_cycle = 0;
    state->current_action = 0;
    state->action_cycle = 0;
    state->action_time = 0;
    state->current_rgb.r = 0;
    state->current_rgb.g = 0;
    state->current_rgb.b = 0;

    led_scene_id_e new_scene = led_scene_find_highest_priority();
    if (new_scene != self.active_scene)
    {
        if (self.active_scene < LED_SCENE_ID_MAX_NUM)
        {
            self.states[self.active_scene].running = false;
        }
        self.active_scene = new_scene;
    }

    if (self.active_scene < LED_SCENE_ID_MAX_NUM)
    {
        led_scene_state_t *active_state = &self.states[self.active_scene];
        active_state->current_cycle = 0;
        active_state->current_action = 0;
        active_state->action_cycle = 0;
        active_state->action_time = 0;
        active_state->current_rgb.r = 0;
        active_state->current_rgb.g = 0;
        active_state->current_rgb.b = 0;
        const led_scene_action_t *action = &scene_table[self.active_scene].scene->action[0];
        if (action->type == ACTION_FADE)
        {
            active_state->current_rgb = action->sub.fade.start_value;
        }
    }
}

void led_scene_cancel(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    if (!self.initialized)
    {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    state->running = false;

    if (self.active_scene == id)
    {
        led_scene_output(NULL);
        led_scene_id_e new_scene = led_scene_find_highest_priority();
        if (new_scene < LED_SCENE_ID_MAX_NUM)
        {
            self.active_scene = new_scene;
            led_scene_state_t *active_state = &self.states[self.active_scene];
            active_state->current_cycle = 0;
            active_state->current_action = 0;
            active_state->action_cycle = 0;
            active_state->action_time = 0;
            active_state->current_rgb.r = 0;
            active_state->current_rgb.g = 0;
            active_state->current_rgb.b = 0;
            const led_scene_action_t *action = &scene_table[self.active_scene].scene->action[0];
            if (action->type == ACTION_FADE)
            {
                active_state->current_rgb = action->sub.fade.start_value;
            }
        }
        else
        {
            self.active_scene = LED_SCENE_ID_MAX_NUM;
        }
    }
}
