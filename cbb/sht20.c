/**
 * @file    sht20.c
 * @brief   Generic SHT20 (SHT2x) driver — I2C command protocol, callback-based.
 */

#include "sht20.h"

/* Commands (Sensirion SHT2x datasheet) */
#define SHT20_CMD_TRIG_T_NO_HOLD  0xF3u
#define SHT20_CMD_TRIG_RH_NO_HOLD 0xF5u
#define SHT20_CMD_READ_USER_REG   0xE7u
#define SHT20_CMD_SOFT_RESET      0xFEu

#define SHT20_DEFAULT_TEMP_WAIT_MS 85u
#define SHT20_DEFAULT_RH_WAIT_MS    30u
#define SHT20_RESET_WAIT_MS         20u

static uint8_t sht20_crc8_push_byte(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (uint8_t i = 8u; i > 0u; i--)
    {
        if (crc & 0x80u)
        {
            crc = (uint8_t)((crc << 1) ^ 0x31u);
        }
        else
        {
            crc <<= 1;
        }
    }
    return crc;
}

static uint8_t sht20_crc8_2data(uint8_t data_msb, uint8_t data_lsb)
{
    uint8_t crc = 0u;
    crc = sht20_crc8_push_byte(crc, data_msb);
    crc = sht20_crc8_push_byte(crc, data_lsb);
    return crc;
}

static sht20_status_t sht20_write_cmd(sht20_t *dev, uint8_t cmd)
{
    if (dev == NULL || dev->write == NULL)
    {
        return SHT20_ERROR_NOT_INIT;
    }
    if (dev->write(dev->address, &cmd, 1u) != 0)
    {
        return SHT20_ERROR_I2C;
    }
    return SHT20_OK;
}

static void sht20_wait_meas(sht20_t *dev, uint16_t wait_ms)
{
    if (dev != NULL && dev->delay_ms != NULL && wait_ms > 0u)
    {
        dev->delay_ms((uint32_t)wait_ms);
    }
}

static sht20_status_t sht20_read_meas_raw(sht20_t *dev, uint16_t *raw_out)
{
    uint8_t buf[3];

    if (dev == NULL || dev->read == NULL || raw_out == NULL)
    {
        return SHT20_ERROR_PARAM;
    }
    if (dev->read(dev->address, buf, 3u) != 0)
    {
        return SHT20_ERROR_I2C;
    }
    if (dev->verify_crc)
    {
        if (sht20_crc8_2data(buf[0], buf[1]) != buf[2])
        {
            return SHT20_ERROR_CRC;
        }
    }
    *raw_out = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return SHT20_OK;
}

static float sht20_raw_to_temp_c(uint16_t raw_st)
{
    raw_st &= 0xFFFCu;
    return -46.85f + (175.72f * (float)raw_st / 65536.0f);
}

static float sht20_raw_to_rh_pct(uint16_t raw_srh)
{
    raw_srh &= 0xFFFCu;
    float rh = -6.0f + (125.0f * (float)raw_srh / 65536.0f);
    if (rh < 0.0f)
    {
        rh = 0.0f;
    }
    if (rh > 100.0f)
    {
        rh = 100.0f;
    }
    return rh;
}

sht20_status_t sht20_init_with_config(sht20_t *dev, const sht20_config_t *cfg)
{
    if (dev == NULL || cfg == NULL || cfg->write == NULL || cfg->read == NULL)
    {
        return SHT20_ERROR_PARAM;
    }

    dev->write        = cfg->write;
    dev->read         = cfg->read;
    dev->delay_ms     = cfg->delay_ms;
    dev->address      = cfg->address;
    dev->temp_wait_ms = (cfg->temp_wait_ms != 0u) ? cfg->temp_wait_ms : SHT20_DEFAULT_TEMP_WAIT_MS;
    dev->rh_wait_ms   = (cfg->rh_wait_ms != 0u) ? cfg->rh_wait_ms : SHT20_DEFAULT_RH_WAIT_MS;
    dev->verify_crc   = cfg->verify_crc;
    dev->initialized  = false;

    if (sht20_soft_reset(dev) != SHT20_OK)
    {
        return SHT20_ERROR_I2C;
    }

    dev->initialized = true;
    return SHT20_OK;
}

