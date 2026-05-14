/**
 * @file    mcp4651.c
 * @brief   MCP4651 I2C 数字电位器驱动实现（命令格式参考 DS22096B / Linux mcp4531）
 */

#include "mcp4651.h"
#include "string.h"

#define MCP4651_CMD_SHIFT_WIPER 4u

static uint8_t mcp4651_mem_wiper(uint8_t wiper_index)
{
    if (wiper_index == 0u)
    {
        return MCP4651_MEM_WIPER0;
    }
    return MCP4651_MEM_WIPER1;
}

uint8_t mcp4651_command_byte(uint8_t mem_addr4, uint8_t cmd_c1c0, uint8_t d9_d8)
{
    return (uint8_t)(((mem_addr4 & 0x0Fu) << 4) | (((cmd_c1c0) & 0x03u) << 2) | ((d9_d8) & 0x03u));
}

mcp4651_status_t mcp4651_init(mcp4651_t *dev, const mcp4651_config_t *cfg)
{
    if (dev == NULL || cfg == NULL || cfg->write == NULL)
    {
        return MCP4651_ERROR_PARAM;
    }

    dev->write         = cfg->write;
    dev->write_read    = cfg->write_read;
    dev->i2c_address_hal = cfg->i2c_address_hal;
    dev->initialized   = true;
    return MCP4651_OK;
}

mcp4651_status_t mcp4651_write_volatile_u9(mcp4651_t *dev, uint8_t mem_addr4, uint16_t value_u9)
{
    if (dev == NULL || !dev->initialized || dev->write == NULL)
    {
        return MCP4651_ERROR_NOT_INIT;
    }
    if ((value_u9 & ~(uint16_t)MCP4651_WIPER_MASK) != 0u)
    {
        return MCP4651_ERROR_RANGE;
    }

    uint8_t buf[2];
    buf[0] = mcp4651_command_byte(mem_addr4, MCP4651_CMD_WRITE, (uint8_t)(value_u9 >> 8));
    buf[1] = (uint8_t)(value_u9 & 0xFFu);

    if (dev->write(dev->i2c_address_hal, buf, 2u) != 0)
    {
        return MCP4651_ERROR_I2C;
    }
    return MCP4651_OK;
}

mcp4651_status_t mcp4651_read_volatile_u9(mcp4651_t *dev, uint8_t mem_addr4, uint16_t *value_u9)
{
    if (dev == NULL || !dev->initialized || value_u9 == NULL)
    {
        return MCP4651_ERROR_PARAM;
    }
    if (dev->write_read == NULL)
    {
        return MCP4651_ERROR_READ_NOT_SUPPORTED;
    }

    uint8_t cmd = mcp4651_command_byte(mem_addr4, MCP4651_CMD_READ, 0u);
    uint8_t rx[2];

    if (dev->write_read(dev->i2c_address_hal, &cmd, 1u, rx, 2u) != 0)
    {
        return MCP4651_ERROR_I2C;
    }

    /* 与 i2c_smbus_read_word_swapped 一致的字节序（见 Linux mcp4531_read_raw） */
    *value_u9 = (uint16_t)(((uint16_t)rx[1] << 8) | rx[0]);
    *value_u9 &= MCP4651_WIPER_MASK;
    return MCP4651_OK;
}

mcp4651_status_t mcp4651_wiper_set(mcp4651_t *dev, uint8_t wiper_index, uint16_t value_u9)
{
    if (wiper_index > 1u)
    {
        return MCP4651_ERROR_PARAM;
    }
    if (value_u9 > MCP4651_WIPER_MAX)
    {
        return MCP4651_ERROR_RANGE;
    }
    return mcp4651_write_volatile_u9(dev, mcp4651_mem_wiper(wiper_index), value_u9);
}

mcp4651_status_t mcp4651_wiper_get(mcp4651_t *dev, uint8_t wiper_index, uint16_t *value_u9)
{
    if (wiper_index > 1u)
    {
        return MCP4651_ERROR_PARAM;
    }
    return mcp4651_read_volatile_u9(dev, mcp4651_mem_wiper(wiper_index), value_u9);
}

static mcp4651_status_t mcp4651_send_cmd_only(mcp4651_t *dev, uint8_t cmd_byte)
{
    if (dev == NULL || !dev->initialized || dev->write == NULL)
    {
        return MCP4651_ERROR_NOT_INIT;
    }
    if (dev->write(dev->i2c_address_hal, &cmd_byte, 1u) != 0)
    {
        return MCP4651_ERROR_I2C;
    }
    return MCP4651_OK;
}

mcp4651_status_t mcp4651_wiper_increment(mcp4651_t *dev, uint8_t wiper_index)
{
    if (wiper_index > 1u)
    {
        return MCP4651_ERROR_PARAM;
    }
    uint8_t mem = (uint8_t)(mcp4651_mem_wiper(wiper_index) << MCP4651_CMD_SHIFT_WIPER);
    uint8_t cmd = (uint8_t)(mem | (MCP4651_CMD_INCR << 2));
    return mcp4651_send_cmd_only(dev, cmd);
}

mcp4651_status_t mcp4651_wiper_decrement(mcp4651_t *dev, uint8_t wiper_index)
{
    if (wiper_index > 1u)
    {
        return MCP4651_ERROR_PARAM;
    }
    uint8_t mem = (uint8_t)(mcp4651_mem_wiper(wiper_index) << MCP4651_CMD_SHIFT_WIPER);
    uint8_t cmd = (uint8_t)(mem | (MCP4651_CMD_DECR << 2));
    return mcp4651_send_cmd_only(dev, cmd);
}

mcp4651_status_t mcp4651_tcon_set(mcp4651_t *dev, uint16_t tcon_u9)
{
    return mcp4651_write_volatile_u9(dev, MCP4651_MEM_TCON, tcon_u9);
}

mcp4651_status_t mcp4651_tcon_get(mcp4651_t *dev, uint16_t *tcon_u9)
{
    return mcp4651_read_volatile_u9(dev, MCP4651_MEM_TCON, tcon_u9);
}
