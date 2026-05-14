/**
 * @file    strip.h
 * @brief   WS2818B / WS2812-family strip on SPI MOSI (3-bit SPI cell @ ~2.4 MHz SCK).
 *          Same GRB order and status codes as @c ws2818b.h; optional buffer share with @c ws2818b_t.
 *          SPI TX uses DMA when @c SPI_HandleTypeDef::hdmatx is linked (SPI1 / DMA1 Ch3).
 */

#ifndef STRIP_H
#define STRIP_H

#include "ws2818b.h"
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Zero bytes appended after payload for latch LOW (length tuned for ~2.25 MHz class SCK). */
#define STRIP_SPI_RESET_PAD_BYTES  96u

typedef struct
{
    SPI_HandleTypeDef *hspi;
    uint8_t           *grb_buf;
    uint8_t           *spi_tx_buf;
    uint16_t            num_leds;
    uint32_t            spi_pclk_hz;
    bool                initialized;
} strip_t;

/** Total MOSI bytes: encoded GRB + reset pad. */
#define STRIP_SPI_TX_BYTES(n_leds)  (9u * (uint32_t)(n_leds) + STRIP_SPI_RESET_PAD_BYTES)

ws2818b_status_t strip_register(strip_t *dev, SPI_HandleTypeDef *hspi,
    uint8_t *grb_buf, uint8_t *spi_tx_buf, uint16_t num_leds, uint32_t spi_pclk_hz);

/**
 * Use an existing @c ws2818b_t pixel buffer; refresh with @ref strip_show_spi.
 * @p ws must already be @c ws2818b_register()'d (bit-bang HW may remain unused).
 */
ws2818b_status_t strip_attach_ws2818b(strip_t *dev, ws2818b_t *ws, SPI_HandleTypeDef *hspi,
    uint8_t *spi_tx_buf, uint32_t spi_pclk_hz);

ws2818b_status_t strip_set_pixel(strip_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
ws2818b_status_t strip_fill(strip_t *dev, uint8_t r, uint8_t g, uint8_t b);

/**
 * Encode GRB, retune SPI BR for ~2.4 MHz SCK, transmit on MOSI.
 * Uses @c HAL_SPI_Transmit_DMA when @c hspi->hdmatx is non-NULL (blocking wait until done).
 */
ws2818b_status_t strip_show_spi(strip_t *dev);

bool strip_is_initialized(const strip_t *dev);

/* -------------------------------------------------------------------------- */
/* 4-LED strip scene (same scheduling style as led_scene.c)                   */
/* -------------------------------------------------------------------------- */

#define STRIP_SCENE_ACTION_NUM    3
#define STRIP_SCENE_MSEC          1
#define STRIP_SCENE_TICK_MS       50
#define STRIP_SCENE_LED_NUM       4
#define STRIP_SCENE_CYCLE_ALWAYS  0xFFFFu

typedef enum
{
    STRIP_SCENE_PRIO_FACTORY = 0,
    STRIP_SCENE_PRIO_PAIR,
    STRIP_SCENE_PRIO_NORMAL,
    STRIP_SCENE_PRIO_LOW,
    STRIP_SCENE_PRIO_MAX_NUM,
} strip_scene_prio_e;

typedef enum
{
    STRIP_SCENE_ID_BOOTUP = 0,
    STRIP_SCENE_ID_PAIRING,
    STRIP_SCENE_ID_TRIGGER,
    STRIP_SCENE_ID_ERROR,
    STRIP_SCENE_ID_SUCCESS,
    STRIP_SCENE_ID_MAX_NUM,
} strip_scene_id_e;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} strip_scene_rgb_t;

typedef enum
{
    STRIP_ACTION_ONOFF = 0,
    STRIP_ACTION_FADE,
} strip_scene_action_type_e;

typedef struct
{
    uint8_t cycle;
    strip_scene_action_type_e type;
    union
    {
        struct
        {
            strip_scene_rgb_t value;
            uint32_t lifetime;
        } onoff;
        struct
        {
            strip_scene_rgb_t start_value;
            strip_scene_rgb_t end_value;
            uint8_t step;
            uint32_t interval;
        } fade;
    } sub;
} strip_scene_action_t;

typedef struct
{
    uint16_t cycle;
    uint8_t num;
    strip_scene_action_t action[STRIP_SCENE_ACTION_NUM];
} strip_scene_t;

typedef struct
{
    strip_scene_prio_e prio;
    const strip_scene_t *scene;
} strip_scene_tab_t;

void strip_scene_attach(strip_t *strip);
void strip_scene_init(void);
void strip_scene_update(void);
void strip_scene_run(strip_scene_id_e id);
void strip_scene_cancel(strip_scene_id_e id);

#ifdef __cplusplus
}
#endif

#endif
