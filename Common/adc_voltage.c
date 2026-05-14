/**
 * @file adc_voltage.c
 * @brief ADC1（PA0）+ ADC3（PA1）独立 DMA：ADC1 使用 DMA1 Ch1，ADC3 使用 DMA2 Ch1（STM32F103xE 手册）。
 */

#include "adc_voltage.h"

#include "adc.h"
#include "main.h"

#ifndef VDD_VALUE
#define VDD_VALUE 3300U
#endif

static volatile uint16_t adc1_dma_buf[ADC_VOLTAGE_DMA_DEPTH];
static volatile uint16_t adc3_dma_buf[ADC_VOLTAGE_DMA_DEPTH];

void adc_voltage_init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_ADCEx_Calibration_Start(&hadc3) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_dma_buf, ADC_VOLTAGE_DMA_DEPTH) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)adc3_dma_buf, ADC_VOLTAGE_DMA_DEPTH) != HAL_OK) {
    Error_Handler();
  }
}

uint16_t adc_voltage_avg_raw(adc_voltage_channel_t ch)
{
  const volatile uint16_t *buf = (ch == ADC_VOLTAGE_SOUND) ? adc1_dma_buf : adc3_dma_buf;
  uint32_t sum = 0u;
  for (uint32_t i = 0u; i < ADC_VOLTAGE_DMA_DEPTH; i++) {
    sum += (uint32_t)(buf[i] & 0x0FFFu);
  }
  return (uint16_t)(sum / ADC_VOLTAGE_DMA_DEPTH);
}

float adc_voltage_avg_volts(adc_voltage_channel_t ch)
{
  const float vdda = (float)VDD_VALUE * 0.001f;
  return vdda * (float)adc_voltage_avg_raw(ch) / 4095.f;
}
