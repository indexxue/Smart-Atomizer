#include "oled.h"
#include <stdlib.h>
#include <stdint.h>
#include "oledfont.h"

static oled_config_t oled_cfg = {0};
/* Frame buffer sized for 128x64; actual width/height are configured at runtime. */
static uint8_t OLED_GRAM[128][8];

int OLED_Register(oled_i2c_write_func_t write_func,
                  oled_delay_ms_func_t delay_ms,
                  uint8_t address,
                  uint8_t width,
                  uint8_t height)
{
    if (write_func == NULL)
    {
        return -1;
    }

    oled_cfg.write_func = write_func;
    oled_cfg.delay_ms   = delay_ms;
    oled_cfg.address    = address;

    /* Clamp to buffer limits; default to 128x64 if invalid. */
    if (width == 0u || width > 128u)
    {
        oled_cfg.width = 128u;
    }
    else
    {
        oled_cfg.width = width;
    }

    if (height == 0u || height > 64u)
    {
        oled_cfg.height = 64u;
    }
    else
    {
        oled_cfg.height = height;
    }

    return 0;
}

// 反显函数
void OLED_ColorTurn(uint8_t i)
{
    if(i == 0) {
        OLED_WR_Byte(0xA6, OLED_CMD);  // 正常显示
    } else if(i == 1) {
        OLED_WR_Byte(0xA7, OLED_CMD);  // 反色显示
    }
}

// 屏幕旋转180度
void OLED_DisplayTurn(uint8_t i)
{
    if(i == 0) {
        OLED_WR_Byte(0xC8, OLED_CMD);  // 正常显示
        OLED_WR_Byte(0xA1, OLED_CMD);
    } else if(i == 1) {
        OLED_WR_Byte(0xC0, OLED_CMD);  // 反转显示
        OLED_WR_Byte(0xA0, OLED_CMD);
    }
}

void OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
    if (oled_cfg.write_func == NULL)
    {
        return;
    }
    
    uint8_t buf[2];
    buf[0] = (mode == OLED_CMD) ? 0x00u : 0x40u;
    buf[1] = dat;
    
    for (int retry = 0; retry < 2; retry++)
    {
        if (oled_cfg.write_func(oled_cfg.address, buf, 2u) == 0)
        {
            return;
        }
        if (retry < 1 && oled_cfg.delay_ms != NULL)
        {
            oled_cfg.delay_ms(1u);
        }
    }
}

// 开启OLED显示
void OLED_DisPlay_On(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);  // 电荷泵使能
    OLED_WR_Byte(0x14, OLED_CMD);  // 开启电荷泵
    OLED_WR_Byte(0xAF, OLED_CMD);  // 点亮屏幕
}

// 关闭OLED显示
void OLED_DisPlay_Off(void)
{
    OLED_WR_Byte(0x8D, OLED_CMD);  // 电荷泵使能
    OLED_WR_Byte(0x10, OLED_CMD);  // 关闭电荷泵
    OLED_WR_Byte(0xAE, OLED_CMD);  // 关闭屏幕
}

// 更新显存到OLED
void OLED_Refresh(void)
{
    uint8_t page_count = (oled_cfg.height == 0u) ? 8u : (oled_cfg.height / 8u);
    uint8_t width      = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;

    for (uint8_t i = 0u; i < page_count; i++)
    {
        OLED_WR_Byte(0xB0 + i, OLED_CMD);  // 设置行起始地址
        OLED_WR_Byte(0x00, OLED_CMD);      // 设置低列起始地址
        OLED_WR_Byte(0x10, OLED_CMD);      // 设置高列起始地址
        
        for (uint8_t n = 0u; n < width; n++)
        {
            OLED_WR_Byte(OLED_GRAM[n][i], OLED_DATA);
        }
    }
}

// 清屏函数
void OLED_Clear(void)
{
    uint8_t page_count = (oled_cfg.height == 0u) ? 8u : (oled_cfg.height / 8u);
    uint8_t width      = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;

    for (uint8_t i = 0u; i < page_count; i++)
    {
        for (uint8_t n = 0u; n < width; n++)
        {
            OLED_GRAM[n][i] = 0u;
        }
    }
    OLED_Refresh();  // 更新显示
}

// 画点
void OLED_DrawPoint(uint8_t x, uint8_t y)
{
    uint8_t width  = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;
    uint8_t height = (oled_cfg.height == 0u) ? 64u  : oled_cfg.height;

    if (x >= width || y >= height)
    {
        return;
    }

    uint8_t i = (uint8_t)(y / 8u);
    uint8_t m = (uint8_t)(y % 8u);
    uint8_t n = (uint8_t)(1u << m);
    OLED_GRAM[x][i] |= n;
}

// 清除一个点
void OLED_ClearPoint(uint8_t x, uint8_t y)
{
    uint8_t width  = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;
    uint8_t height = (oled_cfg.height == 0u) ? 64u  : oled_cfg.height;

    if (x >= width || y >= height)
    {
        return;
    }

    uint8_t i = (uint8_t)(y / 8u);
    uint8_t m = (uint8_t)(y % 8u);
    uint8_t n = (uint8_t)(1u << m);
    OLED_GRAM[x][i] &= (uint8_t)(~n);
}

