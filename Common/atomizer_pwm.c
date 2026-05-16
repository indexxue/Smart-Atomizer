/**
 * @file atomizer_pwm.c
 * @brief TIM3 CH1 @ PB4：100 kHz PWM，占空比对应挡位 70/50/30/0%。
 *
 * 注：STM32F103 上 TIM3 更新 DMA 与 SPI1 TX 共用 DMA1 Ch3，且 HAL PWM DMA
 * 挂在 CC1 事件上，不适合做 SPWM，故采用固定占空比方波驱动。
 */

#include "atomizer_pwm.h"

#include "main.h"
#include "tim.h"

/* TIM3 @ 72 MHz: (PSC+1)*(ARR+1) = 720 -> 100 kHz */
#define ATOMIZER_TIM_ARR           719u
#define ATOMIZER_PWM_TICKS         (ATOMIZER_TIM_ARR + 1u)

/** 挡位 0..3 对应幅度 0/30/50/70 %（由低到高）。 */
static const uint8_t s_gear_amp_pct[ATOMIZER_PWM_GEAR_COUNT] = {0u, 30u, 50u, 70u};

static uint8_t s_gear_idx;
static uint8_t s_running;
static uint8_t s_protect;

static void atomizer_pwm_stop(void)
{
  if (s_running != 0u)
  {
    (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
    s_running = 0u;
  }
}

static void atomizer_pwm_apply(void)
{
  const uint8_t amp = s_gear_amp_pct[s_gear_idx];

  atomizer_pwm_stop();

  if (s_protect != 0u || amp == 0u)
  {
    return;
  }

  {
    uint32_t pulse = ((uint32_t)ATOMIZER_PWM_TICKS * (uint32_t)amp) / 100u;

    if (pulse > ATOMIZER_TIM_ARR)
    {
      pulse = ATOMIZER_TIM_ARR;
    }
    if (pulse < 1u)
    {
      pulse = 1u;
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, (uint32_t)pulse);
  }

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  s_running = 1u;
}

void atomizer_pwm_init(void)
{
  s_gear_idx = 0u;
  s_running = 0u;
  s_protect = 0u;
  atomizer_pwm_apply();
}

void atomizer_pwm_protect_set(bool protect)
{
  const uint8_t next = protect ? 1u : 0u;

  if (s_protect == next)
  {
    return;
  }

  s_protect = next;
  atomizer_pwm_apply();
}

bool atomizer_pwm_protect_active(void)
{
  return (s_protect != 0u);
}

bool atomizer_pwm_gear_increase(void)
{
  if (s_gear_idx >= (ATOMIZER_PWM_GEAR_COUNT - 1u))
  {
    return false;
  }

  s_gear_idx++;
  atomizer_pwm_apply();
  return true;
}

bool atomizer_pwm_gear_decrease(void)
{
  if (s_gear_idx == 0u)
  {
    return false;
  }

  s_gear_idx--;
  atomizer_pwm_apply();
  return true;
}

uint8_t atomizer_pwm_gear_index(void)
{
  return s_gear_idx;
}

uint8_t atomizer_pwm_amp_percent(void)
{
  return s_gear_amp_pct[s_gear_idx];
}
