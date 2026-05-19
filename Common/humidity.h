/**
 * @file    humidity.h
 * @brief   环境湿度（DHT22 / PA2 单总线）。
 */

#ifndef HUMIDITY_H
#define HUMIDITY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  HUMIDITY_OK = 0,
  HUMIDITY_ERROR_PARAM,
  HUMIDITY_ERROR_BUS,
  HUMIDITY_ERROR_CHECKSUM
} humidity_status_t;

/** 在 MX_GPIO_Init 之后调用：配置 PA2 并初始化传感器。 */
humidity_status_t humidity_init(void);

/** 读取相对湿度（%RH）。 */
humidity_status_t humidity_read_rh(float *rh_pct);

/** 读取温度（°C）与相对湿度（%RH）。 */
humidity_status_t humidity_read(float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif

#endif /* HUMIDITY_H */
