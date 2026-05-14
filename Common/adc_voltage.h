/**
 * @file adc_voltage.h
 * @brief DMA 采样后的电压读数接口：ADC1 声压 PA0（DMA1 Ch1），ADC3 水位 PA1（DMA2 Ch1）。
 */

#ifndef ADC_VOLTAGE_H
#define ADC_VOLTAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef ADC_VOLTAGE_DMA_DEPTH
#define ADC_VOLTAGE_DMA_DEPTH 32u
#endif

typedef enum {
  ADC_VOLTAGE_SOUND = 0, /* ADC1 IN0 @ PA0 */
  ADC_VOLTAGE_WATER = 1  /* ADC3 IN1 @ PA1（STM32F103xE 上 PA1 为 ADC123_IN1） */
} adc_voltage_channel_t;

/** 在 MX_ADC1_Init、MX_ADC3_Init 之后调用：校准并启动两路 DMA。 */
void adc_voltage_init(void);

uint16_t adc_voltage_avg_raw(adc_voltage_channel_t ch);

float adc_voltage_avg_volts(adc_voltage_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* ADC_VOLTAGE_H */
