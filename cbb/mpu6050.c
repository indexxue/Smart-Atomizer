/**
 * @file    mpu6050.c
 * @brief   Generic MPU6050 driver (I2C + delay callbacks, no MCU dependencies).
 */

 #include "mpu6050.h"

 /* Register map */
 #define MPU6050_REG_SMPLRT_DIV     0x19u
 #define MPU6050_REG_CONFIG         0x1Au
 #define MPU6050_REG_GYRO_CONFIG    0x1Bu
 #define MPU6050_REG_ACCEL_CONFIG   0x1Cu
 #define MPU6050_REG_INT_ENABLE     0x38u
 #define MPU6050_REG_ACCEL_XOUT_H   0x3Bu
 #define MPU6050_REG_TEMP_OUT_H     0x41u
 #define MPU6050_REG_GYRO_XOUT_H    0x43u
 #define MPU6050_REG_PWR_MGMT_1     0x6Bu
 #define MPU6050_REG_WHO_AM_I       0x75u
 
 #define MPU6050_WHO_AM_I_VALUE     0x69u
 
 static mpu6050_status_t mpu6050_write_reg(mpu6050_t *dev, uint8_t reg, uint8_t value)
 {
     if (dev == NULL || dev->write == NULL)
     {
         return MPU6050_ERROR_NOT_INIT;
     }
 
     uint8_t buf[2];
     buf[0] = reg;
     buf[1] = value;
 
     if (dev->write(dev->address, buf, 2u) != 0)
     {
         return MPU6050_ERROR_I2C;
     }
     return MPU6050_OK;
 }
 
 static mpu6050_status_t mpu6050_read_regs(mpu6050_t *dev, uint8_t reg, uint8_t *data, uint16_t len)
 {
     if (dev == NULL || dev->write == NULL || dev->read == NULL || data == NULL || len == 0u)
     {
         return MPU6050_ERROR_PARAM;
     }
 
     if (dev->write(dev->address, &reg, 1u) != 0)
     {
         return MPU6050_ERROR_I2C;
     }
     if (dev->read(dev->address, data, len) != 0)
     {
         return MPU6050_ERROR_I2C;
     }
     return MPU6050_OK;
 }
 
 /* New init with config struct */
 mpu6050_status_t mpu6050_init_with_config(mpu6050_t *dev,
                                           const mpu6050_config_t *cfg)
 {
     if (dev == NULL || cfg == NULL ||
         cfg->write == NULL || cfg->read == NULL)
     {
         return MPU6050_ERROR_PARAM;
     }
 
     dev->write       = cfg->write;
     dev->read        = cfg->read;
     dev->delay_ms    = cfg->delay_ms;
     dev->address     = cfg->address;
     dev->accel_range = cfg->accel_range;
     dev->gyro_range  = cfg->gyro_range;
     dev->initialized = false;
 
     uint8_t id = 0u;
     if (mpu6050_read_regs(dev, MPU6050_REG_WHO_AM_I, &id, 1u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
    if (id != MPU6050_WHO_AM_I_VALUE)
    {
        /* Some compatible devices may return a different ID. Continue anyway. */
    }
 
     if (mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x00u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
     if (dev->delay_ms != NULL)
     {
         dev->delay_ms(100u);
     }
 
     if (mpu6050_write_reg(dev, MPU6050_REG_SMPLRT_DIV, 0x07u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
    if (mpu6050_write_reg(dev, MPU6050_REG_CONFIG, 0x03u) != MPU6050_OK)
    {
        return MPU6050_ERROR_I2C;
    }

    dev->initialized = true;

    if (mpu6050_set_accel_range(dev, dev->accel_range) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
     if (mpu6050_set_gyro_range(dev, dev->gyro_range) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     if (mpu6050_write_reg(dev, MPU6050_REG_INT_ENABLE, 0x00u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     dev->initialized = true;
     return MPU6050_OK;
 }
 
 /* Legacy wrapper, optional */
 mpu6050_status_t mpu6050_init(mpu6050_t *dev,
                               uint8_t address,
                               mpu6050_i2c_write_t write,
                               mpu6050_i2c_read_t read,
                               mpu6050_delay_ms_t delay_ms)
 {
     mpu6050_config_t cfg;
     cfg.write       = write;
     cfg.read        = read;
     cfg.delay_ms    = delay_ms;
     cfg.address     = address;
     cfg.accel_range = MPU6050_ACCEL_RANGE_2G;
     cfg.gyro_range  = MPU6050_GYRO_RANGE_2000DPS;
     return mpu6050_init_with_config(dev, &cfg);
 }
 
 mpu6050_status_t mpu6050_set_accel_range(mpu6050_t *dev, mpu6050_accel_range_t range)
 {
     if (dev == NULL || !dev->initialized)
     {
         return MPU6050_ERROR_NOT_INIT;
     }
     if (range > MPU6050_ACCEL_RANGE_16G)
     {
         return MPU6050_ERROR_PARAM;
     }
 
     uint8_t cfg = 0u;
     if (mpu6050_read_regs(dev, MPU6050_REG_ACCEL_CONFIG, &cfg, 1u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
     cfg &= (uint8_t)~0x18u;
     cfg |= (uint8_t)((uint8_t)range << 3);
 
     if (mpu6050_write_reg(dev, MPU6050_REG_ACCEL_CONFIG, cfg) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     dev->accel_range = range;
     return MPU6050_OK;
 }
 
 mpu6050_status_t mpu6050_set_gyro_range(mpu6050_t *dev, mpu6050_gyro_range_t range)
 {
     if (dev == NULL || !dev->initialized)
     {
         return MPU6050_ERROR_NOT_INIT;
     }
     if (range > MPU6050_GYRO_RANGE_2000DPS)
     {
         return MPU6050_ERROR_PARAM;
     }
 
     uint8_t cfg = 0u;
     if (mpu6050_read_regs(dev, MPU6050_REG_GYRO_CONFIG, &cfg, 1u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
     cfg &= (uint8_t)~0x18u;
     cfg |= (uint8_t)((uint8_t)range << 3);
 
     if (mpu6050_write_reg(dev, MPU6050_REG_GYRO_CONFIG, cfg) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     dev->gyro_range = range;
     return MPU6050_OK;
 }
 
 mpu6050_status_t mpu6050_read_raw(mpu6050_t *dev,
                                   int16_t *ax, int16_t *ay, int16_t *az,
                                   int16_t *gx, int16_t *gy, int16_t *gz)
 {
     if (dev == NULL || !dev->initialized)
     {
         return MPU6050_ERROR_NOT_INIT;
     }
     if (ax == NULL || ay == NULL || az == NULL || gx == NULL || gy == NULL || gz == NULL)
     {
         return MPU6050_ERROR_PARAM;
     }
 
     uint8_t buf[14];
     if (mpu6050_read_regs(dev, MPU6050_REG_ACCEL_XOUT_H, buf, 14u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     *ax = (int16_t)((buf[0] << 8) | buf[1]);
     *ay = (int16_t)((buf[2] << 8) | buf[3]);
     *az = (int16_t)((buf[4] << 8) | buf[5]);
     *gx = (int16_t)((buf[8] << 8) | buf[9]);
     *gy = (int16_t)((buf[10] << 8) | buf[11]);
     *gz = (int16_t)((buf[12] << 8) | buf[13]);
 
     return MPU6050_OK;
 }
 
 mpu6050_status_t mpu6050_read_temperature(mpu6050_t *dev, float *temp_c)
 {
     if (dev == NULL || !dev->initialized || temp_c == NULL)
     {
         return MPU6050_ERROR_NOT_INIT;
     }
 
     uint8_t buf[2];
     if (mpu6050_read_regs(dev, MPU6050_REG_TEMP_OUT_H, buf, 2u) != MPU6050_OK)
     {
         return MPU6050_ERROR_I2C;
     }
 
     int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
     *temp_c = (float)raw / 340.0f + 36.53f;
 
     return MPU6050_OK;
 }
