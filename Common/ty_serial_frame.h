/**
 * @file ty_serial_frame.h
 * @brief TySerialFrame: SOF A5 5A, CMD, LEN le16, payload, CRC8 (see doc/esp8266_stm32_link_proto.md).
 */

#ifndef TY_SERIAL_FRAME_H
#define TY_SERIAL_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TY_SF_SOF0           0xA5u
#define TY_SF_SOF1           0x5Au
#define TY_SF_MAX_PAYLOAD    16u

typedef struct
{
    uint8_t  state;
    uint8_t  cmd;
    uint16_t len;
    uint16_t idx;
    uint8_t  buf[TY_SF_MAX_PAYLOAD];
} ty_serial_frame_rx_t;

void ty_serial_frame_rx_init(ty_serial_frame_rx_t *rx);

typedef enum
{
    TY_SERIAL_RX_NEED_MORE = 0,
    TY_SERIAL_RX_FRAME = 1,
} ty_serial_rx_result_t;

/**
 * Push one UART byte into the decoder.
 * On TY_SERIAL_RX_FRAME, @p out_cmd / @p out_payload / @p out_len are filled and rx is reset internally.
 */
ty_serial_rx_result_t ty_serial_frame_rx_feed(ty_serial_frame_rx_t *rx,
                                              uint8_t b,
                                              uint8_t *out_cmd,
                                              uint8_t *out_payload,
                                              uint16_t *out_len,
                                              uint16_t out_payload_max);

/** Build one wire frame into @p out. @p out_len is set on success. */
bool ty_serial_frame_encode(uint8_t cmd,
                            const uint8_t *payload,
                            uint16_t len,
                            uint8_t *out,
                            size_t out_cap,
                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* TY_SERIAL_FRAME_H */
