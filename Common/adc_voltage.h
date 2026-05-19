/**
 * @file adc_voltage.h
 * @brief ADC1 扫描 DMA：PA0 音频、PA1 水位。
 */

#ifndef ADC_VOLTAGE_H
#define ADC_VOLTAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef ADC_VOLTAGE_DMA_DEPTH
/** 每路通道在 DMA 缓冲中的采样点数（总 HAL 长度 = DEPTH * 2）。 */
#define ADC_VOLTAGE_DMA_DEPTH 32u
#endif

/** 在 MX_ADC1_Init 之后调用：校准并启动 DMA 环形采样。 */
void adc_voltage_init(void);

/** PA0 / ADC1 IN0：音频（麦克风）平均 ADC 原始值，0~4095。 */
uint16_t adc_voltage_audio_raw(void);

/** PA1 / ADC1 IN1：水位传感器平均 ADC 原始值，0~4095。 */
uint16_t adc_voltage_water_raw(void);

/** 音频通道电压，单位 mV（基于 VDD_VALUE）。 */
uint32_t adc_voltage_audio_mv(void);

/** 水位通道电压，单位 mV（基于 VDD_VALUE）。 */
uint32_t adc_voltage_water_mv(void);

/** 音频通道电压，单位 V。 */
float adc_voltage_audio_volts(void);

/** 水位通道电压，单位 V。 */
float adc_voltage_water_volts(void);

/**
 * 水位百分比 0–100（分段线性，标定：25%@raw1600、50%@1900、75%@2050、100%@2200）。
 */
uint8_t adc_voltage_water_level_pct(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_VOLTAGE_H */
