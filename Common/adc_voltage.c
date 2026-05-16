/**
 * @file adc_voltage.c
 * @brief ADC1 扫描 PA0/PA1，DMA1 Ch1 环形缓冲；按通道解交织后求平均。
 */

#include "adc_voltage.h"

#include "adc.h"
#include "main.h"

#ifndef VDD_VALUE
#define VDD_VALUE 3300U
#endif

#define ADC_SCAN_NCHAN      2u
#define ADC_DMA_LEN         (ADC_VOLTAGE_DMA_DEPTH * ADC_SCAN_NCHAN)
#define ADC_SLOT_AUDIO      0u
#define ADC_SLOT_WATER      1u
#define ADC_RAW_MASK        0x0FFFu
#define ADC_FULL_SCALE      4095u

static volatile uint16_t s_adc_dma_buf[ADC_DMA_LEN];

static uint16_t adc_voltage_slot_avg_raw(uint32_t slot)
{
  uint32_t sum = 0u;

  for (uint32_t i = 0u; i < ADC_VOLTAGE_DMA_DEPTH; i++) {
    sum += (uint32_t)(s_adc_dma_buf[i * ADC_SCAN_NCHAN + slot] & ADC_RAW_MASK);
  }

  return (uint16_t)(sum / ADC_VOLTAGE_DMA_DEPTH);
}

static uint32_t adc_voltage_raw_to_mv(uint16_t raw)
{
  return ((uint32_t)raw * (uint32_t)VDD_VALUE) / ADC_FULL_SCALE;
}

void adc_voltage_init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma_buf, ADC_DMA_LEN) != HAL_OK) {
    Error_Handler();
  }
}

uint16_t adc_voltage_audio_raw(void)
{
  return adc_voltage_slot_avg_raw(ADC_SLOT_AUDIO);
}

uint16_t adc_voltage_water_raw(void)
{
  return adc_voltage_slot_avg_raw(ADC_SLOT_WATER);
}

uint32_t adc_voltage_audio_mv(void)
{
  return adc_voltage_raw_to_mv(adc_voltage_audio_raw());
}

uint32_t adc_voltage_water_mv(void)
{
  return adc_voltage_raw_to_mv(adc_voltage_water_raw());
}

float adc_voltage_audio_volts(void)
{
  return (float)adc_voltage_audio_mv() * 0.001f;
}

float adc_voltage_water_volts(void)
{
  return (float)adc_voltage_water_mv() * 0.001f;
}
