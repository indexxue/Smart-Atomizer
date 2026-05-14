/**
 * @file    ili9341.h
 * @brief   ILI9341 TFT driver - callback-based SPI/pins, MCU-agnostic, single register API.
 */

#ifndef __ILI9341_H
#define __ILI9341_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ILI9341_WIDTH   240
#define ILI9341_HEIGHT  320

typedef void (*ili9341_spi_tx_t)(const uint8_t *data, uint16_t len);
typedef void (*ili9341_pin_t)(int high);
typedef void (*ili9341_delay_ms_t)(uint32_t ms);

typedef enum
{
    ILI9341_OK = 0,
    ILI9341_ERROR_PARAM,
    ILI9341_ERROR_NOT_INIT,
} ili9341_status_t;

typedef struct
{
    ili9341_spi_tx_t    spi_tx;
    ili9341_pin_t      set_cs;
    ili9341_pin_t      set_dc;
    ili9341_pin_t      set_rst;
    ili9341_pin_t      set_bl;
    ili9341_delay_ms_t delay_ms;
} ili9341_config_t;

typedef struct
{
    ili9341_spi_tx_t    spi_tx;
    ili9341_pin_t      set_cs;
    ili9341_pin_t      set_dc;
    ili9341_pin_t      set_rst;
    ili9341_pin_t      set_bl;
    ili9341_delay_ms_t delay_ms;
    bool               initialized;
} ili9341_t;

ili9341_status_t ili9341_register(ili9341_t *dev, const ili9341_config_t *cfg);

ili9341_status_t ili9341_set_backlight(ili9341_t *dev, bool on);
ili9341_status_t ili9341_set_window(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
ili9341_status_t ili9341_write_pixels(ili9341_t *dev, const uint16_t *buf, uint32_t len);
void ili9341_end_write(ili9341_t *dev);

bool ili9341_is_initialized(const ili9341_t *dev);

#ifdef __cplusplus
}
#endif

#endif