sht20_status_t sht20_init(sht20_t *dev,
                          uint8_t address,
                          sht20_i2c_write_t write,
                          sht20_i2c_read_t read,
                          sht20_delay_ms_t delay_ms)
{
    sht20_config_t cfg;
    cfg.write        = write;
    cfg.read         = read;
    cfg.delay_ms     = delay_ms;
    cfg.address      = address;
    cfg.temp_wait_ms = 0u;
    cfg.rh_wait_ms   = 0u;
    cfg.verify_crc   = false;
    return sht20_init_with_config(dev, &cfg);
}

sht20_status_t sht20_soft_reset(sht20_t *dev)
{
    sht20_status_t st = sht20_write_cmd(dev, SHT20_CMD_SOFT_RESET);
    if (st != SHT20_OK)
    {
        return st;
    }
    sht20_wait_meas(dev, SHT20_RESET_WAIT_MS);
    return SHT20_OK;
}

sht20_status_t sht20_read_temperature_c(sht20_t *dev, float *temp_c)
{
    uint16_t raw;
    sht20_status_t mst;

    if (dev == NULL || !dev->initialized || temp_c == NULL)
    {
        return SHT20_ERROR_NOT_INIT;
    }

    if (sht20_write_cmd(dev, SHT20_CMD_TRIG_T_NO_HOLD) != SHT20_OK)
    {
        return SHT20_ERROR_I2C;
    }
    sht20_wait_meas(dev, dev->temp_wait_ms);

    mst = sht20_read_meas_raw(dev, &raw);
    if (mst != SHT20_OK)
    {
        return mst;
    }

    *temp_c = sht20_raw_to_temp_c(raw);
    return SHT20_OK;
}

sht20_status_t sht20_read_relative_humidity_pct(sht20_t *dev, float *rh_pct)
{
    uint16_t raw;
    sht20_status_t mst;

    if (dev == NULL || !dev->initialized || rh_pct == NULL)
    {
        return SHT20_ERROR_NOT_INIT;
    }

    if (sht20_write_cmd(dev, SHT20_CMD_TRIG_RH_NO_HOLD) != SHT20_OK)
    {
        return SHT20_ERROR_I2C;
    }
    sht20_wait_meas(dev, dev->rh_wait_ms);

    mst = sht20_read_meas_raw(dev, &raw);
    if (mst != SHT20_OK)
    {
        return mst;
    }

    *rh_pct = sht20_raw_to_rh_pct(raw);
    return SHT20_OK;
}

sht20_status_t sht20_read_temp_and_rh(sht20_t *dev, float *temp_c, float *rh_pct)
{
    sht20_status_t st;
    if (temp_c == NULL || rh_pct == NULL)
    {
        return SHT20_ERROR_PARAM;
    }
    st = sht20_read_temperature_c(dev, temp_c);
    if (st != SHT20_OK)
    {
        return st;
    }
    st = sht20_read_relative_humidity_pct(dev, rh_pct);
    if (st != SHT20_OK)
    {
        return st;
    }
    return SHT20_OK;
}

sht20_status_t sht20_read_user_register(sht20_t *dev, uint8_t *value)
{
    uint8_t buf[2];

    if (dev == NULL || !dev->initialized || value == NULL)
    {
        return SHT20_ERROR_NOT_INIT;
    }

    if (sht20_write_cmd(dev, SHT20_CMD_READ_USER_REG) != SHT20_OK)
    {
        return SHT20_ERROR_I2C;
    }
    if (dev->read(dev->address, buf, 2u) != 0)
    {
        return SHT20_ERROR_I2C;
    }
    if (dev->verify_crc)
    {
        if (sht20_crc8_push_byte(0u, buf[0]) != buf[1])
        {
            return SHT20_ERROR_CRC;
        }
    }
    *value = buf[0];
    return SHT20_OK;
}
