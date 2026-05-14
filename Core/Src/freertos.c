#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "boot_slot.h"
#include "button.h"
#include "event.h"
#include "iwdg.h"
#include "input.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"
#include "proto.h"
#include "ty_link.h"

#define APP_IWDG_FEED_MS 200U

osThreadId_t tyProtoTaskHandle;
const osThreadAttr_t tyProtoTask_attributes = {
  .name = "proto",
  .stack_size = 256 * 4,
  .priority = (osPriority_t)osPriorityAboveNormal,
};
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "app_start",
  .stack_size = 4096,
  .priority = (osPriority_t)osPriorityNormal,
};

static StaticTimer_t s_iwdg_tmr_mem;
static const osTimerAttr_t s_iwdg_tmr_attr = {
  .name = "iwdg",
  .cb_mem = &s_iwdg_tmr_mem,
  .cb_size = sizeof(s_iwdg_tmr_mem),
};
static osTimerId_t s_iwdg_tmr;

static void app_iwdg_timer_cb(void *argument);
static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);
static void app_event_loop(void);

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);

void MX_FREERTOS_Init(void) {
  s_iwdg_tmr = osTimerNew(app_iwdg_timer_cb, osTimerPeriodic, NULL, &s_iwdg_tmr_attr);
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
}

void StartDefaultTask(void *argument)
{
  (void)argument;

  if (s_iwdg_tmr != NULL)
  {
    (void)osTimerStart(s_iwdg_tmr, (uint32_t)pdMS_TO_TICKS(APP_IWDG_FEED_MS));
  }

  (void)log_init(NULL);
  event_init();
  nvs_init();
  led_scene_init();
  led_scene_run(LED_SCENE_ID_BOOTUP);
  button_init(app_button_notify);
  input_init();

  if (ty_link_init())
  {
    tyProtoTaskHandle = osThreadNew(ty_link_task, NULL, &tyProtoTask_attributes);
  }

  app_event_loop();
}

static void app_iwdg_timer_cb(void *argument)
{
  (void)argument;
  (void)HAL_IWDG_Refresh(&hiwdg);
}

static void app_event_loop(void)
{
  for (;;)
  {
    event_schedule();
    proto_app_dispatch_from_event_loop();
  }
}

static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
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
      if (id == BTN_ID_WAKE && ((uint16_t)permission & (uint16_t)BTN_PERMISSION_FTM) != 0u)
      {
        LOG_INFO("Boot: APP-A requested, resetting");
        if (boot_slot_request_app_a())
        {
          boot_slot_system_reset();
        }
      }
      else
      {
        led_scene_cancel(LED_SCENE_ID_ERROR);
        led_scene_run(LED_SCENE_ID_SUCCESS);
      }
      break;
    default:
      break;
  }
}
