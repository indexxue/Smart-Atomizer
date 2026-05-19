/**
 * @file    humidity.c
 * @brief   DHT22 on PA2 — STM32 port for generic @ref dht22 driver.
 */

#include "humidity.h"
#include "dht22.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>

#define HUMIDITY_GPIO_PORT        GPIOA
#define HUMIDITY_GPIO_PIN         GPIO_PIN_2

/** Host start: DATA low (ms). DHT22 needs ≥1 ms; example uses 20 ms. */
#define HUMIDITY_START_LOW_MS     2u
/** Bit sample delay after rising edge (us). Example: 40 us. */
#define HUMIDITY_BIT_SAMPLE_US    40u

static dht22_t s_dht22;
static bool s_humidity_ready;

static void humidity_dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void humidity_delay_us(uint32_t us)
{
  const uint32_t start = DWT->CYCCNT;
  const uint32_t ticks = (SystemCoreClock / 1000000u) * us;

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

static void humidity_delay_ms(uint32_t ms)
{
  while (ms > 0u)
  {
    humidity_delay_us(1000u);
    ms--;
  }
}

static void humidity_pin_write(uint8_t level)
{
  if (level != 0u)
  {
    HAL_GPIO_WritePin(HUMIDITY_GPIO_PORT, HUMIDITY_GPIO_PIN, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(HUMIDITY_GPIO_PORT, HUMIDITY_GPIO_PIN, GPIO_PIN_RESET);
  }
}

static uint8_t humidity_pin_read(void)
{
  return (HAL_GPIO_ReadPin(HUMIDITY_GPIO_PORT, HUMIDITY_GPIO_PIN) == GPIO_PIN_SET)
             ? 1u
             : 0u;
}

static void humidity_pin_output(void)
{
  GPIO_InitTypeDef io = {0};

  io.Pin = HUMIDITY_GPIO_PIN;
  io.Mode = GPIO_MODE_OUTPUT_PP;
  io.Pull = GPIO_NOPULL;
  io.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(HUMIDITY_GPIO_PORT, &io);
}

static void humidity_pin_input(void)
{
  GPIO_InitTypeDef io = {0};

  io.Pin = HUMIDITY_GPIO_PIN;
  io.Mode = GPIO_MODE_INPUT;
  io.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HUMIDITY_GPIO_PORT, &io);
}

static humidity_status_t humidity_map_status(dht22_status_t st)
{
  switch (st)
  {
    case DHT22_OK:
      return HUMIDITY_OK;
    case DHT22_ERROR_CHECKSUM:
      return HUMIDITY_ERROR_CHECKSUM;
    case DHT22_ERROR_PARAM:
      return HUMIDITY_ERROR_PARAM;
    case DHT22_ERROR_NOT_INIT:
      return HUMIDITY_ERROR_PARAM;
    default:
      return HUMIDITY_ERROR_BUS;
  }
}

humidity_status_t humidity_init(void)
{
  dht22_config_t cfg;
  dht22_status_t st;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  humidity_dwt_init();
  SystemCoreClockUpdate();

  cfg.pin_write            = humidity_pin_write;
  cfg.pin_read             = humidity_pin_read;
  cfg.pin_output           = humidity_pin_output;
  cfg.pin_input            = humidity_pin_input;
  cfg.delay_us             = humidity_delay_us;
  cfg.delay_ms             = humidity_delay_ms;
  cfg.get_ms               = HAL_GetTick;
  cfg.start_low_ms         = HUMIDITY_START_LOW_MS;
  cfg.bit_sample_delay_us  = HUMIDITY_BIT_SAMPLE_US;
  /* 2 s spacing enforced by app timer (see APP_HUMIDITY_READ_MS). */
  cfg.enforce_min_interval = false;

  st = dht22_init_with_config(&s_dht22, &cfg);
  if (st != DHT22_OK)
  {
    return humidity_map_status(st);
  }

  s_humidity_ready = true;
  return HUMIDITY_OK;
}

humidity_status_t humidity_read(float *temp_c, float *rh_pct)
{
  dht22_status_t st;

  if (!s_humidity_ready || temp_c == NULL || rh_pct == NULL)
  {
    return HUMIDITY_ERROR_PARAM;
  }

  taskENTER_CRITICAL();
  st = dht22_read(&s_dht22, temp_c, rh_pct);
  taskEXIT_CRITICAL();

  return humidity_map_status(st);
}

humidity_status_t humidity_read_rh(float *rh_pct)
{
  float temp_c = 0.0f;

  if (rh_pct == NULL)
  {
    return HUMIDITY_ERROR_PARAM;
  }
  return humidity_read(&temp_c, rh_pct);
}