// 显示字符
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1)
{
    uint8_t i, m, temp, size2, chr1;
    uint8_t y0 = y;
    uint8_t width  = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;
    uint8_t height = (oled_cfg.height == 0u) ? 64u  : oled_cfg.height;

    if (x >= width || y >= height)
    {
        return;
    }
    
    size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);
    chr1 = chr - ' ';
    
    for(i = 0; i < size2; i++) {
        if(size1 == 12) {
            temp = asc2_1206[chr1][i];
        } else if(size1 == 16) {
            temp = asc2_1608[chr1][i];
        } else if(size1 == 24) {
            temp = asc2_2412[chr1][i];
        } else {
            return;
        }
        
        for(m = 0; m < 8; m++) {
            if(temp & 0x80) {
                OLED_DrawPoint(x, y);
            } else {
                OLED_ClearPoint(x, y);
            }
            temp <<= 1;
            y++;
            
            if((y - y0) == size1) {
                y = y0;
                x++;
                break;
            }
        }
    }
}

// 显示字符串
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *chr, uint8_t size1)
{
    uint8_t width  = (oled_cfg.width  == 0u) ? 128u : oled_cfg.width;
    uint8_t height = (oled_cfg.height == 0u) ? 64u  : oled_cfg.height;

    if (x >= width || y >= height)
    {
        return;
    }

    while((*chr >= ' ') && (*chr <= '~')) {
        OLED_ShowChar(x, y, *chr, size1);
        x += size1 / 2;
        
        if (x > (uint8_t)(width - size1)) {
            x = 0u;
            y += 2;
            if (y >= height)
            {
                break;
            }
        }
        chr++;
    }
}

// 显示数字
uint32_t OLED_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while(n--) {
        result *= m;
    }
    return result;
}

void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1)
{
    uint8_t t, temp;
    for(t = 0; t < len; t++) {
        temp = (num / OLED_Pow(10, len - t - 1)) % 10;
        OLED_ShowChar(x + (size1 / 2) * t, y, temp + '0', size1);
    }
}

void OLED_Init(void)
{
    if (oled_cfg.write_func == NULL)
    {
        return;
    }
    
    OLED_WR_Byte(0xAE, OLED_CMD);  // 关闭显示    
    // 完整的初始化序列
    OLED_WR_Byte(0x00, OLED_CMD);  // 设置低列地址
    OLED_WR_Byte(0x10, OLED_CMD);  // 设置高列地址
    OLED_WR_Byte(0x40, OLED_CMD);  // 设置起始行地址
    
    OLED_WR_Byte(0x81, OLED_CMD);  // 对比度设置
    OLED_WR_Byte(0xFF, OLED_CMD);  // 对比度值（最大亮度）
    
    OLED_WR_Byte(0xA1, OLED_CMD);  // 设置段重定向
    OLED_WR_Byte(0xC8, OLED_CMD);  // 设置COM扫描方向
    
    OLED_WR_Byte(0xA6, OLED_CMD);  // 正常显示
    OLED_WR_Byte(0xA8, OLED_CMD);  // 多路复用比率
    OLED_WR_Byte(0x3F, OLED_CMD);  // duty = 1/64
    
    OLED_WR_Byte(0xD3, OLED_CMD);  // 设置显示偏移
    OLED_WR_Byte(0x00, OLED_CMD);  // 无偏移
    
    OLED_WR_Byte(0xD5, OLED_CMD);  // 设置振荡器频率
    OLED_WR_Byte(0x80, OLED_CMD);  // 设置频率
    
    OLED_WR_Byte(0xD9, OLED_CMD);  // 设置预充电期
    OLED_WR_Byte(0xF1, OLED_CMD);  // 设置预充电期
    
    OLED_WR_Byte(0xDA, OLED_CMD);  // 设置COM引脚配置
    OLED_WR_Byte(0x12, OLED_CMD);
    
    OLED_WR_Byte(0xDB, OLED_CMD);  // 设置VCOMH
    OLED_WR_Byte(0x40, OLED_CMD);
    
    OLED_WR_Byte(0x20, OLED_CMD);  // 设置内存地址模式
    OLED_WR_Byte(0x02, OLED_CMD);  // 页地址模式
    
    // 电荷泵设置（关键！）
    OLED_WR_Byte(0x8D, OLED_CMD);  // 电荷泵设置
    OLED_WR_Byte(0x14, OLED_CMD);  // 启用电荷泵
    
    OLED_WR_Byte(0xA4, OLED_CMD);  // 全部显示ON
    OLED_WR_Byte(0xA6, OLED_CMD);  // 正常显示
    
    // 最后开启显示
    OLED_WR_Byte(0xAF, OLED_CMD);  // 开启显示
}

void OLED_TEST(){
	OLED_DrawPoint(64, 32);  // 在中心画一个点
	OLED_Refresh();

	// 画一个矩形框
	for(int i = 10; i < 118; i++) {
			OLED_DrawPoint(i, 10);
			OLED_DrawPoint(i, 54);
	}
	for(int i = 10; i < 55; i++) {
			OLED_DrawPoint(10, i);
			OLED_DrawPoint(118, i);
	}
	OLED_Refresh();
}

