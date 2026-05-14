/**
 * @file    ili9341.c
 * @brief   ILI9341 driver - device context and registered ops only, no hardware includes.
 */

#include "ili9341.h"
#include <stddef.h>
#include <string.h>

#define ILI9341_CMD_SWRESET    0x01
#define ILI9341_CMD_SLPOUT     0x11
#define ILI9341_CMD_DISPON     0x29
#define ILI9341_CMD_CASET      0x2A
#define ILI9341_CMD_RASET      0x2B
#define ILI9341_CMD_RAMWR      0x2C
#define ILI9341_CMD_MADCTL     0x36
#define ILI9341_CMD_PIXFMT     0x3A
#define ILI9341_CMD_PWCTRA     0xCF
#define ILI9341_CMD_PWCTRB     0xED
#define ILI9341_CMD_PWSEQ      0xE8
#define ILI9341_CMD_PWCTR1     0xC0
#define ILI9341_CMD_PWCTR2     0xC1
#define ILI9341_CMD_VMCTR1     0xC5
#define ILI9341_CMD_FRMCTR1    0xB1
#define ILI9341_CMD_DFUNCTR    0xB6
#define ILI9341_CMD_ENABLE3G   0xF2
#define ILI9341_CMD_GAMMASET   0x26
#define ILI9341_CMD_GAMMAP     0xE0
#define ILI9341_CMD_GAMMAN     0xE1

#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_BGR 0x08

#define TX_CHUNK  1024u

static void dc_cmd(ili9341_t *dev)  { if (dev->set_dc) dev->set_dc(0); }
static void dc_data(ili9341_t *dev) { if (dev->set_dc) dev->set_dc(1); }
static void cs_low(ili9341_t *dev)  { if (dev->set_cs) dev->set_cs(0); }
static void cs_high(ili9341_t *dev) { if (dev->set_cs) dev->set_cs(1); }
static void rst_high(ili9341_t *dev) { if (dev->set_rst) dev->set_rst(1); }
static void rst_low(ili9341_t *dev)  { if (dev->set_rst) dev->set_rst(0); }
static void delay_ms(ili9341_t *dev, uint32_t ms) { if (dev->delay_ms) dev->delay_ms(ms); }

static void write_cmd(ili9341_t *dev, uint8_t cmd)
{
    dc_cmd(dev);
    cs_low(dev);
    if (dev->spi_tx) dev->spi_tx(&cmd, 1);
    cs_high(dev);
}

static void write_data(ili9341_t *dev, uint8_t data)
{
    dc_data(dev);
    cs_low(dev);
    if (dev->spi_tx) dev->spi_tx(&data, 1);
    cs_high(dev);
}

static void write_data_buf(ili9341_t *dev, const uint8_t *buf, uint16_t len)
{
    if (len == 0 || !dev->spi_tx) return;
    dc_data(dev);
    cs_low(dev);
    dev->spi_tx(buf, len);
    cs_high(dev);
}

