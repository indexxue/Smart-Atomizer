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
    uint8_t measured_rh_pct;
    uint8_t target_humidity_pct;
    uint8_t humidifier_on;
    uint8_t gear;
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

static void ty_link_default_apply(const ty_link_set_request_t *in, ty_link_set_ack_t *out)
{
    s_shadow.target_humidity_pct = in->target_humidity_pct;
    s_shadow.humidifier_on = in->humidifier_on;
    s_shadow.gear = in->gear;
    out->status = TY_ACK_ERR_NONE;
    out->target_humidity_pct = s_shadow.target_humidity_pct;
    out->humidifier_on = s_shadow.humidifier_on;
    out->gear = s_shadow.gear;
}

static void ty_link_handle_set_request(const uint8_t *pl, uint16_t len)
{
    ty_link_set_request_t req;
    ty_link_set_ack_t ack;
    uint8_t wire[5];

    if (len != 4U)
    {
        return;
    }

    req.req_id = pl[0];
    req.target_humidity_pct = pl[1];
    req.humidifier_on = pl[2];
    req.gear = pl[3];

    if (req.req_id == 0U)
    {
        ack.status = TY_ACK_ERR_BAD_REQ_ID;
        ack.target_humidity_pct = s_shadow.target_humidity_pct;
        ack.humidifier_on = s_shadow.humidifier_on;
        ack.gear = s_shadow.gear;
    }
    else if ((req.target_humidity_pct > 100U) || (req.humidifier_on > 1U))
    {
        ack.status = TY_ACK_ERR_BAD_PARAM;
        ack.target_humidity_pct = s_shadow.target_humidity_pct;
        ack.humidifier_on = s_shadow.humidifier_on;
        ack.gear = s_shadow.gear;
    }
    else if ((s_apply_fn != NULL) && s_apply_fn(&req, &ack, s_apply_user))
    {
        /* caller filled ack */
    }
    else
    {
        ty_link_default_apply(&req, &ack);
    }

    wire[0] = req.req_id;
    wire[1] = ack.status;
    wire[2] = ack.target_humidity_pct;
    wire[3] = ack.humidifier_on;
    wire[4] = ack.gear;

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
        uint16_t sound_raw = adc_voltage_avg_raw(ADC_VOLTAGE_SOUND);
        uint16_t water_raw = adc_voltage_avg_raw(ADC_VOLTAGE_WATER);

        pl[0] = s_shadow.measured_rh_pct;
        pl[1] = (uint8_t)((sound_raw >> 4) > 255U ? 255U : (sound_raw >> 4));
        pl[2] = (uint8_t)((uint32_t)water_raw * 100U / 4095U);
        if (pl[2] > 100U)
        {
            pl[2] = 100U;
        }
        pl[3] = (s_shadow.humidifier_on != 0U) ? 1U : 0U;
        pl[4] = s_shadow.gear;
        pl[5] = s_shadow.target_humidity_pct;

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
        s_shadow.measured_rh_pct = pct;
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
    s_shadow.measured_rh_pct = 50U;
    s_shadow.target_humidity_pct = 50U;

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
