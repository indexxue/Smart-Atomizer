/**
 * @file    tb6612.h
 * @brief   Generic TB6612FNG dual motor driver (callback-based GPIO/PWM, MCU-agnostic).
 *          Aligned with requirements: init, single/dual motor, brake/coast, standby,
 *          car control, speed ramp, state query, emergency stop, platform abstraction.
 */

#ifndef __TB6612_H
#define __TB6612_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TB6612_PIN_IN1    0
#define TB6612_PIN_IN2    1
#define TB6612_PIN_IN3    2
#define TB6612_PIN_IN4    3
#define TB6612_PIN_STBY   4

#define TB6612_CHANNEL_A  0
#define TB6612_CHANNEL_B  1

#define TB6612_DIR_CW     0
#define TB6612_DIR_CCW    1

#define TB6612_SPEED_MAX  1000

typedef void (*tb6612_gpio_set_t)(uint8_t pin_id, uint8_t level);
typedef void (*tb6612_pwm_set_t)(uint8_t channel, uint16_t duty_permille);
typedef uint32_t (*tb6612_get_tick_ms_t)(void);

typedef enum
{
    TB6612_OK = 0,
    TB6612_ERROR_NOT_INIT,
    TB6612_ERROR_PARAM,
} tb6612_status_t;

typedef struct
{
    tb6612_gpio_set_t   gpio_set;
    tb6612_pwm_set_t    pwm_set;
    tb6612_get_tick_ms_t get_tick_ms;
    uint8_t             channel_left;
    uint8_t             channel_right;
    uint8_t             left_forward_dir;
    uint8_t             right_forward_dir;
} tb6612_config_t;

typedef struct
{
    tb6612_gpio_set_t    gpio_set;
    tb6612_pwm_set_t     pwm_set;
    tb6612_get_tick_ms_t get_tick_ms;
    uint8_t              channel_left;
    uint8_t              channel_right;
    uint8_t              left_forward_dir;
    uint8_t              right_forward_dir;
    bool                 initialized;
    uint16_t             current_speed[2];
    uint8_t              current_direction[2];
    uint8_t              ramp_active[2];
    uint32_t             ramp_start_ms[2];
    uint32_t             ramp_duration_ms[2];
    uint16_t             ramp_start_speed[2];
    uint16_t             ramp_target_speed[2];
    uint8_t              ramp_target_direction[2];
} tb6612_t;

tb6612_status_t tb6612_register(tb6612_t *dev, const tb6612_config_t *cfg);

tb6612_status_t tb6612_run(tb6612_t *dev, uint8_t channel, uint8_t direction, uint16_t speed_permille);
tb6612_status_t tb6612_run_both(tb6612_t *dev,
    uint8_t dir_a, uint16_t speed_a, uint8_t dir_b, uint16_t speed_b);
tb6612_status_t tb6612_run_ramp(tb6612_t *dev, uint8_t channel, uint8_t direction,
    uint16_t target_permille, uint32_t duration_ms);
void tb6612_poll(tb6612_t *dev);

tb6612_status_t tb6612_brake(tb6612_t *dev, uint8_t channel);
tb6612_status_t tb6612_coast(tb6612_t *dev, uint8_t channel);
tb6612_status_t tb6612_standby(tb6612_t *dev, bool enable);
tb6612_status_t tb6612_emergency_stop(tb6612_t *dev);

tb6612_status_t tb6612_car_forward(tb6612_t *dev, uint16_t speed_permille);
tb6612_status_t tb6612_car_backward(tb6612_t *dev, uint16_t speed_permille);
tb6612_status_t tb6612_car_turn_left(tb6612_t *dev, uint16_t speed_outer, uint16_t speed_inner);
tb6612_status_t tb6612_car_turn_right(tb6612_t *dev, uint16_t speed_outer, uint16_t speed_inner);
tb6612_status_t tb6612_car_spin_left(tb6612_t *dev, uint16_t speed_permille);
tb6612_status_t tb6612_car_spin_right(tb6612_t *dev, uint16_t speed_permille);
tb6612_status_t tb6612_car_stop(tb6612_t *dev, bool use_brake);

bool tb6612_is_initialized(const tb6612_t *dev);
tb6612_status_t tb6612_get_speed(const tb6612_t *dev, uint8_t channel, uint16_t *speed_permille);
tb6612_status_t tb6612_get_direction(const tb6612_t *dev, uint8_t channel, uint8_t *direction);

/*
 * Porting: only gpio_set and pwm_set are required. get_tick_ms can be NULL
 * (then run_ramp with duration_ms>0 behaves as immediate run). For car_* APIs
 * set channel_left, channel_right, left_forward_dir, right_forward_dir to match
 * your wiring (e.g. A=left, B=right; CW/CCW so that "forward" moves the car).
 * STM32: gpio_set -> HAL_GPIO_WritePin; pwm_set -> __HAL_TIM_SET_COMPARE;
 * get_tick_ms -> HAL_GetTick.
 */

#ifdef __cplusplus
}
#endif

#endif
