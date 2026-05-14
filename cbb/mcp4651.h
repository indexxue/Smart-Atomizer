/**
 * @file    mcp4651.h
 * @brief   MCP4651 双通道 8 位 volatile 数字电位器（I2C），与 Microchip DS22096B 命令格式一致。
 *
 * @note    HVC/A0 引脚：既是 I2C 从机地址的 A0 位，也是手册中的 High Voltage Command 输入。
 *          本模块只做总线层读写，不操作 GPIO。请在 CubeMX / gpio.c 中为 HVC/A0（以及 A1、A2 等）
 *          完成引脚模式与默认电平的注册与初始化；若需动态改址或高压命令时序，由上层控制该脚电平。
 *
 * @see     Datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/22096b.pdf
 */

#ifndef MCP4651_H
#define MCP4651_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 与工程内 OLED 等一致：HAL_I2C_* 的 DevAddress 为 7 位地址左移 1 位 */
#define MCP4651_HAL_ADDR_FROM_7BIT(addr7) ((uint8_t)((uint8_t)(addr7) << 1))

/**
 * 7 位 I2C 地址（Linux mcp4531 驱动表）：0101xxx，低 3 位为 A2:A1:A0 硬件绑线。
 * MCP4651 的 A0 与 HVC 共用同一引脚，仅作地址脚时应保持为合法 VIL/VIH。
 */
#define MCP4651_ADDR_7BIT(a2, a1, a0) \
    ((uint8_t)(0x28u | (((uint8_t)(a2) & 1u) << 2) | (((uint8_t)(a1) & 1u) << 1) | ((uint8_t)(a0) & 1u)))

/** volatile 存储器地址（命令字节 AD3:AD0） */
#define MCP4651_MEM_WIPER0 0x00u
#define MCP4651_MEM_WIPER1 0x01u
#define MCP4651_MEM_TCON   0x04u

/** MCP4651 为 257 档（0…256），9 位编码与 Linux mcp4531 对 MCP465x 一致 */
#define MCP4651_WIPER_MIN   0u
#define MCP4651_WIPER_MAX   256u
#define MCP4651_WIPER_MASK  0x1FFu

typedef int (*mcp4651_i2c_write_t)(uint8_t i2c_address_hal, const uint8_t *data, uint16_t len);
/**
 * 带重复起始条件的写后读（读 wiper / TCON 必需）：S + 写地址 + tx[0…] + Sr + 读地址 + rx[0…] + P
 * 在 STM32 HAL 上可用 HAL_I2C_Mem_Read(hi2c, i2c_address_hal, tx[0], I2C_MEMADD_SIZE_8BIT, rx, rx_len, to)
 * 实现（单字节命令时 tx_len==1）。
 */
typedef int (*mcp4651_i2c_write_read_t)(uint8_t i2c_address_hal,
                                        const uint8_t *tx, uint16_t tx_len,
                                        uint8_t *rx, uint16_t rx_len);

typedef enum {
    MCP4651_OK = 0,
    MCP4651_ERROR_PARAM,
    MCP4651_ERROR_NOT_INIT,
    MCP4651_ERROR_I2C,
    MCP4651_ERROR_RANGE,
    MCP4651_ERROR_READ_NOT_SUPPORTED
} mcp4651_status_t;

typedef struct {
    mcp4651_i2c_write_t        write;
    mcp4651_i2c_write_read_t write_read;
    uint8_t                    i2c_address_hal;
} mcp4651_config_t;

typedef struct {
    mcp4651_i2c_write_t        write;
    mcp4651_i2c_write_read_t write_read;
    uint8_t                    i2c_address_hal;
    bool                       initialized;
} mcp4651_t;

mcp4651_status_t mcp4651_init(mcp4651_t *dev, const mcp4651_config_t *cfg);

/** 写 volatile 9 位值到指定存储器地址（wiper0/1 或 TCON 等） */
mcp4651_status_t mcp4651_write_volatile_u9(mcp4651_t *dev, uint8_t mem_addr4, uint16_t value_u9);

/** 读 volatile 9 位值（需 cfg.write_read 非空） */
mcp4651_status_t mcp4651_read_volatile_u9(mcp4651_t *dev, uint8_t mem_addr4, uint16_t *value_u9);

mcp4651_status_t mcp4651_wiper_set(mcp4651_t *dev, uint8_t wiper_index, uint16_t value_u9);
mcp4651_status_t mcp4651_wiper_get(mcp4651_t *dev, uint8_t wiper_index, uint16_t *value_u9);

mcp4651_status_t mcp4651_wiper_increment(mcp4651_t *dev, uint8_t wiper_index);
mcp4651_status_t mcp4651_wiper_decrement(mcp4651_t *dev, uint8_t wiper_index);

mcp4651_status_t mcp4651_tcon_set(mcp4651_t *dev, uint16_t tcon_u9);
mcp4651_status_t mcp4651_tcon_get(mcp4651_t *dev, uint16_t *tcon_u9);

/** 组命令字节（与 Linux drivers/iio/potentiometer/mcp4531.c 一致） */
uint8_t mcp4651_command_byte(uint8_t mem_addr4, uint8_t cmd_c1c0, uint8_t d9_d8);

#define MCP4651_CMD_WRITE 0u
#define MCP4651_CMD_INCR  1u
#define MCP4651_CMD_DECR  2u
#define MCP4651_CMD_READ  3u

#ifdef __cplusplus
}
#endif

#endif /* MCP4651_H */
