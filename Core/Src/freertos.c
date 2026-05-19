#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "spi.h"
#include "button.h"
#include "event.h"
#include "iwdg.h"
#include "input.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"
#include "proto.h"
#include "ty_link.h"
#include "boot_slot.h"
#include "strip.h"
#include "atomizer_pwm.h"
#include "adc_voltage.h"
#include "humidity.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define APP_IWDG_FEED_MS           200U
/** DHT22 minimum interval between reads (datasheet: 2 s). */
#define APP_HUMIDITY_READ_MS       2000U
#define APP_WATER_PROTECT_MV       500u

typedef struct
{
  btn_id_e id;
  btn_event_e event;
  btn_permission_e permission;
} app_btn_pending_t;

static app_btn_pending_t s_btn_pending;
static strip_t s_app_strip;
static uint8_t s_app_strip_grb[WS2818B_BUF_LEN(STRIP_SCENE_LED_NUM)];
static uint8_t s_app_strip_spi[STRIP_SPI_TX_BYTES(STRIP_SCENE_LED_NUM)];
static bool s_app_water_low;
static bool s_app_overheat_active;

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

static StaticTimer_t s_humidity_tmr_mem;
static const osTimerAttr_t s_humidity_tmr_attr = {
  .name = "humidity",
  .cb_mem = &s_humidity_tmr_mem,
  .cb_size = sizeof(s_humidity_tmr_mem),
};
static osTimerId_t s_humidity_tmr;
static bool s_humidity_hw_ok;

static void app_iwdg_timer_cb(void *argument);
static void app_humidity_timer_cb(void *argument);
static bool app_humidity_hw_init(void);
static void app_humidity_push_to_link(float rh_pct);
static void app_humidity_poll(void);
static void app_humidity_start(void);
static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);
static void app_input_notify(input_evt_e event, input_state_e state);
static void app_strip_mode_event_set(uint8_t mode_1_to_4);
static void app_on_button(const app_btn_pending_t *btn);
static void app_dispatch_events(void);
static bool app_atomizer_run_permitted(void);
static void app_atomizer_protect_sync(void);
static void app_atomizer_protect_poll(void);
static void app_atomizer_rhythm_poll(void);
static void app_strip_init(void);
static void app_event_loop(void);
static uint8_t app_ty_led_proto_from_strip(uint8_t strip_mode_1_to_4);
static uint8_t app_ty_led_strip_from_proto(uint8_t led_proto_0_to_3);
static bool app_ty_link_apply(const ty_link_set_request_t *in, ty_link_set_ack_t *out, void *user);
static void app_ty_link_sync_telemetry(void);

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);

void MX_FREERTOS_Init(void)
{
  s_iwdg_tmr = osTimerNew(app_iwdg_timer_cb, osTimerPeriodic, NULL, &s_iwdg_tmr_attr);
  s_humidity_tmr = osTimerNew(app_humidity_timer_cb, osTimerPeriodic, NULL, &s_humidity_tmr_attr);
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
}

void StartDefaultTask(void *argument)
{
  (void)argument;

  if (s_iwdg_tmr != NULL)
  {
    (void)osTimerStart(s_iwdg_tmr, (uint32_t)pdMS_TO_TICKS(APP_IWDG_FEED_MS));
  }

  event_init();

  if (log_init(NULL) == LOG_OK)
  {
    LOG_INFO("[APP] Boot: application firmware (app_start task)");
  }

  nvs_init();
  led_scene_init();
  led_scene_run(LED_SCENE_ID_BOOTUP);
  app_strip_init();
  input_init();
  button_init(app_button_notify);
  atomizer_pwm_init();
  app_atomizer_protect_poll();
  s_humidity_hw_ok = app_humidity_hw_init();

  if (ty_link_init())
  {
    ty_link_register_apply(app_ty_link_apply, NULL);
    app_ty_link_sync_telemetry();
    tyProtoTaskHandle = osThreadNew(ty_link_task, NULL, &tyProtoTask_attributes);
    if (s_humidity_hw_ok)
    {
      app_humidity_start();
    }
  }

  app_event_loop();
}

static void app_iwdg_timer_cb(void *argument)
{
  (void)argument;
  (void)HAL_IWDG_Refresh(&hiwdg);
}

