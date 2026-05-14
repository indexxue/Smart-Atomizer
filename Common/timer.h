/**
 * @file    timer.h
 * @brief   Timer driver for STM32F103RCT6
 */

#ifndef __TIMER_H
#define __TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void (*timer_callback_t)(void *arg);

void* timer_init(uint32_t value_ms, timer_callback_t cb);
void timer_set_callback_arg(void *obj, void *arg);
void timer_set(void *obj, uint32_t value_ms);
void timer_start(void *obj);
void timer_start_from_isr(void *obj);
void timer_stop(void *obj);
void timer_restart(void *obj);
uint32_t timer_get_value(void *obj);
void timer_destroy(void *obj);
void timer_process(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIMER_H */
