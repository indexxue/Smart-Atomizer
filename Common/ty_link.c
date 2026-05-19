#include "ty_link.h"

#include "adc_voltage.h"
#include "event.h"
#include "proto.h"
#include "ty_serial_frame.h"
#include "usart.h"

#include "cmsis_os2.h"

#include <string.h>

#define TY_LINK_RX_QUEUE_DEPTH       128u
#define TY_LINK_SET_REQ_QUEUE_DEPTH  8u
#define TY_LINK_SENSOR_PERIOD_MS     500u
#define TY_LINK_TX_BUF               (2u + 1u + 2u + TY_SF_MAX_PAYLOAD + 1u)

static osMessageQueueId_t s_rx_q;
static osMessageQueueId_t s_set_req_q;
static osMutexId_t s_link_mtx;
static ty_serial_frame_rx_t s_decoder;
static uint8_t s_inited;

static ty_link_apply_settings_fn s_apply_fn;
static void *s_apply_user;

typedef struct
{
    uint8_t humidity_pct;
    uint8_t humidifier_level;
    uint8_t led_strip_mode;
    uint8_t status_flags;
} ty_shadow_t;

static ty_shadow_t s_shadow;

static void ty_link_isr_byte(uint8_t byte, void *user)
{
    osMessageQueueId_t q = (osMessageQueueId_t)user;
    if (q != NULL)
    {
        (void)osMessageQueuePut(q, &byte, 0U, 0U);
    }
}

static bool ty_link_send_locked(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t wire[TY_LINK_TX_BUF];
    size_t wire_len = 0U;

    if (!ty_serial_frame_encode(cmd, payload, len, wire, sizeof(wire), &wire_len))
    {
        return false;
    }
    return proto_uart1_send(wire, (uint16_t)wire_len);
}

bool ty_link_send_raw_frame(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    bool ok;
    if (s_link_mtx == NULL)
    {
        return false;
    }
    if (osMutexAcquire(s_link_mtx, osWaitForever) != osOK)
    {
        return false;
    }
    ok = ty_link_send_locked(cmd, payload, len);
    (void)osMutexRelease(s_link_mtx);
    return ok;
}

static uint8_t ty_link_clamp_level(uint8_t level)
{
    if (level > 3U)
    {
        return 3U;
    }
    return level;
}

static void ty_link_fill_current_ack(ty_link_set_ack_t *ack)
{
    ack->humidifier_level = s_shadow.humidifier_level;
    ack->led_strip_mode = s_shadow.led_strip_mode;
}

static void ty_link_default_apply(const ty_link_set_request_t *in, ty_link_set_ack_t *out)
{
    if ((in->change_mask & TY_CHG_HUMID) != 0U)
    {
        s_shadow.humidifier_level = ty_link_clamp_level(in->humidifier_level);
    }
    if ((in->change_mask & TY_CHG_LED) != 0U)
    {
        s_shadow.led_strip_mode = ty_link_clamp_level(in->led_strip_mode);
    }
    out->status = TY_ACK_ERR_NONE;
    out->change_mask = in->change_mask;
    ty_link_fill_current_ack(out);
}

static uint8_t ty_link_validate_set_request(const ty_link_set_request_t *req)
{
    if (req->req_id == 0U)
    {
        return TY_ACK_ERR_BAD_REQ_ID;
    }
    if (req->change_mask == 0U)
    {
        return TY_ACK_ERR_BAD_REQ_ID;
    }
    if ((req->change_mask & (uint8_t)~0x03u) != 0U)
    {
        return TY_ACK_ERR_BAD_MASK;
    }
    if (((req->change_mask & TY_CHG_HUMID) != 0U) && (req->humidifier_level > 3U))
    {
        return TY_ACK_ERR_BAD_HUMID;
    }
    if (((req->change_mask & TY_CHG_LED) != 0U) && (req->led_strip_mode > 3U))
    {
        return TY_ACK_ERR_BAD_LED;
    }
    return TY_ACK_ERR_NONE;
}

