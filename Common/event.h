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
    EVT_ID_OVERHEAT = 0x00000010,
} event_id_e;

void event_init(void);
void event_set(event_id_e id);
void event_set_from_isr(event_id_e id);
/** Wake the main event loop without OR-ing a new bit (use after pushing work to a side queue, e.g. ty_link set-request). */
void event_signal(void);
void event_signal_from_isr(void);
bool event_is_set(event_id_e id);
void event_clear(event_id_e id);
void event_schedule(void);
/** Same wait queue as event_schedule(), but returns after timeout for periodic work (e.g. button scan). */
void event_wait_timeout_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __EVENT_H */
