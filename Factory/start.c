/* USER CODE BEGIN Header */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "tim.h"
#include "gpio.h"
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
#include "spi.h"
#include "strip.h"
#include "event.h"
#include "input.h"
#include "atomizer_pwm.h"
#include "adc_voltage.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

typedef StaticTimer_t osStaticTimerDef_t;
/* USER CODE BEGIN PTD */

typedef struct
{
  uint32_t iwdg_refresh_period_ms;
  uint32_t start_task_poll_ms;
  uint32_t app_task_idle_ms;
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

/* USER CODE END PTD */

/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* USER CODE BEGIN Variables */
QueueHandle_t s_uart3_rx_queue = NULL;

/** Last button notify payload; consumed by factory_dispatch_events() on EVT_ID_BUTTON. */
typedef struct
{
  btn_id_e id;
  btn_event_e event;
  btn_permission_e permission;
} factory_btn_pending_t;

static factory_btn_pending_t s_btn_pending;

/** SPI MOSI (PA7) WS281x test strip: 4 LEDs, same scene IDs as @c led_scene. */
static strip_t s_factory_strip;
static uint8_t s_factory_strip_grb[WS2818B_BUF_LEN(STRIP_SCENE_LED_NUM)];
static uint8_t s_factory_strip_spi[STRIP_SPI_TX_BYTES(STRIP_SCENE_LED_NUM)];

/**
 * 雾化 PWM 运行条件（与 atomizer_pwm 挡位无关）：
 * - 正常：未过温 且 水位电压 >= FACTORY_WATER_PROTECT_MV → 允许按挡位输出；
 * - 禁止：过温(EVT_ID_OVERHEAT) 或 水位 < 阈值，任一成立即停 PWM；
 * - 从禁止恢复：须同时满足 收到 EVT_ID_OVERHEAT_OK（常温）且 水位正常。
 */
#define FACTORY_WATER_PROTECT_MV  500u

static bool s_factory_water_low;
/** true：处于过温告警，直至 EVT_ID_OVERHEAT_OK。 */
static bool s_factory_overheat_active;
/* USER CODE END Variables */

static const factory_rtos_cfg_t s_factory_cfg = {
    .iwdg_refresh_period_ms = 5000U,
    .start_task_poll_ms = 20U,
    .app_task_idle_ms = 100U,
    .uart3_rx_queue_depth = 64U,
    .uart3_cmd_idle_timeout_ms = 100U,
};

#define FACTORY_START_TASK_STACK_BYTES  (4096U)
#define FACTORY_APP_TASK_STACK_BYTES    (1024U)

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

osThreadId_t appTaskHandle;
const osThreadAttr_t appTask_attributes = {
    .name = "appTask",
    .stack_size = FACTORY_APP_TASK_STACK_BYTES,
    .priority = (osPriority_t)osPriorityHigh,
};

/* USER CODE BEGIN FunctionPrototypes */
static void on_iwdg_timer(void *argument);
static void button_notify_cb(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);
static void uart3_cmd_task_poll(void);
static void factory_rtos_timers_start(void);
static TickType_t factory_ms_to_ticks(uint32_t ms);
static void factory_log_versions(void);
static void factory_init_serial_cmd_path(void);
static void factory_poll_services(void);
static void factory_strip_init(void);
static void factory_strip_mode_event_set(uint8_t mode_1_to_4);
static void factory_input_notify(input_evt_e event, input_state_e state);
static void factory_on_button(const factory_btn_pending_t *btn);
static void factory_dispatch_events(void);
static bool factory_atomizer_run_permitted(void);
static void factory_atomizer_protect_sync(void);
static void factory_atomizer_protect_poll(void);
/* USER CODE END FunctionPrototypes */

void StartThread(void *argument);
void AppThreadTask(void *argument);
void MX_FREERTOS_Init(void);

static void on_iwdg_timer(void *argument)
{
  (void)argument;
  (void)HAL_IWDG_Refresh(&hiwdg);
}

static void factory_strip_mode_event_set(uint8_t mode_1_to_4)
{
  event_clear(EVT_ID_STRIP_MODE_SOLID);
  event_clear(EVT_ID_STRIP_MODE_BREATH);
  event_clear(EVT_ID_STRIP_MODE_CHASE);
  event_clear(EVT_ID_STRIP_MODE_MIC);

  switch (mode_1_to_4)
  {
    case STRIP_FACTORY_MODE_SOLID:
      event_set(EVT_ID_STRIP_MODE_SOLID);
      break;
    case STRIP_FACTORY_MODE_BREATH:
      event_set(EVT_ID_STRIP_MODE_BREATH);
      break;
    case STRIP_FACTORY_MODE_CHASE:
      event_set(EVT_ID_STRIP_MODE_CHASE);
      break;
    case STRIP_FACTORY_MODE_MIC:
    default:
      event_set(EVT_ID_STRIP_MODE_MIC);
      break;
  }
}

static void factory_on_button(const factory_btn_pending_t *btn)
{
  LOG_INFO("BTN %s -> %s", button_id_to_str(btn->id), button_event_to_str(btn->event));

  switch (btn->event)
  {
    case BTN_EVENT_SINGLE_CLICK:
      if (btn->id == BTN_ID_MODE)
      {
        uint8_t next = (uint8_t)(strip_scene_factory_display_get() + 1u);
        if (next > STRIP_FACTORY_MODE_MIC)
        {
          next = STRIP_FACTORY_MODE_SOLID;
        }
        factory_strip_mode_event_set(next);
      }
      else if (btn->id == BTN_ID_NAV_UP)
      {
        if (atomizer_pwm_gear_increase())
        {
          LOG_INFO("Atomizer gear up -> %u%%", (unsigned)atomizer_pwm_amp_percent());
        }
        else
        {
          LOG_INFO("Atomizer already at max (%u%%)", (unsigned)atomizer_pwm_amp_percent());
        }
      }
      else if (btn->id == BTN_ID_NAV_DOWN)
      {
        if (atomizer_pwm_gear_decrease())
        {
          LOG_INFO("Atomizer gear down -> %u%%", (unsigned)atomizer_pwm_amp_percent());
        }
        else
        {
          LOG_INFO("Atomizer already at min (%u%%)", (unsigned)atomizer_pwm_amp_percent());
        }
      }
      else
      {
        led_scene_run(LED_SCENE_ID_TRIGGER);
        strip_scene_run(STRIP_SCENE_ID_TRIGGER);
      }
      break;
    case BTN_EVENT_DOUBLE_CLICK:
      led_scene_run(LED_SCENE_ID_PAIRING);
      strip_scene_run(STRIP_SCENE_ID_PAIRING);
      break;
    case BTN_EVENT_LONG_PRESS:
    case BTN_EVENT_LONG_HOLD:
      led_scene_run(LED_SCENE_ID_ERROR);
      strip_scene_run(STRIP_SCENE_ID_ERROR);
      break;
    case BTN_EVENT_LONG_HOLD_UP:
      led_scene_cancel(LED_SCENE_ID_ERROR);
      strip_scene_cancel(STRIP_SCENE_ID_ERROR);
      if (btn->id == BTN_ID_MODE &&
          ((uint16_t)btn->permission & (uint16_t)BTN_PERMISSION_ZONE_SWITCH) != 0u)
      {
        LOG_INFO("Boot: toggle APP slot (%s -> other), resetting...",
                 boot_slot_running_from_b() ? "B" : "A");
        if (!boot_slot_toggle_partition_and_reset())
        {
          LOG_ERROR("Boot: slot flash failed (stay on current image)");
        }
      }
      led_scene_run(LED_SCENE_ID_SUCCESS);
      strip_scene_run(STRIP_SCENE_ID_SUCCESS);
      break;
    default:
      break;
  }
}

static bool factory_atomizer_run_permitted(void)
{
  return (!s_factory_water_low) && (!s_factory_overheat_active);
}

/** 按当前水位/过温状态同步 PWM 保护锁（禁止=占空比 0，保留挡位）。 */
static void factory_atomizer_protect_sync(void)
{
  const bool protect = !factory_atomizer_run_permitted();
  const bool was_protect = atomizer_pwm_protect_active();

  atomizer_pwm_protect_set(protect);

  if (was_protect && !protect)
  {
    LOG_INFO("Atomizer PWM resumed (water OK, temperature normal), gear %u%%",
             (unsigned)atomizer_pwm_amp_percent());
  }
}

static void factory_atomizer_protect_poll(void)
{
  const uint32_t water_mv = adc_voltage_water_mv();
  const bool water_low = (water_mv < FACTORY_WATER_PROTECT_MV);

  if (water_low != s_factory_water_low)
  {
    s_factory_water_low = water_low;
    if (water_low)
    {
      LOG_WARN("Water level low (%lu mV < %u mV), atomizer inhibited",
               (unsigned long)water_mv, (unsigned)FACTORY_WATER_PROTECT_MV);
    }
    else
    {
      LOG_INFO("Water level OK (%lu mV)", (unsigned long)water_mv);
    }
  }

  factory_atomizer_protect_sync();
}

/** Central handler: consume event bits from button / input / strip-mode producers. */
static void factory_dispatch_events(void)
{
  for (;;)
  {
    if (event_is_set(EVT_ID_OVERHEAT))
    {
      LOG_WARN("EVT: overtemperature alarm, atomizer inhibited");
      s_factory_overheat_active = true;
      factory_atomizer_protect_sync();
      led_scene_run(LED_SCENE_ID_ERROR);
      strip_scene_run(STRIP_SCENE_ID_ERROR);
      continue;
    }

    if (event_is_set(EVT_ID_OVERHEAT_OK))
    {
      LOG_INFO("EVT: overtemperature cleared (room temperature)");
      s_factory_overheat_active = false;
      factory_atomizer_protect_sync();
      led_scene_cancel(LED_SCENE_ID_ERROR);
      strip_scene_cancel(STRIP_SCENE_ID_ERROR);
      continue;
    }

    if (event_is_set(EVT_ID_STRIP_MODE_SOLID))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_SOLID);
      LOG_INFO("Strip mode 1: solid");
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_BREATH))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_BREATH);
      LOG_INFO("Strip mode 2: color breath");
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_CHASE))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_CHASE);
      LOG_INFO("Strip mode 3: color chase");
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_MIC))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_MIC);
      LOG_INFO("Strip mode 4: MIC reactive");
      continue;
    }

    if (event_is_set(EVT_ID_BUTTON))
    {
      factory_on_button(&s_btn_pending);
      continue;
    }

    break;
  }
}

