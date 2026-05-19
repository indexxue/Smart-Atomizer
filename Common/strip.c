/**
 * @file    strip.c
 * @brief   SPI MOSI backend for WS2818B-compatible timing; DMA TX when linked; 4-LED scene.
 */

#include "strip.h"
#include "adc_voltage.h"
#include <stddef.h>
#include <string.h>

static void grb_to_spi_mosi(const uint8_t *grb, uint16_t num_leds, uint8_t *spi_out)
{
    uint16_t led;
    uint8_t cur = 0u;
    uint8_t nbits = 0u;

    for (led = 0u; led < num_leds; led++)
    {
        uint8_t cidx;
        for (cidx = 0u; cidx < 3u; cidx++)
        {
            uint8_t v = grb[(uint32_t)led * 3u + (uint32_t)cidx];
            int8_t bi;
            for (bi = 7; bi >= 0; bi--)
            {
                uint8_t pat = (((uint32_t)v >> (uint32_t)bi) & 1u) != 0u ? 6u : 4u;
                int8_t pi;
                for (pi = 2; pi >= 0; pi--)
                {
                    uint8_t b = (uint8_t)((pat >> (uint32_t)pi) & 1u);
                    cur = (uint8_t)((cur << 1) | b);
                    nbits++;
                    if (nbits == 8)
                    {
                        *spi_out++ = cur;
                        cur = 0u;
                        nbits = 0u;
                    }
                }
            }
        }
    }

    if (nbits != 0u)
    {
        *spi_out++ = (uint8_t)(cur << (8u - nbits));
    }
}

static uint32_t strip_pick_br_field(uint32_t pclk_hz)
{
    uint32_t target_hz = 2400000u;
    uint32_t best_err = 0xFFFFFFFFu;
    uint32_t best_field = 4u << SPI_CR1_BR_Pos;
    uint32_t idx;

    if (pclk_hz == 0u)
    {
        return best_field;
    }

    for (idx = 0u; idx < 8u; idx++)
    {
        uint32_t div = 2u << idx;
        uint32_t sck = pclk_hz / div;
        uint32_t err = (sck > target_hz) ? (sck - target_hz) : (target_hz - sck);
        if (err < best_err)
        {
            best_err = err;
            best_field = (idx & 7u) << SPI_CR1_BR_Pos;
        }
    }
    return best_field;
}

static void strip_spi_apply_br(SPI_HandleTypeDef *hspi, uint32_t br_field)
{
    __HAL_SPI_DISABLE(hspi);
    MODIFY_REG(hspi->Instance->CR1, SPI_CR1_BR, br_field);
    __HAL_SPI_ENABLE(hspi);
}

static ws2818b_status_t strip_spi_send(strip_t *dev, uint8_t *data, uint32_t len)
{
    SPI_HandleTypeDef *hspi = dev->hspi;

    if (len == 0u || len > 65535u)
    {
        return WS2818B_ERROR_PARAM;
    }

    if (hspi->hdmatx != NULL)
    {
        if (HAL_SPI_Transmit_DMA(hspi, data, (uint16_t)len) != HAL_OK)
        {
            return WS2818B_ERROR_PARAM;
        }
        while (HAL_SPI_GetState(hspi) == HAL_SPI_STATE_BUSY_TX)
        {
        }
        if (HAL_SPI_GetState(hspi) != HAL_SPI_STATE_READY)
        {
            return WS2818B_ERROR_PARAM;
        }
        return WS2818B_OK;
    }

    while (len != 0u)
    {
        uint16_t chunk = (len > 65535u) ? 65535u : (uint16_t)len;
        if (HAL_SPI_Transmit(hspi, data, chunk, HAL_MAX_DELAY) != HAL_OK)
        {
            return WS2818B_ERROR_PARAM;
        }
        data += chunk;
        len -= (uint32_t)chunk;
    }
    return WS2818B_OK;
}

ws2818b_status_t strip_register(strip_t *dev, SPI_HandleTypeDef *hspi,
    uint8_t *grb_buf, uint8_t *spi_tx_buf, uint16_t num_leds, uint32_t spi_pclk_hz)
{
    if (dev == NULL || hspi == NULL || grb_buf == NULL || spi_tx_buf == NULL || num_leds == 0u)
    {
        return WS2818B_ERROR_PARAM;
    }

    dev->hspi = hspi;
    dev->grb_buf = grb_buf;
    dev->spi_tx_buf = spi_tx_buf;
    dev->num_leds = num_leds;
    if (spi_pclk_hz == 0u)
    {
        dev->spi_pclk_hz = HAL_RCC_GetPCLK2Freq();
    }
    else
    {
        dev->spi_pclk_hz = spi_pclk_hz;
    }
    dev->initialized = true;

    (void)memset(grb_buf, 0, (size_t)WS2818B_BUF_LEN(num_leds));
    return WS2818B_OK;
}

