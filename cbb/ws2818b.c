/**
 * @file    ws2818b.c
 * @brief   WS2818B single-wire GRB driver (bit-bang).
 */

#include "ws2818b.h"
#include <stddef.h>

static void spin_cycles(uint32_t n)
{
    volatile uint32_t c = n;
    while (c != 0u)
    {
        c--;
    }
}

static void delay_reset(ws2818b_t *dev)
{
    const ws2818b_hw_t *hw = &dev->hw;
    uint32_t us = (uint32_t)hw->reset_us;
    if (us < 50u)
    {
        us = 280u;
    }

    hw->din_set(0);

    if (hw->delay_us != NULL)
    {
        hw->delay_us(us);
        return;
    }

    if (hw->cpu_hz == 0u)
    {
        return;
    }

    while (us != 0u)
    {
        volatile uint32_t inner = hw->cpu_hz / 1000000u;
        if (inner < 8u)
        {
            inner = 8u;
        }
        while (inner != 0u)
        {
            inner--;
        }
        us--;
    }
}

static void emit_bit(ws2818b_t *dev, int bit)
{
    const ws2818b_hw_t *hw = &dev->hw;

    hw->din_set(1);
    if (bit)
    {
        spin_cycles((uint32_t)hw->t1h_cycles);
    }
    else
    {
        spin_cycles((uint32_t)hw->t0h_cycles);
    }
    hw->din_set(0);
    if (bit)
    {
        spin_cycles((uint32_t)hw->t1l_cycles);
    }
    else
    {
        spin_cycles((uint32_t)hw->t0l_cycles);
    }
}

static void emit_byte(ws2818b_t *dev, uint8_t v)
{
    uint8_t m;
    for (m = 0x80u; m != 0u; m >>= 1u)
    {
        emit_bit(dev, (v & m) != 0u ? 1 : 0);
    }
}

ws2818b_status_t ws2818b_register(ws2818b_t *dev, const ws2818b_hw_t *hw,
    uint8_t *grb_buf, uint16_t num_leds)
{
    if (dev == NULL || hw == NULL || grb_buf == NULL)
    {
        return WS2818B_ERROR_PARAM;
    }
    if (hw->din_set == NULL || num_leds == 0u)
    {
        return WS2818B_ERROR_PARAM;
    }

    dev->hw = *hw;
    if (dev->hw.reset_us < 50u)
    {
        dev->hw.reset_us = 280u;
    }
    dev->grb_buf = grb_buf;
    dev->num_leds = num_leds;

    {
        uint32_t nb = (uint32_t)num_leds * 3u;
        uint32_t i;
        for (i = 0u; i < nb; i++)
        {
            grb_buf[i] = 0u;
        }
    }

    dev->initialized = true;
    return WS2818B_OK;
}

void ws2818b_hw_apply_defaults_72mhz(ws2818b_hw_t *hw)
{
    if (hw == NULL)
    {
        return;
    }
    hw->t0h_cycles = 22u;
    hw->t0l_cycles = 48u;
    hw->t1h_cycles = 48u;
    hw->t1l_cycles = 22u;
    if (hw->reset_us < 50u)
    {
        hw->reset_us = 280u;
    }
}

ws2818b_status_t ws2818b_set_pixel(ws2818b_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (dev == NULL || !dev->initialized)
    {
        return WS2818B_ERROR_NOT_INIT;
    }
    if (index >= dev->num_leds)
    {
        return WS2818B_ERROR_PARAM;
    }

    {
        uint8_t *p = dev->grb_buf + ((uint32_t)index * 3u);
        p[0] = g;
        p[1] = r;
        p[2] = b;
    }
    return WS2818B_OK;
}

ws2818b_status_t ws2818b_fill(ws2818b_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;
    if (dev == NULL || !dev->initialized)
    {
        return WS2818B_ERROR_NOT_INIT;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        (void)ws2818b_set_pixel(dev, i, r, g, b);
    }
    return WS2818B_OK;
}

ws2818b_status_t ws2818b_show(ws2818b_t *dev)
{
    uint32_t bi;
    if (dev == NULL || !dev->initialized)
    {
        return WS2818B_ERROR_NOT_INIT;
    }
    if (dev->grb_buf == NULL)
    {
        return WS2818B_ERROR_PARAM;
    }

    for (bi = 0u; bi < (uint32_t)dev->num_leds * 3u; bi++)
    {
        emit_byte(dev, dev->grb_buf[bi]);
    }

    delay_reset(dev);
    return WS2818B_OK;
}

bool ws2818b_is_initialized(const ws2818b_t *dev)
{
    if (dev == NULL)
    {
        return false;
    }
    return dev->initialized;
}

uint16_t ws2818b_num_leds(const ws2818b_t *dev)
{
    if (dev == NULL)
    {
        return 0u;
    }
    return dev->num_leds;
}

uint8_t *ws2818b_grb_buf(ws2818b_t *dev)
{
    if (dev == NULL)
    {
        return NULL;
    }
    return dev->grb_buf;
}
