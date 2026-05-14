/**
 * @file    event.h
 * @brief   Event system for STM32F103RCT6
 */

#ifndef __EVENT_H
#define __EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    EVT_ID_NONE = 0x00000000,
    EVT_ID_BUTTON = 0x00000001,
    EVT_ID_INPUT = 0x00000002,
    EVT_ID_TIMER = 0x00000004,
    EVT_ID_WATCHDOG = 0x00000008,
} event_id_e;

void event_init(void);
void event_set(event_id_e id);
void event_set_from_isr(event_id_e id);
bool event_is_set(event_id_e id);
void event_clear(event_id_e id);
void event_schedule(void);

#ifdef __cplusplus
}
#endif

#endif /* __EVENT_H */
