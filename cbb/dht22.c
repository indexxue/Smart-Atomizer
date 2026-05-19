/**
 * @file    dht22.c
 * @brief   Generic DHT22 (AM2302) driver — classic 1-wire timing (STM32-style).
 */

#include "dht22.h"

#define DHT22_ACK_WAIT_MAX          100u
#define DHT22_BIT_SAMPLE_US_DEFAULT 40u
#define DHT22_RELEASE_US            30u

static void dht22_bus_low(dht22_t *dev)
{
    dev->pin_output();
    dev->pin_write(0u);
}

static void dht22_bus_release(dht22_t *dev)
{
    dev->pin_write(1u);
    dev->pin_input();
}

static uint8_t dht22_bus_level(const dht22_t *dev)
{
    return dev->pin_read() ? 1u : 0u;
}

static void dht22_wait_us(const dht22_t *dev, uint32_t us)
{
    if (dev->delay_us != NULL && us > 0u)
    {
        dev->delay_us(us);
    }
}

/** Wait until DATA == @p level or @p max_retry * 1 us elapsed. */
static bool dht22_wait_level(const dht22_t *dev, uint8_t level, uint32_t max_retry)
{
    while (max_retry > 0u)
    {
        if (dht22_bus_level(dev) == level)
        {
            return true;
        }
        dht22_wait_us(dev, 1u);
        max_retry--;
    }
    return false;
}

/** Host start: DATA low, then release and wait 30 us (DHT22_Rst). */
static dht22_status_t dht22_reset(dht22_t *dev)
{
    dht22_bus_low(dev);
    if (dev->delay_ms != NULL)
    {
        dev->delay_ms((uint32_t)dev->start_low_ms);
    }
    else
    {
        dht22_wait_us(dev, (uint32_t)dev->start_low_ms * 1000u);
    }

    dht22_bus_release(dev);
    dht22_wait_us(dev, DHT22_RELEASE_US);
    return DHT22_OK;
}

/** Sensor ACK: wait high→low (DHT22_Check). */
static dht22_status_t dht22_check(dht22_t *dev)
{
    if (!dht22_wait_level(dev, 0u, DHT22_ACK_WAIT_MAX))
    {
        return DHT22_ERROR_TIMEOUT;
    }
    if (!dht22_wait_level(dev, 1u, DHT22_ACK_WAIT_MAX))
    {
        return DHT22_ERROR_TIMEOUT;
    }
    return DHT22_OK;
}

/** One data bit: low edge, high edge, sample after 40 us (DHT22_Read_Bit). */
static dht22_status_t dht22_read_bit(const dht22_t *dev, uint8_t *bit)
{
    uint8_t sample_us = dev->bit_sample_delay_us;

    if (sample_us == 0u)
    {
        sample_us = DHT22_BIT_SAMPLE_US_DEFAULT;
    }

    if (!dht22_wait_level(dev, 0u, DHT22_ACK_WAIT_MAX))
    {
        return DHT22_ERROR_TIMEOUT;
    }
    if (!dht22_wait_level(dev, 1u, DHT22_ACK_WAIT_MAX))
    {
        return DHT22_ERROR_TIMEOUT;
    }

    dht22_wait_us(dev, (uint32_t)sample_us);
    *bit = dht22_bus_level(dev);
    return DHT22_OK;
}

static dht22_status_t dht22_read_byte(const dht22_t *dev, uint8_t *byte)
{
    uint8_t dat = 0u;

    for (uint8_t i = 0u; i < 8u; i++)
    {
        uint8_t b;
        dht22_status_t st = dht22_read_bit(dev, &b);

        if (st != DHT22_OK)
        {
            return st;
        }
        dat <<= 1;
        if (b != 0u)
        {
            dat |= 1u;
        }
    }

    *byte = dat;
    return DHT22_OK;
}

static bool dht22_checksum_ok(const uint8_t raw[5])
{
    uint8_t sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    return (sum == raw[4]);
}

static void dht22_raw_to_phys(const uint8_t raw[5], float *temp_c, float *rh_pct)
{
    uint16_t rh_raw = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    uint16_t t_raw  = (uint16_t)(((uint16_t)raw[2] << 8) | raw[3]);

    *rh_pct = (float)rh_raw / 10.0f;

    if ((raw[2] & 0x80u) != 0u)
    {
        t_raw = (uint16_t)(t_raw & 0x7FFFu);
        *temp_c = -((float)t_raw / 10.0f);
    }
    else
    {
        *temp_c = (float)t_raw / 10.0f;
    }
}

