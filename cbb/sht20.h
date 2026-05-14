/**
 * @file    sht20.h
 * @brief   Generic SHT20 (Sensirion SHT2x) temperature / RH driver over I2C.
 *
 * Uses command-style I2C (no register address). Callbacks match other cbb
 * drivers (see mpu6050): 7-bit @p addr, @p write sends raw bytes, @p read
 * performs a standalone read transaction after a measurement command.
 *
 * Default measurement path is "no hold master" with explicit delays, which
 * works reliably with typical HAL Master_Transmit + Master_Receive glue.
 */

#ifndef __SHT20_H
#define __SHT20_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default 7-bit I2C address for SHT20 / SHT21 / SHT25 */
#define SHT20_I2C_ADDR_DEFAULT 0x40u

typedef int (*sht20_i2c_write_t)(uint8_t addr, const uint8_t *data, uint16_t len);
typedef int (*sht20_i2c_read_t)(uint8_t addr, uint8_t *data, uint16_t len);
typedef void (*sht20_delay_ms_t)(uint32_t ms);

typedef enum
{
    SHT20_OK = 0,
    SHT20_ERROR_I2C,
    SHT20_ERROR_NOT_INIT,
    SHT20_ERROR_PARAM,
    SHT20_ERROR_CRC
} sht20_status_t;

typedef struct
{
    sht20_i2c_write_t write;
    sht20_i2c_read_t read;
    sht20_delay_ms_t delay_ms;
    uint8_t address;
    /** After TRIG_T no-hold: max 85 ms @ 14-bit; 0 = use driver default */
    uint16_t temp_wait_ms;
    /** After TRIG_RH no-hold: max 29 ms @ 12-bit; 0 = use driver default */
    uint16_t rh_wait_ms;
    bool verify_crc;
} sht20_config_t;

typedef struct
{
    sht20_i2c_write_t write;
    sht20_i2c_read_t read;
    sht20_delay_ms_t delay_ms;
    uint8_t address;
    uint16_t temp_wait_ms;
    uint16_t rh_wait_ms;
    bool verify_crc;
    bool initialized;
} sht20_t;

sht20_status_t sht20_init_with_config(sht20_t *dev, const sht20_config_t *cfg);

sht20_status_t sht20_init(sht20_t *dev,
                          uint8_t address,
                          sht20_i2c_write_t write,
                          sht20_i2c_read_t read,
                          sht20_delay_ms_t delay_ms);

sht20_status_t sht20_soft_reset(sht20_t *dev);

sht20_status_t sht20_read_temperature_c(sht20_t *dev, float *temp_c);

sht20_status_t sht20_read_relative_humidity_pct(sht20_t *dev, float *rh_pct);

sht20_status_t sht20_read_temp_and_rh(sht20_t *dev, float *temp_c, float *rh_pct);

sht20_status_t sht20_read_user_register(sht20_t *dev, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __SHT20_H */
