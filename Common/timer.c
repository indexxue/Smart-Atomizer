/**
 * @file    timer.c
 * @brief   Timer driver implementation for STM32F103RCT6
 */

#include "timer.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"
#include <string.h>

#ifdef TIMER_USE_LOG
#include "log.h"
#endif

#define MAX_TIMERS 16

typedef struct
{
    TimerHandle_t timer_handle;
    timer_callback_t callback;
    void *callback_arg;
    uint32_t timeout_ms;
    uint8_t active;
    uint8_t in_use;
    TickType_t start_tick;
} timer_obj_t;

static timer_obj_t timer_pool[MAX_TIMERS];
static uint8_t timer_count = 0;

static void timer_callback_wrapper(TimerHandle_t xTimer)
{
    timer_obj_t *obj = (timer_obj_t *)pvTimerGetTimerID(xTimer);
    if (obj != NULL && obj->callback != NULL)
    {
        obj->callback(obj->callback_arg);
    }
}

void* timer_init(uint32_t value_ms, timer_callback_t cb)
{
    if (value_ms == 0 || cb == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Invalid timer parameters");
#endif
        return NULL;
    }

    if (timer_count >= MAX_TIMERS)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Maximum number of timers reached");
#endif
        return NULL;
    }

    timer_obj_t *obj = NULL;
    for (uint8_t i = 0; i < MAX_TIMERS; i++)
    {
        if (!timer_pool[i].in_use)
        {
            obj = &timer_pool[i];
            obj->in_use = 1;
            break;
        }
    }
    if (obj == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Maximum number of timers reached");
#endif
        return NULL;
    }

    obj->callback = cb;
    obj->callback_arg = NULL;
    obj->timeout_ms = value_ms;
    obj->active = 0;
    obj->start_tick = 0;

    TickType_t timeout_ticks = pdMS_TO_TICKS(value_ms);
    obj->timer_handle = xTimerCreate("T", timeout_ticks, pdTRUE, obj, timer_callback_wrapper);
    if (obj->timer_handle == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Failed to create FreeRTOS timer");
#endif
        obj->in_use = 0;
        return NULL;
    }

    timer_count++;
    return (void *)obj;
}

void timer_set_callback_arg(void *obj, void *arg)
{
    if (obj == NULL)
    {
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    tm->callback_arg = arg;
}

void timer_set(void *obj, uint32_t value_ms)
{
    if (obj == NULL || value_ms == 0)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Invalid timer parameters");
#endif
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;

    if (tm->active)
    {
        xTimerStop(tm->timer_handle, 0);
    }

    tm->timeout_ms = value_ms;
    TickType_t timeout_ticks = pdMS_TO_TICKS(value_ms);
    xTimerChangePeriod(tm->timer_handle, timeout_ticks, 0);
    BaseType_t result = xTimerStart(tm->timer_handle, 0);
    if (result == pdPASS || result == pdTRUE)
    {
        tm->active = 1;
        tm->start_tick = xTaskGetTickCount();
    }
}

void timer_start(void *obj)
{
    if (obj == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Invalid timer object");
#endif
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;

    BaseType_t result = xTimerStart(tm->timer_handle, 0);
    if (result == pdPASS || result == pdTRUE)
    {
        tm->active = 1;
        tm->start_tick = xTaskGetTickCount();
    }
#ifdef TIMER_USE_LOG
    else
    {
        LOG_ERROR("Failed to start timer, result: %d", result);
    }
#endif
}

void timer_start_from_isr(void *obj)
{
    if (obj == NULL)
    {
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    BaseType_t result = xTimerStartFromISR(tm->timer_handle, &xHigherPriorityTaskWoken);
    if (result == pdPASS || result == pdTRUE)
    {
        tm->active = 1;
        tm->start_tick = xTaskGetTickCountFromISR();
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void timer_stop(void *obj)
{
    if (obj == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Invalid timer object");
#endif
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    if (xTimerStop(tm->timer_handle, 0) == pdPASS)
    {
        tm->active = 0;
    }
}

void timer_restart(void *obj)
{
    if (obj == NULL)
    {
#ifdef TIMER_USE_LOG
        LOG_ERROR("Invalid timer object");
#endif
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    xTimerStop(tm->timer_handle, 0);
    BaseType_t result = xTimerStart(tm->timer_handle, 0);
    if (result == pdPASS || result == pdTRUE)
    {
        tm->active = 1;
        tm->start_tick = xTaskGetTickCount();
    }
}

uint32_t timer_get_value(void *obj)
{
    if (obj == NULL)
    {
        return 0;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    return tm->timeout_ms;
}

void timer_destroy(void *obj)
{
    if (obj == NULL)
    {
        return;
    }

    timer_obj_t *tm = (timer_obj_t *)obj;
    xTimerStop(tm->timer_handle, 0);
    xTimerDelete(tm->timer_handle, 0);
    tm->in_use = 0;
    if (timer_count > 0)
    {
        timer_count--;
    }
}

void timer_process(void)
{
    TickType_t current_tick = xTaskGetTickCount();
    TickType_t timeout_ticks;

    for (uint8_t i = 0; i < MAX_TIMERS; i++)
    {
        timer_obj_t *tm = &timer_pool[i];
        if (!tm->in_use || !tm->active)
        {
            continue;
        }

        timeout_ticks = pdMS_TO_TICKS(tm->timeout_ms);
        if ((current_tick - tm->start_tick) >= timeout_ticks)
        {
            tm->active = 0;
            if (tm->callback != NULL)
            {
                tm->callback(tm->callback_arg);
            }
        }
    }
}
