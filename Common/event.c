#include "event.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

#ifdef EVENT_USE_LOG
#include "log.h"
#endif

typedef struct
{
    uint32_t mask;
    SemaphoreHandle_t semaphore;
    SemaphoreHandle_t mutex;
} event_self_t;

static event_self_t *self = NULL;

void event_init(void)
{
    if (NULL != self)
    {
        return;
    }

    static event_self_t local_self;
    self = &local_self;
    memset(self, 0, sizeof(event_self_t));

    self->semaphore = xSemaphoreCreateBinary();
    if (NULL == self->semaphore)
    {
#ifdef EVENT_USE_LOG
        LOG_ERROR("semaphore init failed");
#endif
        self = NULL;
        return;
    }

    self->mutex = xSemaphoreCreateMutex();
    if (NULL == self->mutex)
    {
#ifdef EVENT_USE_LOG
        LOG_ERROR("mutex init failed");
#endif
        vSemaphoreDelete(self->semaphore);
        self = NULL;
        return;
    }
}

void event_set(event_id_e id)
{
    if (NULL == self)
    {
        return;
    }

    if (xSemaphoreTake(self->mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }

    if (0 != (self->mask & id))
    {
        xSemaphoreGive(self->mutex);
        return;
    }

    self->mask |= id;
    xSemaphoreGive(self->mutex);
    
    xSemaphoreGive(self->semaphore);
}

void event_set_from_isr(event_id_e id)
{
    if (NULL == self)
    {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (xSemaphoreTakeFromISR(self->mutex, &xHigherPriorityTaskWoken) != pdTRUE)
    {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return;
    }

    if (0 != (self->mask & id))
    {
        xSemaphoreGiveFromISR(self->mutex, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return;
    }

    self->mask |= id;
    xSemaphoreGiveFromISR(self->mutex, &xHigherPriorityTaskWoken);
    
    xSemaphoreGiveFromISR(self->semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

bool event_is_set(event_id_e id)
{
    if (NULL == self)
    {
        return false;
    }

    if (xSemaphoreTake(self->mutex, 0) != pdTRUE)
    {
        return false;
    }

    bool status = (self->mask & id) ? true : false;
    if (status)
    {
        self->mask &= ~id;
    }

    xSemaphoreGive(self->mutex);
    return status;
}

void event_clear(event_id_e id)
{
    if (NULL == self)
    {
        return;
    }

    if (xSemaphoreTake(self->mutex, 0) != pdTRUE)
    {
        return;
    }

    self->mask &= ~id;
    xSemaphoreGive(self->mutex);
}

void event_schedule(void)
{
    if (NULL == self)
    {
        return;
    }

    xSemaphoreTake(self->semaphore, portMAX_DELAY);
}
