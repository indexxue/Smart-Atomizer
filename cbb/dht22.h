/**
 * @file    dht22.h
 * @brief   Generic DHT22 (AM2302) temperature / RH driver — single-wire bus.
 *
 * MCU-agnostic: GPIO direction, level, and microsecond delay via callbacks
 * (same style as ws2818b / sht20). Wire the module DATA pin to one GPIO with
 * a 4.7k–10k pull-up to VCC. Typical modules include the pull-up.
 *
 * DHT11 uses the same bus timing; use @ref dht22_read_raw and decode manually,
 * or call @ref dht22_read with a DHT11-specific port if you add conversion.
 *
 * Between measurements allow at least @ref DHT22_MIN_INTERVAL_MS (2 s for DHT22).
 * Disable or shorten interrupts during @ref dht22_read for reliable bit timing.
 */

#ifndef __DHT22_H
#define __DHT22_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum interval between successful reads (DHT22 datasheet). */
#define DHT22_MIN_INTERVAL_MS 2000u

/** Host start: DATA low time (ms). DHT22: ≥1 ms; default 2 ms (example ports often use 18–20 ms). */
#define DHT22_START_LOW_MS_DEFAULT 2u

typedef void (*dht22_pin_write_t)(uint8_t level);
typedef uint8_t (*dht22_pin_read_t)(void);
typedef void (*dht22_pin_output_t)(void);
typedef void (*dht22_pin_input_t)(void);
typedef void (*dht22_delay_us_t)(uint32_t us);
typedef void (*dht22_delay_ms_t)(uint32_t ms);
/** Optional monotonic millisecond tick (e.g. HAL_GetTick) for read spacing. */
typedef uint32_t (*dht22_get_ms_t)(void);

typedef enum
{
    DHT22_OK = 0,
    DHT22_ERROR_NOT_INIT,
    DHT22_ERROR_PARAM,
    DHT22_ERROR_TIMEOUT,
    DHT22_ERROR_CHECKSUM
} dht22_status_t;

typedef struct
{
    dht22_pin_write_t  pin_write;
    dht22_pin_read_t   pin_read;
    dht22_pin_output_t pin_output;
    dht22_pin_input_t  pin_input;
    dht22_delay_us_t   delay_us;
    dht22_delay_ms_t   delay_ms;
    dht22_get_ms_t     get_ms;
    /** Host pulls DATA low for this many ms before release; 0 = default. */
    uint16_t start_low_ms;
    /** Sample point after each bit rising edge (us); 0 = driver default (40). */
    uint8_t bit_sample_delay_us;
    /** Enforce @ref DHT22_MIN_INTERVAL_MS between reads when delay_ms is set. */
    bool enforce_min_interval;
} dht22_config_t;

typedef struct
{
    dht22_pin_write_t  pin_write;
    dht22_pin_read_t   pin_read;
    dht22_pin_output_t pin_output;
    dht22_pin_input_t  pin_input;
    dht22_delay_us_t   delay_us;
    dht22_delay_ms_t   delay_ms;
    dht22_get_ms_t     get_ms;
    uint16_t start_low_ms;
    uint8_t bit_sample_delay_us;
    bool enforce_min_interval;
    bool initialized;
    uint32_t last_read_ms;
} dht22_t;

dht22_status_t dht22_init_with_config(dht22_t *dev, const dht22_config_t *cfg);

dht22_status_t dht22_init(dht22_t *dev,
                          dht22_pin_write_t pin_write,
                          dht22_pin_read_t pin_read,
                          dht22_pin_output_t pin_output,
                          dht22_pin_input_t pin_input,
                          dht22_delay_us_t delay_us,
                          dht22_delay_ms_t delay_ms);

/**
 * Read 5 raw bytes: RH_H, RH_L, T_H, T_L, checksum.
 */
dht22_status_t dht22_read_raw(dht22_t *dev, uint8_t raw[5]);

/**
 * Read temperature (°C) and relative humidity (%).
 * DHT22: RH = (RH_H<<8|RH_L)/10, T = signed 0.1 °C from T_H/T_L.
 */
dht22_status_t dht22_read(dht22_t *dev, float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif

#endif /* __DHT22_H */
