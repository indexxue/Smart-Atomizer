/**
 * @file    input.c
 * @brief   Input driver implementation for STM32F103RCT6
 */

#include "input.h"
#include "event.h"
#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

#ifdef INPUT_USE_LOG
#include "log.h"
#endif

#ifdef INPUT_USE_LED_SCENE
#include "led_scene.h"
#endif

#define INPUT_MAX_NUM  4

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    input_evt_e evt;
    uint8_t active;
    input_state_e old_state;
    volatile uint8_t triggered;
    IRQn_Type irqn;
} input_io_t;

typedef struct
{
    uint8_t num;
    input_io_t *input;
    SemaphoreHandle_t mutex;
} input_self_t;

static input_io_t s_input_list[INPUT_MAX_NUM];
static input_self_t self = {0};

static IRQn_Type input_pin_to_irqn(uint16_t pin)
{
    switch (pin)
    {
        case GPIO_PIN_0:  return EXTI0_IRQn;
        case GPIO_PIN_1:  return EXTI1_IRQn;
        case GPIO_PIN_2:  return EXTI2_IRQn;
        case GPIO_PIN_3:  return EXTI3_IRQn;
        case GPIO_PIN_4:  return EXTI4_IRQn;
        case GPIO_PIN_5:
        case GPIO_PIN_6:
        case GPIO_PIN_7:
        case GPIO_PIN_8:
        case GPIO_PIN_9:  return EXTI9_5_IRQn;
        case GPIO_PIN_10:
        case GPIO_PIN_11:
        case GPIO_PIN_12:
        case GPIO_PIN_13:
        case GPIO_PIN_14:
        case GPIO_PIN_15: return EXTI15_10_IRQn;
        default:          return (IRQn_Type)0;
    }
}

void input_pin_callback(uint16_t GPIO_Pin)
{
    if (self.input == NULL || self.mutex == NULL)
    {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (xSemaphoreTakeFromISR(self.mutex, &xHigherPriorityTaskWoken) == pdTRUE)
    {
        for (uint8_t i = 0; i < self.num; i++)
        {
            if (self.input[i].pin == GPIO_Pin)
            {
                self.input[i].triggered = 1;
                break;
            }
        }
        xSemaphoreGiveFromISR(self.mutex, &xHigherPriorityTaskWoken);
    }

    event_set_from_isr(EVT_ID_INPUT);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void input_gpio_init(input_io_t *list, uint8_t number)
{
    GPIO_InitTypeDef gpio = {0};

    self.num = number;
    self.input = list;

    __HAL_RCC_AFIO_CLK_ENABLE();

    for (uint8_t i = 0; i < number; i++)
    {
        input_io_t *in = &list[i];
        in->irqn = input_pin_to_irqn(in->pin);

        gpio.Pin = in->pin;
        gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(in->port, &gpio);

        GPIO_PinState s = HAL_GPIO_ReadPin(in->port, in->pin);
        uint8_t level_value = (s == GPIO_PIN_SET) ? 1u : 0u;
        in->old_state = (in->active == level_value) ? INPUT_EVT_CLOSE : INPUT_EVT_OPEN;

        if (in->irqn != (IRQn_Type)0)
        {
            HAL_NVIC_SetPriority(in->irqn, 5, 0);
            HAL_NVIC_EnableIRQ(in->irqn);
        }
    }
}

static void input_config(void)
{
    input_io_t *list = s_input_list;

    list[0].port = GPIOA;
    list[0].pin = GPIO_PIN_1;
    list[0].evt = INPUT_EVT_TAMPER;
    list[0].active = 0;
    list[0].old_state = INPUT_EVT_OPEN;
    list[0].triggered = 0;

    input_gpio_init(list, 1);

#ifdef INPUT_USE_LOG
    LOG_INFO("Input config done: %d inputs", self.num);
#endif
}

void input_init(void)
{
    memset(&self, 0, sizeof(input_self_t));
    memset(s_input_list, 0, sizeof(s_input_list));
    input_config();

    self.mutex = xSemaphoreCreateMutex();
    if (self.mutex == NULL)
    {
#ifdef INPUT_USE_LOG
        LOG_ERROR("Failed to create input mutex");
#endif
        return;
    }

#ifdef INPUT_USE_LOG
    LOG_INFO("Input driver initialized");
#endif
}

void input_schedule(input_notify_t notify)
{
    if (!event_is_set(EVT_ID_INPUT))
    {
        return;
    }

    for (uint8_t i = 0; i < self.num; i++)
    {
        if (xSemaphoreTake(self.mutex, 0) != pdTRUE)
        {
            continue;
        }

        if (!self.input[i].triggered)
        {
            xSemaphoreGive(self.mutex);
            continue;
        }

        self.input[i].triggered = 0;
        GPIO_TypeDef *port = self.input[i].port;
        uint16_t pin = self.input[i].pin;
        xSemaphoreGive(self.mutex);

        input_evt_e event = self.input[i].evt;
        input_state_e new_state = INPUT_EVT_CLOSE;
        input_io_t *current_input = &self.input[i];

        GPIO_PinState s1 = HAL_GPIO_ReadPin(port, pin);
        HAL_Delay(50);
        GPIO_PinState s2 = HAL_GPIO_ReadPin(port, pin);
        if (s1 != s2)
        {
            continue;
        }

        uint8_t level_value = (s1 == GPIO_PIN_SET) ? 1u : 0u;
        new_state = (current_input->active == level_value) ? INPUT_EVT_CLOSE : INPUT_EVT_OPEN;

        if (event == INPUT_EVT_NONE || event >= INPUT_EVT_MAX ||
            (new_state == current_input->old_state))
        {
            continue;
        }

        current_input->old_state = new_state;

#ifdef INPUT_USE_LOG
        const char *map[INPUT_EVT_MAX] = {
            "None", "Tamper", "DWS_R", "DWS_L", "WaterLeak"
        };
        LOG_INFO("input event: %s, %s", map[event], (new_state == INPUT_EVT_CLOSE) ? "close" : "open");
#endif

#ifdef INPUT_USE_LED_SCENE
        if (event != INPUT_EVT_DWS_R && event != INPUT_EVT_DWS_L)
        {
            led_scene_run(LED_SCENE_ID_TRIGGER);
        }
#endif

        if (notify != NULL)
        {
            notify(event, new_state);
        }
    }
}

void input_disable_all(void)
{
    if (self.input == NULL || self.num == 0)
    {
        return;
    }

    if (xSemaphoreTake(self.mutex, portMAX_DELAY) == pdTRUE)
    {
        for (uint8_t i = 0; i < self.num; i++)
        {
            if (self.input[i].irqn != (IRQn_Type)0)
            {
                HAL_NVIC_DisableIRQ(self.input[i].irqn);
            }
        }
        xSemaphoreGive(self.mutex);
    }
}

void input_enable_all(void)
{
    if (self.input == NULL || self.num == 0)
    {
        return;
    }

    if (xSemaphoreTake(self.mutex, portMAX_DELAY) == pdTRUE)
    {
        for (uint8_t i = 0; i < self.num; i++)
        {
            GPIO_PinState s = HAL_GPIO_ReadPin(self.input[i].port, self.input[i].pin);
            uint8_t level_value = (s == GPIO_PIN_SET) ? 1u : 0u;
            self.input[i].old_state = (self.input[i].active == level_value) ? INPUT_EVT_CLOSE : INPUT_EVT_OPEN;
            if (self.input[i].irqn != (IRQn_Type)0)
            {
                HAL_NVIC_EnableIRQ(self.input[i].irqn);
            }
        }
        xSemaphoreGive(self.mutex);
    }
}
