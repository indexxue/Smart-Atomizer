/**
 * @file    led_scene.h
 * @brief   LED scene driver for STM32F103RCT6
 */

#ifndef __LED_SCENE_H
#define __LED_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define LED_SCENE_ACTION_NUM    3
#define LED_SCENE_MSEC           1
#define CYCLE_ALWAYS             0xFFFF
#define LED_SCENE_LED_NUM        2

typedef enum
{
    LED_SCENE_LED_0 = 0,
    LED_SCENE_LED_1,
    LED_SCENE_LED_MAX_NUM,
} led_scene_led_e;

typedef enum
{
    LED_SCENE_PRIO_FACTORY = 0,
    LED_SCENE_PRIO_PAIR,
    LED_SCENE_PRIO_NORMAL,
    LED_SCENE_PRIO_LOW,
    LED_SCENE_PRIO_MAX_NUM,
} led_scene_prio_e;

typedef enum
{
    LED_SCENE_ID_BOOTUP = 0,
    LED_SCENE_ID_PAIRING,
    LED_SCENE_ID_TRIGGER,
    LED_SCENE_ID_ERROR,
    LED_SCENE_ID_SUCCESS,
    LED_SCENE_ID_MAX_NUM,
} led_scene_id_e;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_value_t;

typedef enum
{
    ACTION_ONOFF = 0,
    ACTION_FADE,
} led_action_type_e;

typedef struct
{
    uint8_t cycle;
    led_action_type_e type;
    union
    {
        struct
        {
            led_rgb_value_t value;
            uint32_t lifetime;
        } onoff;
        struct
        {
            led_rgb_value_t start_value;
            led_rgb_value_t end_value;
            uint8_t step;
            uint32_t interval;
        } fade;
    } sub;
} led_scene_action_t;

typedef struct
{
    uint16_t cycle;
    uint8_t num;
    led_scene_action_t action[LED_SCENE_ACTION_NUM];
} led_scene_t;

typedef struct
{
    led_scene_prio_e prio;
    const led_scene_t *scene;
} led_scene_tab_t;

void led_scene_init(void);
void led_scene_update(void);
void led_scene_run(led_scene_id_e id);
void led_scene_cancel(led_scene_id_e id);
void led_scene_led_direct_set(led_scene_led_e led, bool on);

#ifdef __cplusplus
}
#endif

#endif /* __LED_SCENE_H */