static bool app_humidity_hw_init(void)
{
  return (humidity_init() == HUMIDITY_OK);
}

static void app_humidity_push_to_link(float rh_pct)
{
  uint8_t pct;

  if (rh_pct <= 0.0f)
  {
    pct = 0u;
  }
  else if (rh_pct >= 100.0f)
  {
    pct = 100u;
  }
  else
  {
    pct = (uint8_t)(rh_pct + 0.5f);
  }
  ty_link_set_measured_humidity_pct(pct);
}

static void app_humidity_poll(void)
{
  float rh_pct = 0.0f;

  if (humidity_read_rh(&rh_pct) == HUMIDITY_OK)
  {
    app_humidity_push_to_link(rh_pct);
  }
}

static void app_humidity_start(void)
{
  app_humidity_poll();
  if (s_humidity_tmr != NULL)
  {
    (void)osTimerStart(s_humidity_tmr, (uint32_t)pdMS_TO_TICKS(APP_HUMIDITY_READ_MS));
  }
}

static void app_humidity_timer_cb(void *argument)
{
  (void)argument;
  app_humidity_poll();
}

static void app_strip_init(void)
{
  ws2818b_status_t st;

  strip_scene_init();
  st = strip_register(&s_app_strip, &hspi1, s_app_strip_grb, s_app_strip_spi,
      STRIP_SCENE_LED_NUM, 0u);
  if (st != WS2818B_OK)
  {
    LOG_ERROR("strip_register failed (%d)", (int)st);
    return;
  }
  strip_scene_attach(&s_app_strip);
  strip_scene_factory_display_set(STRIP_FACTORY_MODE_MIC);
}

static uint8_t app_ty_led_proto_from_strip(uint8_t strip_mode_1_to_4)
{
  switch (strip_mode_1_to_4)
  {
    case STRIP_FACTORY_MODE_SOLID:
      return 0u;
    case STRIP_FACTORY_MODE_BREATH:
      return 1u;
    case STRIP_FACTORY_MODE_CHASE:
      return 2u;
    case STRIP_FACTORY_MODE_MIC:
    default:
      return 3u;
  }
}

static uint8_t app_ty_led_strip_from_proto(uint8_t led_proto_0_to_3)
{
  static const uint8_t map[4] = {
    STRIP_FACTORY_MODE_SOLID,
    STRIP_FACTORY_MODE_BREATH,
    STRIP_FACTORY_MODE_CHASE,
    STRIP_FACTORY_MODE_MIC,
  };

  if (led_proto_0_to_3 > 3u)
  {
    led_proto_0_to_3 = 3u;
  }
  return map[led_proto_0_to_3];
}

static bool app_ty_link_apply(const ty_link_set_request_t *in, ty_link_set_ack_t *out, void *user)
{
  (void)user;

  if ((in->change_mask & TY_CHG_HUMID) != 0u)
  {
    atomizer_pwm_set_gear_index(in->humidifier_level);
  }
  if ((in->change_mask & TY_CHG_LED) != 0u)
  {
    const uint8_t strip_mode = app_ty_led_strip_from_proto(in->led_strip_mode);
    strip_scene_factory_display_set(strip_mode);
    app_strip_mode_event_set(strip_mode);
  }

  out->status = TY_ACK_ERR_NONE;
  out->change_mask = in->change_mask;
  out->humidifier_level = atomizer_pwm_gear_index();
  out->led_strip_mode = app_ty_led_proto_from_strip(strip_scene_factory_display_get());
  return true;
}

static void app_ty_link_sync_telemetry(void)
{
  ty_link_set_humidifier_level(atomizer_pwm_gear_index());
  ty_link_set_led_strip_mode(app_ty_led_proto_from_strip(strip_scene_factory_display_get()));
  ty_link_set_status_flags(s_app_overheat_active ? TY_FLAG_OVERHEAT : 0u);
}

static void app_strip_mode_event_set(uint8_t mode_1_to_4)
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

