#include "ty_serial_frame.h"

#include <string.h>

enum
{
    ST_WAIT_A5 = 0,
    ST_WAIT_5A = 1,
    ST_CMD = 2,
    ST_LEN_LO = 3,
    ST_LEN_HI = 4,
    ST_PAYLOAD = 5,
    ST_CRC = 6,
};

void ty_serial_frame_rx_init(ty_serial_frame_rx_t *rx)
{
    if (rx != NULL)
    {
        memset(rx, 0, sizeof(*rx));
    }
}

ty_serial_rx_result_t ty_serial_frame_rx_feed(ty_serial_frame_rx_t *rx,
                                              uint8_t b,
                                              uint8_t *out_cmd,
                                              uint8_t *out_payload,
                                              uint16_t *out_len,
                                              uint16_t out_payload_max)
{
    uint8_t crc;
    uint16_t i;

    if (rx == NULL || out_cmd == NULL || out_payload == NULL || out_len == NULL)
    {
        return TY_SERIAL_RX_NEED_MORE;
    }

    switch (rx->state)
    {
    case ST_WAIT_A5:
        rx->state = (uint8_t)((b == TY_SF_SOF0) ? ST_WAIT_5A : ST_WAIT_A5);
        break;
    case ST_WAIT_5A:
        if (b == TY_SF_SOF1)
        {
            rx->state = ST_CMD;
        }
        else
        {
            rx->state = (uint8_t)((b == TY_SF_SOF0) ? ST_WAIT_5A : ST_WAIT_A5);
        }
        break;
    case ST_CMD:
        rx->cmd = b;
        rx->state = ST_LEN_LO;
        break;
    case ST_LEN_LO:
        rx->len = b;
        rx->state = ST_LEN_HI;
        break;
    case ST_LEN_HI:
        rx->len |= (uint16_t)((uint16_t)b << 8);
        if ((rx->len > TY_SF_MAX_PAYLOAD) || (rx->len > out_payload_max))
        {
            ty_serial_frame_rx_init(rx);
            break;
        }
        rx->idx = 0U;
        if (rx->len == 0U)
        {
            rx->state = ST_CRC;
        }
        else
        {
            rx->state = ST_PAYLOAD;
        }
        break;
    case ST_PAYLOAD:
        rx->buf[rx->idx++] = b;
        if (rx->idx >= rx->len)
        {
            rx->state = ST_CRC;
        }
        break;
    case ST_CRC:
        crc = (uint8_t)(rx->cmd + (uint8_t)(rx->len & 0xFFu) + (uint8_t)((rx->len >> 8) & 0xFFu));
        for (i = 0U; i < rx->len; i++)
        {
            crc = (uint8_t)(crc + rx->buf[i]);
        }
        if (crc != b)
        {
            ty_serial_frame_rx_init(rx);
            break;
        }
        *out_cmd = rx->cmd;
        if (rx->len > 0U)
        {
            (void)memcpy(out_payload, rx->buf, (size_t)rx->len);
        }
        *out_len = rx->len;
        ty_serial_frame_rx_init(rx);
        return TY_SERIAL_RX_FRAME;
    default:
        ty_serial_frame_rx_init(rx);
        break;
    }

    return TY_SERIAL_RX_NEED_MORE;
}

bool ty_serial_frame_encode(uint8_t cmd,
                            const uint8_t *payload,
                            uint16_t len,
                            uint8_t *out,
                            size_t out_cap,
                            size_t *out_len)
{
    size_t need;
    uint8_t crc;
    uint16_t i;

    if (out == NULL || out_len == NULL)
    {
        return false;
    }
    if ((len > TY_SF_MAX_PAYLOAD) || (payload == NULL && len > 0U))
    {
        return false;
    }
    need = 2U + 1U + 2U + (size_t)len + 1U;
    if (out_cap < need)
    {
        return false;
    }

    out[0] = TY_SF_SOF0;
    out[1] = TY_SF_SOF1;
    out[2] = cmd;
    out[3] = (uint8_t)(len & 0xFFu);
    out[4] = (uint8_t)((len >> 8) & 0xFFu);
    if (len > 0U)
    {
        (void)memcpy(&out[5], payload, (size_t)len);
    }
    crc = (uint8_t)(cmd + out[3] + out[4]);
    for (i = 0U; i < len; i++)
    {
        crc = (uint8_t)(crc + payload[i]);
    }
    out[5U + (size_t)len] = crc;
    *out_len = need;
    return true;
}