static dht22_status_t dht22_guard_interval(dht22_t *dev)
{
    uint32_t now;
    uint32_t elapsed;

    if (!dev->enforce_min_interval || dev->get_ms == NULL || dev->delay_ms == NULL)
    {
        return DHT22_OK;
    }

    now = dev->get_ms();
    if (dev->last_read_ms == 0u)
    {
        return DHT22_OK;
    }

    elapsed = now - dev->last_read_ms;
    if (elapsed < DHT22_MIN_INTERVAL_MS)
    {
        dev->delay_ms(DHT22_MIN_INTERVAL_MS - elapsed);
    }
    return DHT22_OK;
}

static void dht22_stamp_read(dht22_t *dev)
{
    if (dev->get_ms != NULL)
    {
        dev->last_read_ms = dev->get_ms();
    }
}

dht22_status_t dht22_init_with_config(dht22_t *dev, const dht22_config_t *cfg)
{
    if (dev == NULL || cfg == NULL)
    {
        return DHT22_ERROR_PARAM;
    }
    if (cfg->pin_write == NULL || cfg->pin_read == NULL ||
        cfg->pin_output == NULL || cfg->pin_input == NULL || cfg->delay_us == NULL)
    {
        return DHT22_ERROR_PARAM;
    }

    dev->pin_write            = cfg->pin_write;
    dev->pin_read             = cfg->pin_read;
    dev->pin_output           = cfg->pin_output;
    dev->pin_input            = cfg->pin_input;
    dev->delay_us             = cfg->delay_us;
    dev->delay_ms             = cfg->delay_ms;
    dev->get_ms               = cfg->get_ms;
    dev->start_low_ms         = (cfg->start_low_ms != 0u) ? cfg->start_low_ms
                                                          : DHT22_START_LOW_MS_DEFAULT;
    dev->bit_sample_delay_us  = cfg->bit_sample_delay_us;
    dev->enforce_min_interval = cfg->enforce_min_interval;
    dev->last_read_ms         = 0u;
    dev->initialized          = true;

    dev->pin_output();
    dev->pin_write(1u);
    return DHT22_OK;
}

dht22_status_t dht22_init(dht22_t *dev,
                          dht22_pin_write_t pin_write,
                          dht22_pin_read_t pin_read,
                          dht22_pin_output_t pin_output,
                          dht22_pin_input_t pin_input,
                          dht22_delay_us_t delay_us,
                          dht22_delay_ms_t delay_ms)
{
    dht22_config_t cfg;
    cfg.pin_write            = pin_write;
    cfg.pin_read             = pin_read;
    cfg.pin_output           = pin_output;
    cfg.pin_input            = pin_input;
    cfg.delay_us             = delay_us;
    cfg.delay_ms             = delay_ms;
    cfg.get_ms               = NULL;
    cfg.start_low_ms         = 0u;
    cfg.bit_sample_delay_us  = 0u;
    cfg.enforce_min_interval = false;
    return dht22_init_with_config(dev, &cfg);
}

dht22_status_t dht22_read_raw(dht22_t *dev, uint8_t raw[5])
{
    dht22_status_t st;

    if (dev == NULL || !dev->initialized || raw == NULL)
    {
        return DHT22_ERROR_NOT_INIT;
    }

    st = dht22_guard_interval(dev);
    if (st != DHT22_OK)
    {
        return st;
    }

    st = dht22_reset(dev);
    if (st != DHT22_OK)
    {
        return st;
    }

    st = dht22_check(dev);
    if (st != DHT22_OK)
    {
        return st;
    }

    for (uint8_t i = 0u; i < 5u; i++)
    {
        st = dht22_read_byte(dev, &raw[i]);
        if (st != DHT22_OK)
        {
            return st;
        }
    }

    if (!dht22_checksum_ok(raw))
    {
        return DHT22_ERROR_CHECKSUM;
    }

    dht22_stamp_read(dev);
    return DHT22_OK;
}

dht22_status_t dht22_read(dht22_t *dev, float *temp_c, float *rh_pct)
{
    uint8_t raw[5];
    dht22_status_t st;

    if (temp_c == NULL || rh_pct == NULL)
    {
        return DHT22_ERROR_PARAM;
    }

    st = dht22_read_raw(dev, raw);
    if (st != DHT22_OK)
    {
        return st;
    }

    dht22_raw_to_phys(raw, temp_c, rh_pct);
    return DHT22_OK;
}
