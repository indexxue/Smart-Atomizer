/* USER CODE BEGIN Header */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "usart.h"
#include "log.h"
#include "nvs.h"
#include "led_scene.h"
#include "button.h"
#include "iwdg.h"
#include "serial_cmd.h"
#include "proto.h"
#include "queue.h"
#include "boot_slot.h"
#include <stdbool.h>
#include <string.h>
/* USER CODE END Includes */

typedef struct
{
  uint32_t iwdg_refresh_period_ms;
  uint32_t start_task_poll_ms;
  uint32_t uart3_rx_queue_depth;
  uint32_t uart3_cmd_idle_timeout_ms;
} factory_rtos_cfg_t;

typedef struct
{
  osTimerId_t iwdg;
} factory_rtos_timers_t;

typedef struct
{
  const factory_rtos_cfg_t *cfg;
  factory_rtos_timers_t timer;
} factory_rtos_self_t;

QueueHandle_t s_uart3_rx_queue = NULL;

static const factory_rtos_cfg_t s_factory_cfg = {
    .iwdg_refresh_period_ms = 5000U,
    .start_task_poll_ms = 20U,
    .uart3_rx_queue_depth = 64U,
    .uart3_cmd_idle_timeout_ms = 100U,
};

#define FACTORY_START_TASK_STACK_BYTES  (2048U)

static factory_rtos_self_t s_factory = {
    .cfg = &s_factory_cfg,
    .timer = {NULL},
};

static StaticTimer_t s_iwdg_timer_cb_mem;
static const osTimerAttr_t s_timer_iwdg_attr = {
    .name = "timer_iwdg",
    .cb_mem = &s_iwdg_timer_cb_mem,
    .cb_size = sizeof(s_iwdg_timer_cb_mem),
};

osThreadId_t StartTaskHandle;
const osThreadAttr_t StartTask_attributes = {
    .name = "StartTask",
    .stack_size = FACTORY_START_TASK_STACK_BYTES,
    .priority = (osPriority_t)osPriorityNormal,
};

static void on_iwdg_timer(void *argument);
static void ftm_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);
static void uart3_cmd_task_poll(void);
static void factory_rtos_timers_start(void);
static TickType_t factory_ms_to_ticks(uint32_t ms);
static void factory_log_versions(void);
static void factory_init_serial_cmd_path(void);

void StartThread(void *argument);
void MX_FREERTOS_Init(void);

static void on_iwdg_timer(void *argument)
{
  (void)argument;
  (void)HAL_IWDG_Refresh(&hiwdg);
}

static void ftm_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
  LOG_INFO("BTN %s %s -> %s",
           button_id_to_str(id),
           (name != NULL) ? name : "",
           button_event_to_str(event));

  switch (event)
  {
    case BTN_EVENT_SINGLE_CLICK:
      led_scene_run(LED_SCENE_ID_TRIGGER);
      break;
    case BTN_EVENT_DOUBLE_CLICK:
      led_scene_run(LED_SCENE_ID_PAIRING);
      break;
    case BTN_EVENT_LONG_PRESS:
    case BTN_EVENT_LONG_HOLD:
      led_scene_run(LED_SCENE_ID_ERROR);
      break;
    case BTN_EVENT_LONG_HOLD_UP:
      led_scene_cancel(LED_SCENE_ID_ERROR);
      if (id == BTN_ID_MODE &&
          ((uint16_t)permission & (uint16_t)BTN_PERMISSION_ZONE_SWITCH) != 0u)
      {
        LOG_INFO("Boot: toggle APP slot (%s -> other), resetting...",
                 boot_slot_running_from_b() ? "B" : "A");
        if (!boot_slot_toggle_partition_and_reset())
        {
          LOG_ERROR("Boot: slot flash failed (stay on current image)");
          led_scene_run(LED_SCENE_ID_ERROR);
        }
        break;
      }
      led_scene_run(LED_SCENE_ID_SUCCESS);
      break;
    default:
      break;
  }
}

static TickType_t factory_ms_to_ticks(uint32_t ms)
{
  TickType_t ticks = pdMS_TO_TICKS(ms);
  return (ticks == 0U) ? 1U : ticks;
}