ws2818b_status_t strip_attach_ws2818b(strip_t *dev, ws2818b_t *ws, SPI_HandleTypeDef *hspi,
    uint8_t *spi_tx_buf, uint32_t spi_pclk_hz)
{
    if (dev == NULL || ws == NULL || hspi == NULL || spi_tx_buf == NULL)
    {
        return WS2818B_ERROR_PARAM;
    }
    if (!ws2818b_is_initialized(ws))
    {
        return WS2818B_ERROR_NOT_INIT;
    }

    dev->hspi = hspi;
    dev->grb_buf = ws2818b_grb_buf(ws);
    dev->spi_tx_buf = spi_tx_buf;
    dev->num_leds = ws2818b_num_leds(ws);
    if (spi_pclk_hz == 0u)
    {
        dev->spi_pclk_hz = HAL_RCC_GetPCLK2Freq();
    }
    else
    {
        dev->spi_pclk_hz = spi_pclk_hz;
    }

    if (dev->grb_buf == NULL || dev->num_leds == 0u)
    {
        return WS2818B_ERROR_PARAM;
    }

    dev->initialized = true;
    return WS2818B_OK;
}

ws2818b_status_t strip_set_pixel(strip_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *p;
    if (dev == NULL || !dev->initialized || dev->grb_buf == NULL)
    {
        return WS2818B_ERROR_NOT_INIT;
    }
    if (index >= dev->num_leds)
    {
        return WS2818B_ERROR_PARAM;
    }

    p = dev->grb_buf + ((uint32_t)index * 3u);
    p[0] = g;
    p[1] = r;
    p[2] = b;
    return WS2818B_OK;
}

ws2818b_status_t strip_fill(strip_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;
    if (dev == NULL || !dev->initialized)
    {
        return WS2818B_ERROR_NOT_INIT;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        (void)strip_set_pixel(dev, i, r, g, b);
    }
    return WS2818B_OK;
}

ws2818b_status_t strip_show_spi(strip_t *dev)
{
    uint32_t br_field;
    uint32_t br_saved;
    uint32_t payload;
    ws2818b_status_t st;

    if (dev == NULL || !dev->initialized)
    {
        return WS2818B_ERROR_NOT_INIT;
    }
    if (dev->hspi == NULL || dev->grb_buf == NULL || dev->spi_tx_buf == NULL)
    {
        return WS2818B_ERROR_PARAM;
    }

    payload = 9u * (uint32_t)dev->num_leds;

    grb_to_spi_mosi(dev->grb_buf, dev->num_leds, dev->spi_tx_buf);
    (void)memset(dev->spi_tx_buf + payload, 0, (size_t)STRIP_SPI_RESET_PAD_BYTES);

    br_field = strip_pick_br_field(dev->spi_pclk_hz);
    br_saved = dev->hspi->Instance->CR1 & SPI_CR1_BR;

    strip_spi_apply_br(dev->hspi, br_field);
    st = strip_spi_send(dev, dev->spi_tx_buf, payload + STRIP_SPI_RESET_PAD_BYTES);
    strip_spi_apply_br(dev->hspi, br_saved);

    return st;
}

bool strip_is_initialized(const strip_t *dev)
{
    if (dev == NULL)
    {
        return false;
    }
    return dev->initialized;
}

/* -------------------------------------------------------------------------- */
/* Strip scene (4 LEDs), SPI refresh                                          */
/* -------------------------------------------------------------------------- */

typedef struct
{
    strip_scene_id_e id;
    bool running;
    uint8_t current_cycle;
    uint8_t current_action;
    uint8_t action_cycle;
    uint32_t action_time;
    strip_scene_rgb_t current_rgb;
} strip_scene_state_t;

typedef struct
{
    strip_scene_state_t states[STRIP_SCENE_ID_MAX_NUM];
    strip_scene_id_e active_scene;
    bool initialized;
} strip_scene_self_t;

static strip_t *s_strip;
static strip_scene_self_t s_scene;

/* --- MAX9814 / ADC1 -> strip (STRIP_SCENE_ID_MIC_REACTIVE) ---------------- */

static uint32_t s_mic_baseline_u32;
static uint32_t s_mic_peak_u32;
static uint16_t s_mic_full_scale = 1200u;
/** 包络超过该值视为「开始说话」；低于 @ref s_mic_gate_release 则退回静音。 */
static uint16_t s_mic_gate_open = 430u;
static uint16_t s_mic_gate_release = 280u;
static bool s_mic_speech_latched;
static uint16_t s_mic_fill_smooth;

