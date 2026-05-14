/**
 * @file    tb6612.c
 * @brief   TB6612FNG dual motor driver implementation (requirements-aligned).
 */

#include "tb6612.h"
#include <stddef.h>

#define TB6612_PIN_LOW  0
#define TB6612_PIN_HIGH 1

static void set_motor_in(tb6612_t *dev, uint8_t channel, uint8_t in1, uint8_t in2)
{
    if (channel == TB6612_CHANNEL_A)
    {
        dev->gpio_set(TB6612_PIN_IN1, in1);
        dev->gpio_set(TB6612_PIN_IN2, in2);
    }
    else
    {
        dev->gpio_set(TB6612_PIN_IN3, in1);
        dev->gpio_set(TB6612_PIN_IN4, in2);
    }
}

static uint16_t clamp_speed(uint16_t s)
{
    if (s > TB6612_SPEED_MAX)
        return TB6612_SPEED_MAX;
    return s;
}

tb6612_status_t tb6612_register(tb6612_t *dev, const tb6612_config_t *cfg)
{
    if (dev == NULL || cfg == NULL)
        return TB6612_ERROR_PARAM;
    if (cfg->gpio_set == NULL || cfg->pwm_set == NULL)
        return TB6612_ERROR_PARAM;
    if (cfg->channel_left > TB6612_CHANNEL_B || cfg->channel_right > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;

    dev->gpio_set = cfg->gpio_set;
    dev->pwm_set = cfg->pwm_set;
    dev->get_tick_ms = cfg->get_tick_ms;
    dev->channel_left = cfg->channel_left;
    dev->channel_right = cfg->channel_right;
    dev->left_forward_dir = cfg->left_forward_dir;
    dev->right_forward_dir = cfg->right_forward_dir;
    dev->initialized = true;

    dev->current_speed[0] = 0;
    dev->current_speed[1] = 0;
    dev->current_direction[0] = TB6612_DIR_CW;
    dev->current_direction[1] = TB6612_DIR_CW;
    dev->ramp_active[0] = 0;
    dev->ramp_active[1] = 0;

    dev->gpio_set(TB6612_PIN_STBY, TB6612_PIN_HIGH);
    dev->pwm_set(TB6612_CHANNEL_A, 0);
    dev->pwm_set(TB6612_CHANNEL_B, 0);
    set_motor_in(dev, TB6612_CHANNEL_A, TB6612_PIN_LOW, TB6612_PIN_LOW);
    set_motor_in(dev, TB6612_CHANNEL_B, TB6612_PIN_LOW, TB6612_PIN_LOW);

    return TB6612_OK;
}

tb6612_status_t tb6612_run(tb6612_t *dev, uint8_t channel, uint8_t direction, uint16_t speed_permille)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;

    speed_permille = clamp_speed(speed_permille);
    dev->ramp_active[channel] = 0;

    if (direction != dev->current_direction[channel])
    {
        set_motor_in(dev, channel, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
        dev->pwm_set(channel, TB6612_SPEED_MAX);
        dev->current_speed[channel] = TB6612_SPEED_MAX;
        dev->current_direction[channel] = direction;
        set_motor_in(dev, channel,
            direction == TB6612_DIR_CW ? TB6612_PIN_HIGH : TB6612_PIN_LOW,
            direction == TB6612_DIR_CW ? TB6612_PIN_LOW : TB6612_PIN_HIGH);
    }
    else
    {
        dev->current_direction[channel] = direction;
        set_motor_in(dev, channel,
            direction == TB6612_DIR_CW ? TB6612_PIN_HIGH : TB6612_PIN_LOW,
            direction == TB6612_DIR_CW ? TB6612_PIN_LOW : TB6612_PIN_HIGH);
    }

    dev->pwm_set(channel, speed_permille);
    dev->current_speed[channel] = speed_permille;
    return TB6612_OK;
}

tb6612_status_t tb6612_run_both(tb6612_t *dev,
    uint8_t dir_a, uint16_t speed_a, uint8_t dir_b, uint16_t speed_b)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (dir_a > TB6612_DIR_CCW || dir_b > TB6612_DIR_CCW)
        return TB6612_ERROR_PARAM;
    speed_a = clamp_speed(speed_a);
    speed_b = clamp_speed(speed_b);

    dev->ramp_active[TB6612_CHANNEL_A] = 0;
    dev->ramp_active[TB6612_CHANNEL_B] = 0;

    if (dir_a != dev->current_direction[TB6612_CHANNEL_A])
    {
        set_motor_in(dev, TB6612_CHANNEL_A, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
        dev->pwm_set(TB6612_CHANNEL_A, TB6612_SPEED_MAX);
    }
    dev->current_direction[TB6612_CHANNEL_A] = dir_a;
    set_motor_in(dev, TB6612_CHANNEL_A,
        dir_a == TB6612_DIR_CW ? TB6612_PIN_HIGH : TB6612_PIN_LOW,
        dir_a == TB6612_DIR_CW ? TB6612_PIN_LOW : TB6612_PIN_HIGH);
    dev->pwm_set(TB6612_CHANNEL_A, speed_a);
    dev->current_speed[TB6612_CHANNEL_A] = speed_a;

    if (dir_b != dev->current_direction[TB6612_CHANNEL_B])
    {
        set_motor_in(dev, TB6612_CHANNEL_B, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
        dev->pwm_set(TB6612_CHANNEL_B, TB6612_SPEED_MAX);
    }
    dev->current_direction[TB6612_CHANNEL_B] = dir_b;
    set_motor_in(dev, TB6612_CHANNEL_B,
        dir_b == TB6612_DIR_CW ? TB6612_PIN_HIGH : TB6612_PIN_LOW,
        dir_b == TB6612_DIR_CW ? TB6612_PIN_LOW : TB6612_PIN_HIGH);
    dev->pwm_set(TB6612_CHANNEL_B, speed_b);
    dev->current_speed[TB6612_CHANNEL_B] = speed_b;

    return TB6612_OK;
}

tb6612_status_t tb6612_run_ramp(tb6612_t *dev, uint8_t channel, uint8_t direction,
    uint16_t target_permille, uint32_t duration_ms)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;

    target_permille = clamp_speed(target_permille);

    if (duration_ms == 0 || dev->get_tick_ms == NULL)
    {
        return tb6612_run(dev, channel, direction, target_permille);
    }

    dev->ramp_start_speed[channel] = dev->current_speed[channel];
    dev->ramp_target_speed[channel] = target_permille;
    dev->ramp_target_direction[channel] = direction;
    dev->ramp_start_ms[channel] = dev->get_tick_ms();
    dev->ramp_duration_ms[channel] = duration_ms;
    dev->ramp_active[channel] = 1;

    if (direction != dev->current_direction[channel])
    {
        set_motor_in(dev, channel, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
        dev->pwm_set(channel, TB6612_SPEED_MAX);
        dev->current_direction[channel] = direction;
        set_motor_in(dev, channel,
            direction == TB6612_DIR_CW ? TB6612_PIN_HIGH : TB6612_PIN_LOW,
            direction == TB6612_DIR_CW ? TB6612_PIN_LOW : TB6612_PIN_HIGH);
        dev->ramp_start_speed[channel] = TB6612_SPEED_MAX;
    }
    return TB6612_OK;
}

void tb6612_poll(tb6612_t *dev)
{
    if (dev == NULL || !dev->initialized || dev->get_tick_ms == NULL)
        return;

    for (uint8_t ch = 0; ch <= TB6612_CHANNEL_B; ch++)
    {
        if (!dev->ramp_active[ch])
            continue;

        uint32_t now = dev->get_tick_ms();
        uint32_t elapsed = now - dev->ramp_start_ms[ch];
        if (elapsed >= dev->ramp_duration_ms[ch])
        {
            dev->current_speed[ch] = dev->ramp_target_speed[ch];
            dev->pwm_set(ch, dev->ramp_target_speed[ch]);
            dev->ramp_active[ch] = 0;
            continue;
        }

        uint32_t start = dev->ramp_start_speed[ch];
        uint32_t target = dev->ramp_target_speed[ch];
        uint32_t speed = start + (target - start) * elapsed / dev->ramp_duration_ms[ch];
        if (speed > TB6612_SPEED_MAX)
            speed = TB6612_SPEED_MAX;
        dev->current_speed[ch] = (uint16_t)speed;
        dev->pwm_set(ch, (uint16_t)speed);
    }
}

tb6612_status_t tb6612_brake(tb6612_t *dev, uint8_t channel)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;

    dev->ramp_active[channel] = 0;
    set_motor_in(dev, channel, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
    dev->pwm_set(channel, TB6612_SPEED_MAX);
    dev->current_speed[channel] = TB6612_SPEED_MAX;
    return TB6612_OK;
}

tb6612_status_t tb6612_coast(tb6612_t *dev, uint8_t channel)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;

    dev->ramp_active[channel] = 0;
    set_motor_in(dev, channel, TB6612_PIN_LOW, TB6612_PIN_LOW);
    dev->pwm_set(channel, 0);
    dev->current_speed[channel] = 0;
    return TB6612_OK;
}

tb6612_status_t tb6612_standby(tb6612_t *dev, bool enable)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;

    if (enable)
    {
        set_motor_in(dev, TB6612_CHANNEL_A, TB6612_PIN_LOW, TB6612_PIN_LOW);
        set_motor_in(dev, TB6612_CHANNEL_B, TB6612_PIN_LOW, TB6612_PIN_LOW);
        dev->pwm_set(TB6612_CHANNEL_A, 0);
        dev->pwm_set(TB6612_CHANNEL_B, 0);
        dev->current_speed[0] = 0;
        dev->current_speed[1] = 0;
        dev->ramp_active[0] = 0;
        dev->ramp_active[1] = 0;
        dev->gpio_set(TB6612_PIN_STBY, TB6612_PIN_LOW);
    }
    else
    {
        dev->gpio_set(TB6612_PIN_STBY, TB6612_PIN_HIGH);
    }
    return TB6612_OK;
}

