/**
 * @file    ws2818b.h
 * @brief   Generic WS2818B (Worldsemi) addressable RGB driver — single-wire GRB
 *          framing compatible with WS2812/WS2818 family (800 kHz data timing).
 *          MCU-agnostic: GPIO and timing via callbacks / configurable spin counts.
 */

#ifndef __WS2818B_H
#define __WS2818B_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ws2818b_din_set_t)(uint8_t level);
typedef void (*ws2818b_delay_us_t)(uint32_t us);

typedef enum
{
    WS2818B_OK = 0,
    WS2818B_ERROR_NOT_INIT,
    WS2818B_ERROR_PARAM,
} ws2818b_status_t;

/**
 * Hardware / timing port. Bit timing uses four busy-wait loop counts (tune with
 * a scope or logic analyzer). Each count is one iteration of an empty volatile
 * decrement loop in the driver (approximate; depends on compiler and flash wait).
 *
 * din_set: drive data line (0 = low, non-zero = high).
 * delay_us: optional microsecond delay for reset gap; if NULL, reset uses cpu_hz.
 * cpu_hz: system core clock when delay_us is NULL (also used to scale reset spin).
 */
typedef struct
{
    ws2818b_din_set_t   din_set;
    ws2818b_delay_us_t delay_us;
    uint32_t            cpu_hz;
    uint16_t            t0h_cycles;
    uint16_t            t0l_cycles;
    uint16_t            t1h_cycles;
    uint16_t            t1l_cycles;
    uint16_t            reset_us;
} ws2818b_hw_t;

typedef struct
{
    ws2818b_hw_t hw;
    uint8_t      *grb_buf;
    uint16_t      num_leds;
    bool          initialized;
} ws2818b_t;

/** @return Byte length of GRB buffer for @p n_leds pixels. */
#define WS2818B_BUF_LEN(n_leds)  ((uint32_t)(n_leds) * 3u)

ws2818b_status_t ws2818b_register(ws2818b_t *dev, const ws2818b_hw_t *hw,
    uint8_t *grb_buf, uint16_t num_leds);

/** Starting values for STM32F1 @ 72 MHz system clock (bit-bang via din_set); re-tune if needed. */
void ws2818b_hw_apply_defaults_72mhz(ws2818b_hw_t *hw);

ws2818b_status_t ws2818b_set_pixel(ws2818b_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
ws2818b_status_t ws2818b_fill(ws2818b_t *dev, uint8_t r, uint8_t g, uint8_t b);

/**
 * Shift GRB buffer to the chain. Disable interrupts for the duration if your
 * din_set path is not cycle-deterministic under preemptive RTOS / IRQ latency.
 */
ws2818b_status_t ws2818b_show(ws2818b_t *dev);

bool ws2818b_is_initialized(const ws2818b_t *dev);
uint16_t ws2818b_num_leds(const ws2818b_t *dev);
uint8_t *ws2818b_grb_buf(ws2818b_t *dev);

#ifdef __cplusplus
}
#endif

#endif