/** 本帧 |raw−baseline|，仅在 MIC 场景运行时在 @ref strip_scene_update 中更新。 */
static uint32_t s_mic_snap_mag;

static void strip_mic_frame_prepare(void);
static bool strip_scene_mic_is_active(void);
static void strip_scene_solid_apply(void);
static void strip_scene_breath_apply(void);
static void strip_scene_chase_apply(void);

/* Factory display mode 1..4 (see strip_scene_factory_display_*). */
static uint8_t s_factory_display_mode = STRIP_FACTORY_MODE_MIC;

/** 常亮：柔和黄色暖调（约 25% 满亮度）。 */
#define STRIP_SOLID_R  72u
#define STRIP_SOLID_G  58u
#define STRIP_SOLID_B  10u

/** 呼吸/流水：每 N 个 @c STRIP_SCENE_TICK_MS 帧才推进一帧动画（50ms 基准）。 */
#define STRIP_BREATH_ANIM_DIV   4u   /* 200ms/步，完整呼吸约 5~6s */
#define STRIP_CHASE_ANIM_DIV    3u   /* 150ms/步 */

#define STRIP_BREATH_HUE_STEP   1u
#define STRIP_BREATH_VAL_STEP   3u
#define STRIP_BREATH_VAL_MIN    18u
#define STRIP_BREATH_VAL_MAX    130u
#define STRIP_BREATH_SAT        220u

#define STRIP_CHASE_OFFSET_STEP 5u

static uint8_t s_breath_hue;
static uint8_t s_breath_val;
static int8_t s_breath_val_dir = 1;
static uint8_t s_breath_anim_div;
static uint8_t s_chase_offset;
static uint8_t s_chase_anim_div;

static uint32_t mic_u32_min(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static void strip_mic_frame_prepare(void)
{
    uint16_t raw;
    int32_t ac;
    uint32_t mag;

    raw = adc_voltage_audio_raw();
    if (s_mic_baseline_u32 == 0u)
    {
        s_mic_baseline_u32 = (uint32_t)raw;
    }
    else
    {
        s_mic_baseline_u32 = (s_mic_baseline_u32 * 63u + (uint32_t)raw) >> 6u;
    }

    ac = (int32_t)raw - (int32_t)s_mic_baseline_u32;
    if (ac < 0)
    {
        mag = (uint32_t)(-ac);
    }
    else
    {
        mag = (uint32_t)ac;
    }
    s_mic_snap_mag = mag;
}

/** h,s,v ∈ [0,255]，紧凑 6 段彩虹（无浮点）。 */
static void mic_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *pr, uint8_t *pg, uint8_t *pb)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t region;
    uint32_t rem;
    uint32_t p;
    uint32_t q;
    uint32_t t;

    if (s == 0u)
    {
        *pr = v;
        *pg = v;
        *pb = v;
        return;
    }

    region = (uint8_t)(((uint32_t)h * 6u) >> 8);
    rem = ((uint32_t)h * 6u) & 0xFFu;
    p = ((uint32_t)v * (255u - (uint32_t)s)) / 255u;
    q = ((uint32_t)v * (255u - (((uint32_t)s * rem) >> 8))) / 255u;
    t = ((uint32_t)v * (255u - (((uint32_t)s * (255u - rem)) >> 8))) / 255u;

    switch (region)
    {
        default:
        case 0u:
            r = v;
            g = (uint8_t)t;
            b = (uint8_t)p;
            break;
        case 1u:
            r = (uint8_t)q;
            g = v;
            b = (uint8_t)p;
            break;
        case 2u:
            r = (uint8_t)p;
            g = v;
            b = (uint8_t)t;
            break;
        case 3u:
            r = (uint8_t)p;
            g = (uint8_t)q;
            b = v;
            break;
        case 4u:
            r = (uint8_t)t;
            g = (uint8_t)p;
            b = v;
            break;
        case 5u:
            r = v;
            g = (uint8_t)p;
            b = (uint8_t)q;
            break;
    }

    *pr = r;
    *pg = g;
    *pb = b;
}

void strip_scene_mic_reset(void)
{
    s_mic_baseline_u32 = 0u;
    s_mic_peak_u32 = 0u;
    s_mic_speech_latched = false;
    s_mic_fill_smooth = 0u;
}

uint8_t strip_scene_mic_level_percent(void)
{
    if (!strip_scene_mic_is_active())
    {
        return 0u;
    }

    return (uint8_t)(((uint32_t)s_mic_fill_smooth * 100u) / 1020u);
}