static void app_on_button(const app_btn_pending_t *btn)
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
        app_strip_mode_event_set(next);
      }
      else if (btn->id == BTN_ID_NAV_UP)
      {
        if (atomizer_pwm_gear_increase())
        {
          ty_link_set_humidifier_level(atomizer_pwm_gear_index());
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
          ty_link_set_humidifier_level(atomizer_pwm_gear_index());
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
          led_scene_run(LED_SCENE_ID_ERROR);
          strip_scene_run(STRIP_SCENE_ID_ERROR);
        }
        break;
      }
      led_scene_run(LED_SCENE_ID_SUCCESS);
      strip_scene_run(STRIP_SCENE_ID_SUCCESS);
      break;
    default:
      break;
  }
}

static bool app_atomizer_run_permitted(void)
{
  return (!s_app_water_low) && (!s_app_overheat_active);
}

static void app_atomizer_protect_sync(void)
{
  const bool protect = !app_atomizer_run_permitted();
  const bool was_protect = atomizer_pwm_protect_active();

  atomizer_pwm_protect_set(protect);

  if (was_protect && !protect)
  {
    LOG_INFO("Atomizer PWM resumed (water OK, temperature normal), gear %u%%",
             (unsigned)atomizer_pwm_amp_percent());
  }
}

static void app_atomizer_protect_poll(void)
{
  const uint32_t water_mv = adc_voltage_water_mv();
  const bool water_low = (water_mv < APP_WATER_PROTECT_MV);

  if (water_low != s_app_water_low)
  {
    s_app_water_low = water_low;
    if (water_low)
    {
      LOG_WARN("Water level low (%lu mV < %u mV), atomizer inhibited",
               (unsigned long)water_mv, (unsigned)APP_WATER_PROTECT_MV);
    }
    else
    {
      LOG_INFO("Water level OK (%lu mV)", (unsigned long)water_mv);
    }
  }

  app_atomizer_protect_sync();
}

static void app_atomizer_rhythm_poll(void)
{
  const bool rhythm_mode =
      (strip_scene_factory_display_get() == STRIP_FACTORY_MODE_MIC);
  const bool run_ok = app_atomizer_run_permitted();
  const bool gear_active = (atomizer_pwm_gear_index() != 0u);

  if (!rhythm_mode || !run_ok || !gear_active)
  {
    atomizer_pwm_rhythm_set(false);
    return;
  }

  atomizer_pwm_rhythm_set(true);
  atomizer_pwm_rhythm_update(strip_scene_mic_level_percent());
}

static void app_dispatch_events(void)
{
  for (;;)
  {
    if (event_is_set(EVT_ID_OVERHEAT))
    {
      LOG_WARN("EVT: overtemperature alarm, atomizer inhibited");
      s_app_overheat_active = true;
      ty_link_set_status_flags(TY_FLAG_OVERHEAT);
      app_atomizer_protect_sync();
      led_scene_run(LED_SCENE_ID_ERROR);
      strip_scene_run(STRIP_SCENE_ID_ERROR);
      continue;
    }

    if (event_is_set(EVT_ID_OVERHEAT_OK))
    {
      LOG_INFO("EVT: overtemperature cleared (room temperature)");
      s_app_overheat_active = false;
      ty_link_set_status_flags(0u);
      app_atomizer_protect_sync();
      led_scene_cancel(LED_SCENE_ID_ERROR);
      strip_scene_cancel(STRIP_SCENE_ID_ERROR);
      continue;
    }

    if (event_is_set(EVT_ID_STRIP_MODE_SOLID))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_SOLID);
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_BREATH))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_BREATH);
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_CHASE))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_CHASE);
      continue;
    }
    if (event_is_set(EVT_ID_STRIP_MODE_MIC))
    {
      strip_scene_factory_display_set(STRIP_FACTORY_MODE_MIC);
      continue;
    }

    if (event_is_set(EVT_ID_BUTTON))
    {
      app_on_button(&s_btn_pending);
      continue;
    }

    break;
  }
}

static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
  (void)name;
  s_btn_pending.id = id;
  s_btn_pending.event = event;
  s_btn_pending.permission = permission;
}

static void app_input_notify(input_evt_e event, input_state_e state)
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

static void app_event_loop(void)
{
  for (;;)
  {
    event_wait_timeout_ms(20U);
    input_schedule(app_input_notify);
    button_schedule();
    app_dispatch_events();
    app_atomizer_protect_poll();
    led_scene_update();
    strip_scene_update();
    app_atomizer_rhythm_poll();
    app_ty_link_sync_telemetry();
    proto_app_dispatch_from_event_loop();
  }
}