tb6612_status_t tb6612_emergency_stop(tb6612_t *dev)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;

    dev->ramp_active[0] = 0;
    dev->ramp_active[1] = 0;
    set_motor_in(dev, TB6612_CHANNEL_A, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
    set_motor_in(dev, TB6612_CHANNEL_B, TB6612_PIN_HIGH, TB6612_PIN_HIGH);
    dev->pwm_set(TB6612_CHANNEL_A, TB6612_SPEED_MAX);
    dev->pwm_set(TB6612_CHANNEL_B, TB6612_SPEED_MAX);
    dev->current_speed[0] = TB6612_SPEED_MAX;
    dev->current_speed[1] = TB6612_SPEED_MAX;
    return TB6612_OK;
}

tb6612_status_t tb6612_car_forward(tb6612_t *dev, uint16_t speed_permille)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_permille = clamp_speed(speed_permille);
    tb6612_run(dev, dev->channel_left, dev->left_forward_dir, speed_permille);
    tb6612_run(dev, dev->channel_right, dev->right_forward_dir, speed_permille);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_backward(tb6612_t *dev, uint16_t speed_permille)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_permille = clamp_speed(speed_permille);
    uint8_t left_back = dev->left_forward_dir == TB6612_DIR_CW ? TB6612_DIR_CCW : TB6612_DIR_CW;
    uint8_t right_back = dev->right_forward_dir == TB6612_DIR_CW ? TB6612_DIR_CCW : TB6612_DIR_CW;
    tb6612_run(dev, dev->channel_left, left_back, speed_permille);
    tb6612_run(dev, dev->channel_right, right_back, speed_permille);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_turn_left(tb6612_t *dev, uint16_t speed_outer, uint16_t speed_inner)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_outer = clamp_speed(speed_outer);
    speed_inner = clamp_speed(speed_inner);
    tb6612_run(dev, dev->channel_left, dev->left_forward_dir, speed_inner);
    tb6612_run(dev, dev->channel_right, dev->right_forward_dir, speed_outer);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_turn_right(tb6612_t *dev, uint16_t speed_outer, uint16_t speed_inner)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_outer = clamp_speed(speed_outer);
    speed_inner = clamp_speed(speed_inner);
    tb6612_run(dev, dev->channel_left, dev->left_forward_dir, speed_outer);
    tb6612_run(dev, dev->channel_right, dev->right_forward_dir, speed_inner);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_spin_left(tb6612_t *dev, uint16_t speed_permille)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_permille = clamp_speed(speed_permille);
    uint8_t left_back = dev->left_forward_dir == TB6612_DIR_CW ? TB6612_DIR_CCW : TB6612_DIR_CW;
    tb6612_run(dev, dev->channel_left, left_back, speed_permille);
    tb6612_run(dev, dev->channel_right, dev->right_forward_dir, speed_permille);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_spin_right(tb6612_t *dev, uint16_t speed_permille)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    speed_permille = clamp_speed(speed_permille);
    uint8_t right_back = dev->right_forward_dir == TB6612_DIR_CW ? TB6612_DIR_CCW : TB6612_DIR_CW;
    tb6612_run(dev, dev->channel_left, dev->left_forward_dir, speed_permille);
    tb6612_run(dev, dev->channel_right, right_back, speed_permille);
    return TB6612_OK;
}