static void ty_link_handle_set_request(const uint8_t *pl, uint16_t len)
{
    ty_link_set_request_t req;
    ty_link_set_ack_t ack;
    uint8_t wire[5];
    uint8_t st;

    if (len != 4U)
    {
        return;
    }

    req.req_id = pl[0];
    req.change_mask = pl[1];
    req.humidifier_level = pl[2];
    req.led_strip_mode = pl[3];

    st = ty_link_validate_set_request(&req);
    ack.status = st;
    ack.change_mask = req.change_mask;
    ty_link_fill_current_ack(&ack);

    if (st == TY_ACK_ERR_NONE)
    {
        if ((s_apply_fn != NULL) && s_apply_fn(&req, &ack, s_apply_user))
        {
            if (osMutexAcquire(s_link_mtx, osWaitForever) == osOK)
            {
                s_shadow.humidifier_level = ty_link_clamp_level(ack.humidifier_level);
                s_shadow.led_strip_mode = ty_link_clamp_level(ack.led_strip_mode);
                (void)osMutexRelease(s_link_mtx);
            }
        }
        else
        {
            ty_link_default_apply(&req, &ack);
        }
    }

    wire[0] = req.req_id;
    wire[1] = ack.status;
    wire[2] = req.change_mask;
    wire[3] = ack.humidifier_level;
    wire[4] = ack.led_strip_mode;

    if (osMutexAcquire(s_link_mtx, osWaitForever) != osOK)
    {
        return;
    }
    (void)ty_link_send_locked(TY_CMD_SET_ACK, wire, 5U);
    (void)osMutexRelease(s_link_mtx);
}

static void ty_link_on_frame(uint8_t cmd, const uint8_t *pl, uint16_t len)
{
    if (cmd == TY_CMD_SET_REQUEST)
    {
        if (len == 4U && s_set_req_q != NULL)
        {
            uint8_t copy[4];
            (void)memcpy(copy, pl, 4U);
            if (osMessageQueuePut(s_set_req_q, copy, 0U, 0U) == osOK)
            {
                event_signal();
            }
        }
    }
}

void ty_link_service_main_from_event_loop(void)
{
    uint8_t pl[4];

    if (s_set_req_q == NULL)
    {
        return;
    }
    while (osMessageQueueGet(s_set_req_q, pl, NULL, 0U) == osOK)
    {
        ty_link_handle_set_request(pl, 4U);
    }
}

static void ty_link_maybe_send_sensor_report(void)
{
    static uint32_t s_last_tick;
    uint32_t now = osKernelGetTickCount();
    uint32_t freq = osKernelGetTickFreq();
    uint32_t period_ticks;

    if (freq == 0U)
    {
        freq = 1000U;
    }
    period_ticks = (TY_LINK_SENSOR_PERIOD_MS * freq) / 1000U;
    if (period_ticks == 0U)
    {
        period_ticks = 1U;
    }

    if ((uint32_t)(now - s_last_tick) < period_ticks)
    {
        return;
    }
    s_last_tick = now;

    if (osMutexAcquire(s_link_mtx, osWaitForever) != osOK)
    {
        return;
    }

    {
        uint8_t pl[6];
        uint16_t sound_raw = adc_voltage_audio_raw();

        pl[0] = s_shadow.humidity_pct;
        pl[1] = (uint8_t)((sound_raw >> 4) > 255U ? 255U : (sound_raw >> 4));
        pl[2] = adc_voltage_water_level_pct();
        pl[3] = ty_link_clamp_level(s_shadow.humidifier_level);
        pl[4] = ty_link_clamp_level(s_shadow.led_strip_mode);
        pl[5] = (uint8_t)(s_shadow.status_flags & TY_FLAG_OVERHEAT);

        (void)ty_link_send_locked(TY_CMD_SENSOR_REPORT, pl, 6U);
    }
    (void)osMutexRelease(s_link_mtx);
}

