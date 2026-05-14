/**
 * @file    xpt2046.h
 * @brief   XPT2046 resistive touch driver - hardware-agnostic, register ops
 */

#ifndef __XPT2046_H
#define __XPT2046_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*xpt2046_transfer_t)(const uint8_t *tx, uint8_t *rx, uint16_t len);
typedef void (*xpt2046_pin_t)(int high);
typedef void (*xpt2046_delay_us_t)(uint32_t us);

typedef struct {
  xpt2046_transfer_t transfer;
  xpt2046_pin_t     set_cs;
  xpt2046_delay_us_t delay_us;
} xpt2046_ops_t;

int xpt2046_init(const xpt2046_ops_t *ops);
void xpt2046_read(int16_t *x, int16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif
