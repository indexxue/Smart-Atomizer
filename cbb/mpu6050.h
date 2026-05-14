/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2026-02-12 15:07:27
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2026-02-12 16:03:54
 * @FilePath: \STM32F103RCT6\cbb\mpu6050.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/**
 * @file    mpu6050.h
 * @brief   Generic MPU6050 driver (I2C + delay callbacks, no MCU dependencies).
 */

 #ifndef __MPU6050_H
 #define __MPU6050_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdbool.h>
 #include <stddef.h>

 #ifdef __cplusplus
 extern "C" {
 #endif
 
 typedef int  (*mpu6050_i2c_write_t)(uint8_t addr, const uint8_t *data, uint16_t len);
 typedef int  (*mpu6050_i2c_read_t)(uint8_t addr, uint8_t *data, uint16_t len);
 typedef void (*mpu6050_delay_ms_t)(uint32_t ms);
 
 typedef enum
 {
     MPU6050_OK = 0,
     MPU6050_ERROR_I2C,
     MPU6050_ERROR_NOT_INIT,
     MPU6050_ERROR_PARAM,
     MPU6050_ERROR_ID
 } mpu6050_status_t;
 
 typedef enum
 {
     MPU6050_ACCEL_RANGE_2G  = 0,
     MPU6050_ACCEL_RANGE_4G  = 1,
     MPU6050_ACCEL_RANGE_8G  = 2,
     MPU6050_ACCEL_RANGE_16G = 3
 } mpu6050_accel_range_t;
 
 typedef enum
 {
     MPU6050_GYRO_RANGE_250DPS  = 0,
     MPU6050_GYRO_RANGE_500DPS  = 1,
     MPU6050_GYRO_RANGE_1000DPS = 2,
     MPU6050_GYRO_RANGE_2000DPS = 3
 } mpu6050_gyro_range_t;
 
 /* Registration/config structure */
 typedef struct
 {
     mpu6050_i2c_write_t   write;
     mpu6050_i2c_read_t    read;
     mpu6050_delay_ms_t    delay_ms;
     uint8_t               address;
     mpu6050_accel_range_t accel_range;
     mpu6050_gyro_range_t  gyro_range;
 } mpu6050_config_t;
 
 /* Device state */
 typedef struct
 {
     mpu6050_i2c_write_t   write;
     mpu6050_i2c_read_t    read;
     mpu6050_delay_ms_t    delay_ms;
     uint8_t               address;
     mpu6050_accel_range_t accel_range;
     mpu6050_gyro_range_t  gyro_range;
     bool                  initialized;
 } mpu6050_t;
 
 /* New init with config struct */
 mpu6050_status_t mpu6050_init_with_config(mpu6050_t *dev,
                                           const mpu6050_config_t *cfg);
 
 /* Optional legacy init wrapper */
 mpu6050_status_t mpu6050_init(mpu6050_t *dev,
                               uint8_t address,
                               mpu6050_i2c_write_t write,
                               mpu6050_i2c_read_t read,
                               mpu6050_delay_ms_t delay_ms);
 
 mpu6050_status_t mpu6050_set_accel_range(mpu6050_t *dev, mpu6050_accel_range_t range);
 mpu6050_status_t mpu6050_set_gyro_range(mpu6050_t *dev, mpu6050_gyro_range_t range);
 
 mpu6050_status_t mpu6050_read_raw(mpu6050_t *dev,
                                   int16_t *ax, int16_t *ay, int16_t *az,
                                   int16_t *gx, int16_t *gy, int16_t *gz);
 
 mpu6050_status_t mpu6050_read_temperature(mpu6050_t *dev, float *temp_c);
 
#ifdef __cplusplus
}
#endif
#endif /* __MPU6050_H */

