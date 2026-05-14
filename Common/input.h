/**
 * @file    input.h
 * @brief   Input driver for STM32F103RCT6
 */

#ifndef __INPUT_H
#define __INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    INPUT_EVT_NONE = 0,
    INPUT_EVT_TAMPER,
    INPUT_EVT_DWS_R,
    INPUT_EVT_DWS_L,
    INPUT_EVT_WATER,
    INPUT_EVT_OVERHEAT,
    INPUT_EVT_MAX,
} input_evt_e;

typedef enum
{
    INPUT_EVT_CLOSE = 0,
    INPUT_EVT_OPEN,
} input_state_e;

typedef void (*input_notify_t)(input_evt_e event, input_state_e state);

void input_init(void);
void input_schedule(input_notify_t notify);
void input_disable_all(void);
void input_enable_all(void);

void input_pin_callback(uint16_t GPIO_Pin);

#ifdef __cplusplus
}
#endif

#endif /* __INPUT_H */