ili9341_status_t ili9341_register(ili9341_t *dev, const ili9341_config_t *cfg)
{
    if (dev == NULL || cfg == NULL)
        return ILI9341_ERROR_PARAM;
    if (cfg->spi_tx == NULL)
        return ILI9341_ERROR_PARAM;

    dev->spi_tx = cfg->spi_tx;
    dev->set_cs = cfg->set_cs;
    dev->set_dc = cfg->set_dc;
    dev->set_rst = cfg->set_rst;
    dev->set_bl = cfg->set_bl;
    dev->delay_ms = cfg->delay_ms;
    dev->initialized = false;

    cs_high(dev);
    dc_data(dev);
    rst_high(dev);
    delay_ms(dev, 10);
    rst_low(dev);
    delay_ms(dev, 50);
    rst_high(dev);
    delay_ms(dev, 120);

    write_cmd(dev, ILI9341_CMD_SWRESET);
    delay_ms(dev, 150);

    write_cmd(dev, ILI9341_CMD_PWCTRA);
    write_data(dev, 0x00);
    write_data(dev, 0xC1);
    write_data(dev, 0x30);
    write_cmd(dev, ILI9341_CMD_PWCTRB);
    write_data(dev, 0x64);
    write_data(dev, 0x03);
    write_data(dev, 0x12);
    write_data(dev, 0x81);
    write_cmd(dev, ILI9341_CMD_PWSEQ);
    write_data(dev, 0x85);
    write_data(dev, 0x78);
    write_data(dev, 0x0C);
    write_cmd(dev, ILI9341_CMD_PWCTR1);
    write_data(dev, 0x23);
    write_cmd(dev, ILI9341_CMD_PWCTR2);
    write_data(dev, 0x10);
    write_cmd(dev, ILI9341_CMD_VMCTR1);
    write_data(dev, 0x3E);
    write_data(dev, 0x28);
    write_cmd(dev, ILI9341_CMD_PIXFMT);
    write_data(dev, 0x55);
    write_cmd(dev, ILI9341_CMD_FRMCTR1);
    write_data(dev, 0x00);
    write_data(dev, 0x18);
    write_cmd(dev, ILI9341_CMD_DFUNCTR);
    write_data(dev, 0x08);
    write_data(dev, 0x82);
    write_data(dev, 0x27);
    write_cmd(dev, ILI9341_CMD_ENABLE3G);
    write_data(dev, 0x00);
    write_cmd(dev, ILI9341_CMD_GAMMASET);
    write_data(dev, 0x01);
    write_cmd(dev, ILI9341_CMD_GAMMAP);
    write_data(dev, 0x0F);
    write_data(dev, 0x31);
    write_data(dev, 0x2B);
    write_data(dev, 0x0C);
    write_data(dev, 0x0E);
    write_data(dev, 0x08);
    write_data(dev, 0x4E);
    write_data(dev, 0xF1);
    write_data(dev, 0x37);
    write_data(dev, 0x07);
    write_data(dev, 0x10);
    write_data(dev, 0x03);
    write_data(dev, 0x0E);
    write_data(dev, 0x09);
    write_data(dev, 0x00);
    write_cmd(dev, ILI9341_CMD_GAMMAN);
    write_data(dev, 0x00);
    write_data(dev, 0x0E);
    write_data(dev, 0x14);
    write_data(dev, 0x03);
    write_data(dev, 0x11);
    write_data(dev, 0x07);
    write_data(dev, 0x31);
    write_data(dev, 0xC1);
    write_data(dev, 0x48);
    write_data(dev, 0x08);
    write_data(dev, 0x0F);
    write_data(dev, 0x0C);
    write_data(dev, 0x31);
    write_data(dev, 0x36);
    write_data(dev, 0x0F);

    write_cmd(dev, ILI9341_CMD_MADCTL);
    write_data(dev, MADCTL_MX | MADCTL_BGR);

    write_cmd(dev, ILI9341_CMD_SLPOUT);
    delay_ms(dev, 120);
    write_cmd(dev, ILI9341_CMD_DISPON);
    delay_ms(dev, 20);

    ili9341_set_backlight(dev, true);
    dev->initialized = true;
    return ILI9341_OK;
}

ili9341_status_t ili9341_set_backlight(ili9341_t *dev, bool on)
{
    if (dev == NULL)
        return ILI9341_ERROR_PARAM;
    if (dev->set_bl)
        dev->set_bl(on ? 1 : 0);
    return ILI9341_OK;
}

ili9341_status_t ili9341_set_window(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (dev == NULL || !dev->initialized)
        return ILI9341_ERROR_NOT_INIT;

    uint8_t buf[4];
    write_cmd(dev, ILI9341_CMD_CASET);
    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFF);
    write_data_buf(dev, buf, 4);
    write_cmd(dev, ILI9341_CMD_RASET);
    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFF);
    write_data_buf(dev, buf, 4);
    {
        uint8_t ramwr = ILI9341_CMD_RAMWR;
        dc_cmd(dev);
        cs_low(dev);
        if (dev->spi_tx)
            dev->spi_tx(&ramwr, 1);
    }
    return ILI9341_OK;
}

ili9341_status_t ili9341_write_pixels(ili9341_t *dev, const uint16_t *buf, uint32_t len)
{
    if (dev == NULL || !dev->initialized)
        return ILI9341_ERROR_NOT_INIT;
    if (buf == NULL || len == 0 || !dev->spi_tx)
        return ILI9341_ERROR_PARAM;

    dc_data(dev);
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t remain = len * 2u;
    while (remain > 0)
    {
        uint32_t n = remain > TX_CHUNK ? TX_CHUNK : remain;
        dev->spi_tx(p, (uint16_t)n);
        p += n;
        remain -= n;
    }
    return ILI9341_OK;
}

void ili9341_end_write(ili9341_t *dev)
{
    if (dev != NULL && dev->set_cs)
        dev->set_cs(1);
}

bool ili9341_is_initialized(const ili9341_t *dev)
{
    return dev != NULL && dev->initialized;
}
