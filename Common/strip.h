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
    /** 常亮（暖白）。 */
    STRIP_SCENE_ID_SOLID,
    /** 彩色呼吸（HSV 色相旋转 + 亮度起伏）。 */
    STRIP_SCENE_ID_COLOR_BREATH,
    /** 彩色流水（彩虹追逐）。 */
    STRIP_SCENE_ID_COLOR_CHASE,
    /** ADC1 / MAX9814: DMA 平均 + 包络驱动 VU（与 @c strip_scene_update 同步刷新）。 */
    STRIP_SCENE_ID_MIC_REACTIVE,
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
    /** 每帧从 ADC1（声压）取数并刷新灯带；不自动结束。 */
    STRIP_ACTION_MIC_STREAM,
    STRIP_ACTION_RGB_SOLID,
    STRIP_ACTION_RGB_BREATH,
    STRIP_ACTION_RGB_CHASE,
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
        struct
        {
            uint8_t reserved;
        } mic;
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

/** 重置 MIC 场景的直流跟踪与峰值（进入 @c STRIP_SCENE_ID_MIC_REACTIVE 时自动调用）。 */
void strip_scene_mic_reset(void);

/** Factory 灯带展示模式 1..4：常亮 / 呼吸 / 流水 / MIC(电压)。 */
#define STRIP_FACTORY_MODE_SOLID   1u
#define STRIP_FACTORY_MODE_BREATH  2u
#define STRIP_FACTORY_MODE_CHASE   3u
#define STRIP_FACTORY_MODE_MIC     4u

void strip_scene_factory_display_set(uint8_t mode_1_to_4);
uint8_t strip_scene_factory_display_get(void);
void strip_scene_factory_display_next(void);

#ifdef __cplusplus
}
#endif

#endif