tb6612_status_t tb6612_car_stop(tb6612_t *dev, bool use_brake)
{
    if (dev == NULL || !dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (use_brake)
    {
        tb6612_brake(dev, dev->channel_left);
        tb6612_brake(dev, dev->channel_right);
    }
    else
    {
        tb6612_coast(dev, dev->channel_left);
        tb6612_coast(dev, dev->channel_right);
    }
    return TB6612_OK;
}

bool tb6612_is_initialized(const tb6612_t *dev)
{
    return dev != NULL && dev->initialized;
}

tb6612_status_t tb6612_get_speed(const tb6612_t *dev, uint8_t channel, uint16_t *speed_permille)
{
    if (dev == NULL || speed_permille == NULL)
        return TB6612_ERROR_PARAM;
    if (!dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;
    *speed_permille = dev->current_speed[channel];
    return TB6612_OK;
}

tb6612_status_t tb6612_get_direction(const tb6612_t *dev, uint8_t channel, uint8_t *direction)
{
    if (dev == NULL || direction == NULL)
        return TB6612_ERROR_PARAM;
    if (!dev->initialized)
        return TB6612_ERROR_NOT_INIT;
    if (channel > TB6612_CHANNEL_B)
        return TB6612_ERROR_PARAM;
    *direction = dev->current_direction[channel];
    return TB6612_OK;
}