static bool strip_scene_mic_is_active(void)
{
    return s_scene.initialized &&
           s_scene.states[STRIP_SCENE_ID_MIC_REACTIVE].running;
}

static void strip_scene_mic_apply(void)
{
    uint32_t mag;
    uint32_t pk;
    uint32_t span;
    uint32_t fill_target;
    uint16_t i;

    /* 静音：每颗灯独立暗色，不跟 ADC 抖动绑定 */
    static const uint8_t k_idle_r[4] = {22u, 10u, 18u, 14u};
    static const uint8_t k_idle_g[4] = {8u, 20u, 14u, 16u};
    static const uint8_t k_idle_b[4] = {26u, 22u, 10u, 24u};

    if (s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    mag = s_mic_snap_mag;

    if (mag > s_mic_peak_u32)
    {
        s_mic_peak_u32 = mag;
    }
    else
    {
        s_mic_peak_u32 = (s_mic_peak_u32 * 250u) >> 8u;
    }

    pk = s_mic_peak_u32;
    if (s_mic_speech_latched)
    {
        if (pk < (uint32_t)s_mic_gate_release)
        {
            s_mic_speech_latched = false;
        }
    }
    else
    {
        if (pk >= (uint32_t)s_mic_gate_open)
        {
            s_mic_speech_latched = true;
        }
    }

    if (!s_mic_speech_latched)
    {
        span = 0u;
    }
    else if (pk > (uint32_t)s_mic_gate_open)
    {
        span = pk - (uint32_t)s_mic_gate_open;
    }
    else
    {
        span = 0u;
    }

    span = mic_u32_min(span, (uint32_t)s_mic_full_scale);

    fill_target = (span * 1020u) / (uint32_t)s_mic_full_scale;
    if (fill_target > 1020u)
    {
        fill_target = 1020u;
    }

    if (!s_mic_speech_latched)
    {
        /* 快速回到「稳定 idle」避免在门限附近拖尾 */
        s_mic_fill_smooth = (uint16_t)(((uint32_t)s_mic_fill_smooth * 5u) >> 3);
        if (s_mic_fill_smooth < 16u)
        {
            s_mic_fill_smooth = 0u;
        }
    }
    else if (fill_target > (uint32_t)s_mic_fill_smooth)
    {
        uint32_t up = fill_target - (uint32_t)s_mic_fill_smooth;
        uint32_t step = (up * 3u) >> 2;
        if (step == 0u)
        {
            step = 1u;
        }
        s_mic_fill_smooth = (uint16_t)mic_u32_min(1020u, (uint32_t)s_mic_fill_smooth + step);
    }
    else
    {
        s_mic_fill_smooth = (uint16_t)(((uint32_t)s_mic_fill_smooth * 13u) >> 4);
        if (s_mic_fill_smooth < 16u)
        {
            s_mic_fill_smooth = 0u;
        }
    }

    if (s_mic_fill_smooth == 0u)
    {
        for (i = 0u; i < (uint16_t)STRIP_SCENE_LED_NUM; i++)
        {
            (void)strip_set_pixel(s_strip, i, k_idle_r[i], k_idle_g[i], k_idle_b[i]);
        }
    }
    else
    {
        uint32_t fill = (uint32_t)s_mic_fill_smooth;
        for (i = 0u; i < (uint16_t)STRIP_SCENE_LED_NUM; i++)
        {
            uint32_t base = (uint32_t)i * 255u;
            uint32_t lv;
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t hue;
            uint8_t sat;
            uint8_t val;

            if (fill <= base)
            {
                lv = 0u;
            }
            else if ((fill - base) >= 255u)
            {
                lv = 255u;
            }
            else
            {
                lv = fill - base;
            }

            /* 每颗灯色相错开 + 随整体电平缓慢旋转，饱和度高 */
            hue = (uint8_t)((uint32_t)i * 48u + (fill >> 2) + (span >> 1));
            sat = 255u;
            val = (uint8_t)mic_u32_min(255u, 28u + (lv * 230u) / 255u);
            mic_hsv_to_rgb(hue, sat, val, &r, &g, &b);
            (void)strip_set_pixel(s_strip, i, r, g, b);
        }
    }

    (void)strip_show_spi(s_strip);
}

static void strip_scene_solid_apply(void)
{
    uint16_t i;

    if (s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    for (i = 0u; i < (uint16_t)STRIP_SCENE_LED_NUM; i++)
    {
        (void)strip_set_pixel(s_strip, i, STRIP_SOLID_R, STRIP_SOLID_G, STRIP_SOLID_B);
    }
    (void)strip_show_spi(s_strip);
}

static void strip_scene_breath_apply(void)
{
    uint16_t i;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    s_breath_anim_div++;
    if (s_breath_anim_div >= STRIP_BREATH_ANIM_DIV)
    {
        s_breath_anim_div = 0u;
        s_breath_hue = (uint8_t)(s_breath_hue + STRIP_BREATH_HUE_STEP);
        if (s_breath_val_dir > 0)
        {
            if (s_breath_val >= STRIP_BREATH_VAL_MAX)
            {
                s_breath_val_dir = -1;
            }
            else
            {
                uint16_t n = (uint16_t)s_breath_val + STRIP_BREATH_VAL_STEP;
                s_breath_val = (n >= STRIP_BREATH_VAL_MAX) ? STRIP_BREATH_VAL_MAX : (uint8_t)n;
            }
        }
        else
        {
            if (s_breath_val <= STRIP_BREATH_VAL_MIN)
            {
                s_breath_val_dir = 1;
            }
            else
            {
                s_breath_val = (uint8_t)(s_breath_val - STRIP_BREATH_VAL_STEP);
            }
        }
    }

    mic_hsv_to_rgb(s_breath_hue, STRIP_BREATH_SAT, s_breath_val, &r, &g, &b);
    for (i = 0u; i < (uint16_t)STRIP_SCENE_LED_NUM; i++)
    {
        (void)strip_set_pixel(s_strip, i, r, g, b);
    }
    (void)strip_show_spi(s_strip);
}

static void strip_scene_chase_apply(void)
{
    uint16_t i;

    if (s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    s_chase_anim_div++;
    if (s_chase_anim_div >= STRIP_CHASE_ANIM_DIV)
    {
        s_chase_anim_div = 0u;
        s_chase_offset = (uint8_t)(s_chase_offset + STRIP_CHASE_OFFSET_STEP);
    }

    for (i = 0u; i < (uint16_t)STRIP_SCENE_LED_NUM; i++)
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t hue = (uint8_t)(s_chase_offset + (uint8_t)(i * 64u));
        uint8_t val = (uint8_t)(36u + (((uint32_t)i + 1u) * 22u));

        mic_hsv_to_rgb(hue, STRIP_BREATH_SAT, val, &r, &g, &b);
        (void)strip_set_pixel(s_strip, i, r, g, b);
    }
    (void)strip_show_spi(s_strip);
}

static const strip_scene_t strip_scene_bootup =
{
    .cycle = 1,
    .num = 1,
    .action[0] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 2000u * STRIP_SCENE_MSEC,
    },
};

static const strip_scene_t strip_scene_pairing =
{
    .cycle = 30,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 1000u * STRIP_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 1000u * STRIP_SCENE_MSEC,
    },
};