static void button_notify_cb(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
  (void)name;
  s_btn_pending.id = id;
  s_btn_pending.event = event;
  s_btn_pending.permission = permission;
  /* EVT_ID_BUTTON is set inside button.c after this callback. */
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

/* USER CODE BEGIN Application */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)pcTaskName;
  (void)xTask;
  taskDISABLE_INTERRUPTS();
  for (;;)
  {
  }
}
/* USER CODE END Application */

static void factory_rtos_timers_start(void)
{
  if (s_factory.timer.iwdg != NULL)
  {
    (void)osTimerStart(s_factory.timer.iwdg, s_factory.cfg->iwdg_refresh_period_ms);
  }
  else
  {
    LOG_WARN("IWDG timer not available, fallback to loop refresh");
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
  else
  {
    LOG_WARN("HW version not found");
  }

  if (nvs_factory_version_get(ftm_ver))
  {
    LOG_INFO("Factory version: %s", ftm_ver);
  }
  else
  {
    LOG_WARN("Factory version not found");
  }
}

static void factory_init_serial_cmd_path(void)
{
  /* Bring up serial command path early to keep startup observable. */
  serial_cmd_init(&huart3);
  serial_cmd_register_defaults();
  usart_factory_set_uart3_rx_queue(s_uart3_rx_queue);
  UART1_Start_Receive_IT();
  UART3_Start_Receive_IT();
}



static void factory_input_notify(input_evt_e event, input_state_e state)
{
  if (event != INPUT_EVT_OVERHEAT)
  {
    return;
  }

  if (state == INPUT_EVT_CLOSE)
  {
    event_set(EVT_ID_OVERHEAT);
  }
  else
  {
    event_set(EVT_ID_OVERHEAT_OK);
  }
}

static void factory_poll_services(void)
{
  input_schedule(factory_input_notify);
  button_schedule();
  factory_dispatch_events();
  factory_atomizer_protect_poll();
  led_scene_update();
  strip_scene_update();
  uart3_cmd_task_poll();
}

static void factory_strip_init(void)
{
  ws2818b_status_t st;

  strip_scene_init();
  st = strip_register(&s_factory_strip, &hspi1, s_factory_strip_grb, s_factory_strip_spi,
      STRIP_SCENE_LED_NUM, 0u);
  if (st != WS2818B_OK)
  {
    LOG_ERROR("strip_register failed (%d)", (int)st);
    return;
  }
  strip_scene_attach(&s_factory_strip);
  LOG_INFO("SPI strip: Mode key cycles solid/breath/chase/MIC; UART3 cmd / other buttons override");
  strip_scene_factory_display_set(STRIP_FACTORY_MODE_MIC);
}

void AppThreadTask(void *argument)
{
  /* USER CODE BEGIN AppThreadTask */
  (void)argument;

  for (;;)
  {
    osDelay(s_factory.cfg->app_task_idle_ms);
  }
  /* USER CODE END AppThreadTask */
}

void StartThread(void *argument)
{
  /* USER CODE BEGIN StartThread */
  (void)argument;
  /* Start watchdog feed timer after scheduler is running. */
  factory_rtos_timers_start();

  /* Required for EVT_ID_STRIP_MODE_* and button path (APP uses freertos.c). */
  event_init();

  if (log_init(NULL) == LOG_OK)
  {
    LOG_INFO("[FTM] Boot: factory test firmware (StartTask, UART3 cmd)");
  }

  nvs_init();

  factory_init_serial_cmd_path();
  factory_log_versions();

  led_scene_init();
  led_scene_run(LED_SCENE_ID_BOOTUP);

  factory_strip_init();

  input_init();
  button_init(button_notify_cb);
  atomizer_pwm_init();
  factory_atomizer_protect_poll();

  for (;;)
  {
    (void)HAL_IWDG_Refresh(&hiwdg);
    factory_poll_services();
    osDelay(s_factory.cfg->start_task_poll_ms);
  }
  /* USER CODE END StartThread */
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
  appTaskHandle = osThreadNew(AppThreadTask, NULL, &appTask_attributes);
  /* appTask is non-critical; keep boot path alive even when memory is tight. */
}
