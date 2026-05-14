/**
 * @file    xpt2046.c
 * @brief   XPT2046 driver - uses registered ops only
 */

#include "xpt2046.h"
#include <string.h>

#define XPT2046_CMD_X     0xD0
#define XPT2046_CMD_Y     0x90
#define XPT2046_RAW_MIN   200
#define XPT2046_RAW_MAX   3900
#define XPT2046_DISP_W    240
#define XPT2046_DISP_H    320

static xpt2046_ops_t s_ops;
static int s_inited = 0;

static uint16_t read_adc(uint8_t cmd)
{
  uint8_t tx[3] = { cmd, 0, 0 };
  uint8_t rx[3] = { 0 };
  if (s_ops.transfer)
    s_ops.transfer(tx, rx, 3);
  return (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]) >> 3;
}

int xpt2046_init(const xpt2046_ops_t *ops)
{
  if (ops == NULL || ops->transfer == NULL) return -1;
  memcpy(&s_ops, ops, sizeof(xpt2046_ops_t));
  s_inited = 1;
  if (s_ops.set_cs) s_ops.set_cs(1);
  return 0;
}

void xpt2046_read(int16_t *x, int16_t *y, bool *pressed)
{
  if (!s_inited || !x || !y || !pressed) return;

  if (s_ops.set_cs) s_ops.set_cs(0);
  if (s_ops.delay_us) s_ops.delay_us(1);

  uint16_t rx_val = read_adc(XPT2046_CMD_X);
  uint16_t ry_val = read_adc(XPT2046_CMD_Y);

  if (s_ops.set_cs) s_ops.set_cs(1);

  if (rx_val < XPT2046_RAW_MIN || rx_val > XPT2046_RAW_MAX ||
      ry_val < XPT2046_RAW_MIN || ry_val > XPT2046_RAW_MAX) {
    *pressed = false;
    return;
  }
  *pressed = true;
  *x = (int16_t)((rx_val - XPT2046_RAW_MIN) * XPT2046_DISP_W / (XPT2046_RAW_MAX - XPT2046_RAW_MIN));
  *y = (int16_t)((ry_val - XPT2046_RAW_MIN) * XPT2046_DISP_H / (XPT2046_RAW_MAX - XPT2046_RAW_MIN));
  if (*x < 0) *x = 0; else if (*x >= XPT2046_DISP_W) *x = XPT2046_DISP_W - 1;
  if (*y < 0) *y = 0; else if (*y >= XPT2046_DISP_H) *y = XPT2046_DISP_H - 1;
}