void ty_link_set_measured_humidity_pct(uint8_t pct)
{
    if (s_link_mtx == NULL)
    {
        return;
    }
    if (pct > 100U)
    {
        pct = 100U;
    }
    if (osMutexAcquire(s_link_mtx, osWaitForever) == osOK)
    {
        s_shadow.humidity_pct = pct;
        (void)osMutexRelease(s_link_mtx);
    }
}

void ty_link_set_humidifier_level(uint8_t level)
{
    if (s_link_mtx == NULL)
    {
        return;
    }
    if (osMutexAcquire(s_link_mtx, osWaitForever) == osOK)
    {
        s_shadow.humidifier_level = ty_link_clamp_level(level);
        (void)osMutexRelease(s_link_mtx);
    }
}

void ty_link_set_led_strip_mode(uint8_t mode)
{
    if (s_link_mtx == NULL)
    {
        return;
    }
    if (osMutexAcquire(s_link_mtx, osWaitForever) == osOK)
    {
        s_shadow.led_strip_mode = ty_link_clamp_level(mode);
        (void)osMutexRelease(s_link_mtx);
    }
}

void ty_link_set_status_flags(uint8_t flags)
{
    if (s_link_mtx == NULL)
    {
        return;
    }
    if (osMutexAcquire(s_link_mtx, osWaitForever) == osOK)
    {
        s_shadow.status_flags = (uint8_t)(flags & TY_FLAG_OVERHEAT);
        (void)osMutexRelease(s_link_mtx);
    }
}

void ty_link_register_apply(ty_link_apply_settings_fn fn, void *user)
{
    s_apply_fn = fn;
    s_apply_user = user;
}

bool ty_link_init(void)
{
    const osMutexAttr_t mtx_attr = {.name = "tyLinkMtx"};
    const osMessageQueueAttr_t q_attr = {.name = "tyLinkRx"};

    if (s_inited != 0U)
    {
        return true;
    }

    s_rx_q = osMessageQueueNew(TY_LINK_RX_QUEUE_DEPTH, sizeof(uint8_t), &q_attr);
    s_set_req_q = osMessageQueueNew(TY_LINK_SET_REQ_QUEUE_DEPTH, sizeof(uint8_t) * 4U, &q_attr);
    s_link_mtx = osMutexNew(&mtx_attr);
    if ((s_rx_q == NULL) || (s_set_req_q == NULL) || (s_link_mtx == NULL))
    {
        return false;
    }

    memset(&s_shadow, 0, sizeof(s_shadow));
    s_shadow.humidity_pct = TY_LINK_HUMIDITY_PLACEHOLDER_PCT;
    s_shadow.led_strip_mode = 3U;

    proto_init(&huart1);
    UART1_Start_Receive_IT();

    ty_serial_frame_rx_init(&s_decoder);

    proto_esp_register_rx_byte_handler(ty_link_isr_byte, s_rx_q);
    proto_uart_set_mode(PROTO_UART_MODE_ESP8266);

    s_inited = 1U;
    return true;
}

void ty_link_task(void *argument)
{
    (void)argument;
    if (s_inited == 0U)
    {
        for (;;)
        {
            osDelay(1000U);
        }
    }

    ty_serial_frame_rx_init(&s_decoder);

    for (;;)
    {
        uint8_t b;
        osStatus_t st = osMessageQueueGet(s_rx_q, &b, NULL, 50U);
        if (st == osOK)
        {
            do
            {
                uint8_t cmd;
                uint8_t pl[TY_SF_MAX_PAYLOAD];
                uint16_t plen;

                if (ty_serial_frame_rx_feed(&s_decoder, b, &cmd, pl, &plen, TY_SF_MAX_PAYLOAD) == TY_SERIAL_RX_FRAME)
                {
                    ty_link_on_frame(cmd, pl, plen);
                }
            } while (osMessageQueueGet(s_rx_q, &b, NULL, 0U) == osOK);
        }

        ty_link_maybe_send_sensor_report();
    }
}
