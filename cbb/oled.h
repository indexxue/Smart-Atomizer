#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#define OLED_CMD   0u
#define OLED_DATA  1u

typedef int  (*oled_i2c_write_func_t)(uint8_t addr, const uint8_t *data, uint16_t len);
typedef void (*oled_delay_ms_func_t)(uint32_t ms);

typedef struct
{
    oled_i2c_write_func_t write_func;
    oled_delay_ms_func_t  delay_ms;
    uint8_t               address;
    uint8_t               width;
    uint8_t               height;
} oled_config_t;

int OLED_Register(oled_i2c_write_func_t write_func,
                  oled_delay_ms_func_t delay_ms,
                  uint8_t address,
                  uint8_t width,
                  uint8_t height);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1);
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *chr, uint8_t size1);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1);
void OLED_DrawPoint(uint8_t x, uint8_t y);
void OLED_Refresh(void);
void OLED_WR_Byte(uint8_t dat, uint8_t mode);
void OLED_TEST(void);

#endif