static const strip_scene_t strip_scene_trigger =
{
    .cycle = 10,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500u * STRIP_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500u * STRIP_SCENE_MSEC,
    },
};

static const strip_scene_t strip_scene_error =
{
    .cycle = 10,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100u * STRIP_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100u * STRIP_SCENE_MSEC,
    },
};

static const strip_scene_t strip_scene_success =
{
    .cycle = 20,
    .num = 2,
    .action[0] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 500u * STRIP_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1,
        .type = STRIP_ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500u * STRIP_SCENE_MSEC,
    },
};

static const strip_scene_t strip_scene_mic =
{
    .cycle = STRIP_SCENE_CYCLE_ALWAYS,
    .num = 1u,
    .action[0] =
    {
        .cycle = 1u,
        .type = STRIP_ACTION_MIC_STREAM,
        .sub.mic = { .reserved = 0u },
    },
};

static const strip_scene_t strip_scene_solid =
{
    .cycle = STRIP_SCENE_CYCLE_ALWAYS,
    .num = 1u,
    .action[0] =
    {
        .cycle = 1u,
        .type = STRIP_ACTION_RGB_SOLID,
        .sub.mic = { .reserved = 0u },
    },
};

static const strip_scene_t strip_scene_color_breath =
{
    .cycle = STRIP_SCENE_CYCLE_ALWAYS,
    .num = 1u,
    .action[0] =
    {
        .cycle = 1u,
        .type = STRIP_ACTION_RGB_BREATH,
        .sub.mic = { .reserved = 0u },
    },
};

static const strip_scene_t strip_scene_color_chase =
{
    .cycle = STRIP_SCENE_CYCLE_ALWAYS,
    .num = 1u,
    .action[0] =
    {
        .cycle = 1u,
        .type = STRIP_ACTION_RGB_CHASE,
        .sub.mic = { .reserved = 0u },
    },
};

static const strip_scene_id_e s_factory_display_scenes[] =
{
    STRIP_SCENE_ID_SOLID,
    STRIP_SCENE_ID_COLOR_BREATH,
    STRIP_SCENE_ID_COLOR_CHASE,
    STRIP_SCENE_ID_MIC_REACTIVE,
};