static void uart3_cmd_task_poll(void)
{
  const TickType_t idle_timeout_ticks = factory_ms_to_ticks(s_factory.cfg->uart3_cmd_idle_timeout_ms);
  if (s_uart3_rx_queue == NULL)
  {
    return;
  }

  static char line_buf[SERIAL_CMD_LINE_MAX];
  static uint16_t pos = 0;
  static TickType_t last_rx_tick = 0;
  uint8_t ch;
  bool got_char = false;

  while (xQueueReceive(s_uart3_rx_queue, &ch, 0) == pdTRUE)
  {
    (void)serial_cmd_send(&ch, 1);
    got_char = true;
    last_rx_tick = xTaskGetTickCount();

    if (ch == '\r' || ch == '\n')
    {
      if (pos > 0U)
      {
        line_buf[pos] = '\0';
        if (proto_is_active() != 0U)
        {
          proto_process_line(line_buf);
        }
        else
        {
          serial_cmd_process_line(line_buf);
        }
        pos = 0U;
      }
    }
    else
    {
      if (pos < (SERIAL_CMD_LINE_MAX - 1U))
      {
        line_buf[pos++] = (char)ch;
      }
      else
      {
        LOG_WARN("UART3 cmd overflow, dropping current line");
        pos = 0U;
      }
    }
  }

  if (!got_char && pos > 0U && last_rx_tick != 0U)
  {
    TickType_t now = xTaskGetTickCount();
    TickType_t diff = now - last_rx_tick;
    if (diff >= idle_timeout_ticks)
    {
      line_buf[pos] = '\0';
      if (proto_is_active() != 0U)
      {
        proto_process_line(line_buf);
      }
      else
      {
        serial_cmd_process_line(line_buf);
      }
      pos = 0U;
    }
  }
}

static void factory_rtos_timers_start(void)
{
  if (s_factory.timer.iwdg != NULL)
  {
    (void)osTimerStart(s_factory.timer.iwdg, s_factory.cfg->iwdg_refresh_period_ms);
  }
}

static void factory_log_versions(void)
{
  char hw_ver[NVS_HW_VERSION_SIZE];
  char ftm_ver[NVS_FACTORY_VERSION_SIZE];

  if (nvs_hw_version_get(hw_ver))
  {
    LOG_INFO("HW version: %s", hw_ver);
  }
  if (nvs_factory_version_get(ftm_ver))
  {
    LOG_INFO("Factory version: %s", ftm_ver);
  }
}

static void factory_init_serial_cmd_path(void)
{
  serial_cmd_init(&huart3);
  serial_cmd_register_defaults();
  usart_factory_set_uart3_rx_queue(s_uart3_rx_queue);
  UART1_Start_Receive_IT();
  UART3_Start_Receive_IT();
}

void StartThread(void *argument)
{
  (void)argument;

  factory_rtos_timers_start();

  if (log_init(NULL) == LOG_OK)
  {
    LOG_INFO("[FTM] Boot: factory test (UART3 cmd + slot toggle)");
  }

  nvs_init();
  factory_init_serial_cmd_path();
  factory_log_versions();

  led_scene_init();
  led_scene_run(LED_SCENE_ID_BOOTUP);
  button_init(ftm_button_notify);

  for (;;)
  {
    (void)HAL_IWDG_Refresh(&hiwdg);
    button_schedule();
    led_scene_update();
    uart3_cmd_task_poll();
    osDelay(s_factory.cfg->start_task_poll_ms);
  }
}

void MX_FREERTOS_Init(void)
{
  s_factory.timer.iwdg = osTimerNew(on_iwdg_timer, osTimerPeriodic, NULL, &s_timer_iwdg_attr);
  s_uart3_rx_queue = xQueueCreate((UBaseType_t)s_factory.cfg->uart3_rx_queue_depth, sizeof(uint8_t));

  StartTaskHandle = osThreadNew(StartThread, NULL, &StartTask_attributes);
  if (StartTaskHandle == NULL)
  {
    Error_Handler();
  }
}