static const strip_scene_tab_t s_scene_table[STRIP_SCENE_ID_MAX_NUM] =
{
    [STRIP_SCENE_ID_BOOTUP]  = {.prio = STRIP_SCENE_PRIO_LOW,    .scene = &strip_scene_bootup,},
    [STRIP_SCENE_ID_PAIRING] = {.prio = STRIP_SCENE_PRIO_PAIR,   .scene = &strip_scene_pairing,},
    [STRIP_SCENE_ID_TRIGGER] = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_trigger,},
    [STRIP_SCENE_ID_ERROR]   = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_error,},
    [STRIP_SCENE_ID_SUCCESS] = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_success,},
    [STRIP_SCENE_ID_SOLID]        = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_solid,},
    [STRIP_SCENE_ID_COLOR_BREATH] = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_color_breath,},
    [STRIP_SCENE_ID_COLOR_CHASE]    = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_color_chase,},
    [STRIP_SCENE_ID_MIC_REACTIVE]   = {.prio = STRIP_SCENE_PRIO_NORMAL, .scene = &strip_scene_mic,},
};

static void strip_scene_apply_rgb(const strip_scene_rgb_t *rgb)
{
    if (s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    if (rgb == NULL)
    {
        (void)strip_fill(s_strip, 0, 0, 0);
    }
    else
    {
        (void)strip_fill(s_strip, rgb->r, rgb->g, rgb->b);
    }
    (void)strip_show_spi(s_strip);
}

static bool strip_scene_is_factory_display(strip_scene_id_e id)
{
    return (id == STRIP_SCENE_ID_SOLID) ||
           (id == STRIP_SCENE_ID_COLOR_BREATH) ||
           (id == STRIP_SCENE_ID_COLOR_CHASE) ||
           (id == STRIP_SCENE_ID_MIC_REACTIVE);
}

static strip_scene_id_e strip_scene_factory_mode_to_id(uint8_t mode_1_to_4)
{
    if (mode_1_to_4 < STRIP_FACTORY_MODE_SOLID || mode_1_to_4 > STRIP_FACTORY_MODE_MIC)
    {
        mode_1_to_4 = STRIP_FACTORY_MODE_MIC;
    }
    return s_factory_display_scenes[mode_1_to_4 - 1u];
}

static strip_scene_id_e strip_scene_find_highest_priority(void)
{
    strip_scene_id_e highest_id = STRIP_SCENE_ID_MAX_NUM;
    strip_scene_prio_e highest_prio = STRIP_SCENE_PRIO_MAX_NUM;
    uint8_t i;

    for (i = 0u; i < (uint8_t)STRIP_SCENE_ID_MAX_NUM; i++)
    {
        if (s_scene.states[i].running)
        {
            strip_scene_prio_e prio = s_scene_table[i].prio;
            if (prio < highest_prio)
            {
                highest_prio = prio;
                highest_id = (strip_scene_id_e)i;
            }
        }
    }

    return highest_id;
}

static bool strip_fade_step_channel(uint8_t *cur, uint8_t target, uint8_t step)
{
    if (*cur == target)
    {
        return true;
    }
    if (*cur < target)
    {
        uint16_t n = (uint16_t)*cur + (uint16_t)step;
        *cur = (n >= (uint16_t)target) ? target : (uint8_t)n;
        return (*cur == target);
    }
    if (*cur > target)
    {
        uint16_t d = (uint16_t)*cur - (uint16_t)target;
        if (d <= (uint16_t)step)
        {
            *cur = target;
            return true;
        }
        *cur = (uint8_t)((uint16_t)*cur - (uint16_t)step);
        return (*cur == target);
    }
    return true;
}

void strip_scene_attach(strip_t *strip)
{
    if (strip == NULL)
    {
        s_strip = NULL;
        return;
    }
    if (!strip->initialized)
    {
        s_strip = NULL;
        return;
    }
    if (strip->num_leds != STRIP_SCENE_LED_NUM)
    {
        s_strip = NULL;
        return;
    }
    s_strip = strip;
}

void strip_scene_init(void)
{
    (void)memset(&s_scene, 0, sizeof(s_scene));
    s_scene.active_scene = STRIP_SCENE_ID_MAX_NUM;
    s_scene.initialized = true;
}

void strip_scene_update(void)
{
    strip_scene_state_t *state;
    const strip_scene_t *scene;
    const strip_scene_action_t *action;
    bool action_complete;

    if (!s_scene.initialized || s_strip == NULL || !s_strip->initialized)
    {
        return;
    }

    if (strip_scene_mic_is_active())
    {
        strip_mic_frame_prepare();
    }

    if (s_scene.active_scene >= STRIP_SCENE_ID_MAX_NUM)
    {
        return;
    }

    state = &s_scene.states[s_scene.active_scene];
    scene = s_scene_table[s_scene.active_scene].scene;

    if (!state->running || scene == NULL)
    {
        return;
    }

    state->action_time += STRIP_SCENE_TICK_MS;
    action = &scene->action[state->current_action];
    action_complete = false;

    if (action->type == STRIP_ACTION_MIC_STREAM)
    {
        strip_scene_mic_apply();
        return;
    }
    if (action->type == STRIP_ACTION_RGB_SOLID)
    {
        strip_scene_solid_apply();
        return;
    }
    if (action->type == STRIP_ACTION_RGB_BREATH)
    {
        strip_scene_breath_apply();
        return;
    }
    if (action->type == STRIP_ACTION_RGB_CHASE)
    {
        strip_scene_chase_apply();
        return;
    }

    if (action->type == STRIP_ACTION_ONOFF)
    {
        if (state->action_time >= action->sub.onoff.lifetime)
        {
            action_complete = true;
        }
        else
        {
            strip_scene_apply_rgb(&action->sub.onoff.value);
        }
    }
    else if (action->type == STRIP_ACTION_FADE)
    {
        if (state->action_time >= action->sub.fade.interval)
        {
            bool rd;
            bool gd;
            bool bd;

            rd = strip_fade_step_channel(&state->current_rgb.r,
                action->sub.fade.end_value.r, action->sub.fade.step);
            gd = strip_fade_step_channel(&state->current_rgb.g,
                action->sub.fade.end_value.g, action->sub.fade.step);
            bd = strip_fade_step_channel(&state->current_rgb.b,
                action->sub.fade.end_value.b, action->sub.fade.step);

            strip_scene_apply_rgb(&state->current_rgb);
            state->action_time = 0u;

            if (rd && gd && bd)
            {
                action_complete = true;
            }
        }
    }

    if (action_complete)
    {
        state->action_cycle++;
        if (state->action_cycle >= action->cycle)
        {
            state->action_cycle = 0u;
            state->current_action++;
            if (state->current_action >= scene->num)
            {
                state->current_action = 0u;
                state->current_cycle++;
                if (scene->cycle != STRIP_SCENE_CYCLE_ALWAYS &&
                    (uint16_t)state->current_cycle >= scene->cycle)
                {
                    state->running = false;
                    strip_scene_apply_rgb(NULL);
                    s_scene.active_scene = strip_scene_find_highest_priority();
                    if (s_scene.active_scene >= STRIP_SCENE_ID_MAX_NUM)
                    {
                        return;
                    }
                    state = &s_scene.states[s_scene.active_scene];
                    scene = s_scene_table[s_scene.active_scene].scene;
                }
            }
        }
        state->action_time = 0u;
        if (state->current_action < scene->num)
        {
            const strip_scene_action_t *cur_act = &scene->action[state->current_action];
            if (cur_act->type == STRIP_ACTION_FADE)
            {
                state->current_rgb = cur_act->sub.fade.start_value;
            }
        }
    }
}

void strip_scene_run(strip_scene_id_e id)
{
    strip_scene_state_t *state;
    const strip_scene_t *scene;
    strip_scene_id_e new_scene;

    if (id >= STRIP_SCENE_ID_MAX_NUM)
    {
        return;
    }
    if (!s_scene.initialized || s_strip == NULL)
    {
        return;
    }

    state = &s_scene.states[id];
    scene = s_scene_table[id].scene;
    if (scene == NULL)
    {
        return;
    }
    if (state->running)
    {
        return;
    }

    state->running = true;
    state->current_cycle = 0u;
    state->current_action = 0u;
    state->action_cycle = 0u;
    state->action_time = 0u;
    state->current_rgb.r = 0u;
    state->current_rgb.g = 0u;
    state->current_rgb.b = 0u;

    if (id == STRIP_SCENE_ID_MIC_REACTIVE)
    {
        strip_scene_mic_reset();
    }
    if (id == STRIP_SCENE_ID_COLOR_BREATH)
    {
        s_breath_hue = 0u;
        s_breath_val = STRIP_BREATH_VAL_MIN;
        s_breath_val_dir = 1;
        s_breath_anim_div = 0u;
    }
    if (id == STRIP_SCENE_ID_COLOR_CHASE)
    {
        s_chase_offset = 0u;
        s_chase_anim_div = 0u;
    }

    new_scene = strip_scene_find_highest_priority();
    if (new_scene != s_scene.active_scene)
    {
        if (s_scene.active_scene < STRIP_SCENE_ID_MAX_NUM)
        {
            strip_scene_id_e old_id = s_scene.active_scene;
            if (!strip_scene_is_factory_display(old_id))
            {
                s_scene.states[old_id].running = false;
            }
        }
        s_scene.active_scene = new_scene;
    }

    if (s_scene.active_scene < STRIP_SCENE_ID_MAX_NUM)
    {
        strip_scene_state_t *active_state = &s_scene.states[s_scene.active_scene];
        active_state->current_cycle = 0u;
        active_state->current_action = 0u;
        active_state->action_cycle = 0u;
        active_state->action_time = 0u;
        active_state->current_rgb.r = 0u;
        active_state->current_rgb.g = 0u;
        active_state->current_rgb.b = 0u;
        {
            const strip_scene_action_t *a = &s_scene_table[s_scene.active_scene].scene->action[0];
            if (a->type == STRIP_ACTION_FADE)
            {
                active_state->current_rgb = a->sub.fade.start_value;
            }
        }
    }
}

void strip_scene_factory_display_set(uint8_t mode_1_to_4)
{
    strip_scene_id_e target;
    uint8_t i;

    if (mode_1_to_4 < STRIP_FACTORY_MODE_SOLID || mode_1_to_4 > STRIP_FACTORY_MODE_MIC)
    {
        mode_1_to_4 = STRIP_FACTORY_MODE_MIC;
    }
    s_factory_display_mode = mode_1_to_4;
    target = strip_scene_factory_mode_to_id(mode_1_to_4);

    for (i = 0u; i < (uint8_t)(sizeof(s_factory_display_scenes) / sizeof(s_factory_display_scenes[0])); i++)
    {
        strip_scene_id_e sid = s_factory_display_scenes[i];
        if (sid != target)
        {
            strip_scene_cancel(sid);
        }
    }

    if (!s_scene.states[target].running)
    {
        strip_scene_run(target);
    }

    /* 显式切换展示模式时强制激活，避免 TRIGGER 等同优先级场景占住 active_scene。 */
    if (s_scene.states[target].running)
    {
        strip_scene_state_t *active = &s_scene.states[target];

        s_scene.active_scene = target;
        active->current_cycle = 0u;
        active->current_action = 0u;
        active->action_cycle = 0u;
        active->action_time = 0u;
        active->current_rgb.r = 0u;
        active->current_rgb.g = 0u;
        active->current_rgb.b = 0u;
    }
}

uint8_t strip_scene_factory_display_get(void)
{
    return s_factory_display_mode;
}

void strip_scene_factory_display_next(void)
{
    uint8_t next = s_factory_display_mode + 1u;
    if (next > STRIP_FACTORY_MODE_MIC)
    {
        next = STRIP_FACTORY_MODE_SOLID;
    }
    strip_scene_factory_display_set(next);
}

void strip_scene_cancel(strip_scene_id_e id)
{
    if (id >= STRIP_SCENE_ID_MAX_NUM)
    {
        return;
    }
    if (!s_scene.initialized)
    {
        return;
    }

    s_scene.states[id].running = false;

    if (id == STRIP_SCENE_ID_MIC_REACTIVE)
    {
        strip_scene_mic_reset();
    }
    if (id == STRIP_SCENE_ID_COLOR_BREATH)
    {
        s_breath_hue = 0u;
        s_breath_val = 0u;
        s_breath_val_dir = 1;
        s_breath_anim_div = 0u;
    }
    if (id == STRIP_SCENE_ID_COLOR_CHASE)
    {
        s_chase_offset = 0u;
        s_chase_anim_div = 0u;
    }

    if (s_scene.active_scene == id)
    {
        strip_scene_apply_rgb(NULL);
        {
            strip_scene_id_e new_scene = strip_scene_find_highest_priority();
            if (new_scene < STRIP_SCENE_ID_MAX_NUM)
            {
                strip_scene_state_t *active_state = &s_scene.states[new_scene];
                s_scene.active_scene = new_scene;
                active_state->current_cycle = 0u;
                active_state->current_action = 0u;
                active_state->action_cycle = 0u;
                active_state->action_time = 0u;
                active_state->current_rgb.r = 0u;
                active_state->current_rgb.g = 0u;
                active_state->current_rgb.b = 0u;
                {
                    const strip_scene_action_t *a = &s_scene_table[new_scene].scene->action[0];
                    if (a->type == STRIP_ACTION_FADE)
                    {
                        active_state->current_rgb = a->sub.fade.start_value;
                    }
                }
            }
            else
            {
                s_scene.active_scene = STRIP_SCENE_ID_MAX_NUM;
            }
        }
    }
}
