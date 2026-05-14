#include "proto.h"

#include "serial_cmd.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PROTO_RX_BUF_SIZE       512U
#define PROTO_RX_DUMP_DEFAULT   64U
#define PROTO_RX_DUMP_MAX       256U
#define PROTO_MAX_TX_BYTES      64U
#define PROTO_MAX_WAIT_MS       3000U

static UART_HandleTypeDef *s_uart1 = NULL;
static uint8_t s_rx_buf[PROTO_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t s_proto_active = 0U;
static osThreadId_t s_proto_monitor_task = NULL;

#define PROTO_MONITOR_TASK_STACK_BYTES   (512U)
#define PROTO_MONITOR_TASK_POLL_MS       (10U)

static const osThreadAttr_t s_proto_monitor_task_attr = {
    .name = "protoMon",
    .stack_size = PROTO_MONITOR_TASK_STACK_BYTES,
    .priority = (osPriority_t)osPriorityLow,
};

static void proto_monitor_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        if (s_proto_active != 0U)
        {
            proto_poll_monitor();
        }
        osDelay(PROTO_MONITOR_TASK_POLL_MS);
    }
}

static void proto_clear(void)
{
    taskENTER_CRITICAL();
    s_rx_head = 0U;
    s_rx_tail = 0U;
    taskEXIT_CRITICAL();
}

static uint16_t proto_pop(uint8_t *out, uint16_t max_len)
{
    uint16_t count = 0U;
    if (out == NULL || max_len == 0U)
    {
        return 0U;
    }
    while ((count < max_len) && (s_rx_tail != s_rx_head))
    {
        out[count++] = s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % PROTO_RX_BUF_SIZE);
    }
    return count;
}

static int proto_hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

static bool proto_hex_to_byte(const char *s, uint8_t *out)
{
    int hi;
    int lo;
    if (s == NULL || out == NULL || strlen(s) != 2U)
    {
        return false;
    }
    hi = proto_hex_nibble(s[0]);
    lo = proto_hex_nibble(s[1]);
    if (hi < 0 || lo < 0)
    {
        return false;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

static bool proto_parse_tx_bytes(int argc, const char *argv[], int start_idx, uint8_t *out, uint16_t *out_len)
{
    uint16_t n = 0U;
    int i;
    if (out == NULL || out_len == NULL || start_idx >= argc)
    {
        return false;
    }
    for (i = start_idx; i < argc; i++)
    {
        uint8_t b = 0U;
        if (n >= PROTO_MAX_TX_BYTES)
        {
            return false;
        }
        if (!proto_hex_to_byte(argv[i], &b))
        {
            return false;
        }
        out[n++] = b;
    }
    *out_len = n;
    return (n > 0U);
}

static uint8_t proto_is_sep_char(char c)
{
    return (uint8_t)(c == ' ' || c == '\t' || c == ',');
}

static uint8_t proto_is_hex_prefix(const char *s, size_t n, size_t *data_idx)
{
    if (s == NULL || data_idx == NULL || n < 4U)
    {
        return 0U;
    }

    if (((s[0] == 'h') || (s[0] == 'H')) &&
        ((s[1] == 'e') || (s[1] == 'E')) &&
        ((s[2] == 'x') || (s[2] == 'X')))
    {
        size_t idx = 3U;
        if (s[idx] == ':')
        {
            idx++;
        }
        while (idx < n && proto_is_sep_char(s[idx]) != 0U)
        {
            idx++;
        }
        if (idx >= n)
        {
            return 0U;
        }
        *data_idx = idx;
        return 1U;
    }
    return 0U;
}

static bool proto_parse_inline_hex(const char *s, uint8_t *out, uint16_t *out_len)
{
    uint16_t n = 0U;
    size_t i = 0U;
    if (s == NULL || out == NULL || out_len == NULL)
    {
        return false;
    }

    while (s[i] != '\0')
    {
        int hi;
        int lo;

        while (s[i] != '\0' && proto_is_sep_char(s[i]) != 0U)
        {
            i++;
        }
        if (s[i] == '\0')
        {
            break;
        }

        if (n >= PROTO_MAX_TX_BYTES)
        {
            return false;
        }

        if (s[i] == '0' && (s[i + 1U] == 'x' || s[i + 1U] == 'X'))
        {
            i += 2U;
        }

        hi = proto_hex_nibble(s[i]);
        lo = proto_hex_nibble(s[i + 1U]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        i += 2U;

        if (s[i] != '\0' && proto_is_sep_char(s[i]) == 0U)
        {
            return false;
        }

        out[n++] = (uint8_t)((hi << 4) | lo);
    }

    *out_len = n;
    return (n > 0U);
}

static bool proto_send(const uint8_t *data, uint16_t len)
{
    if (s_uart1 == NULL || data == NULL || len == 0U)
    {
        return false;
    }
    return (HAL_UART_Transmit(s_uart1, (uint8_t *)data, len, SERIAL_CMD_TX_TIMEOUT_MS) == HAL_OK);
}

static void proto_reply_hex(const char *cmd, const uint8_t *data, uint16_t len)
{
    char buf[SERIAL_CMD_LINE_MAX];
    int n;
    uint16_t i;
    n = snprintf(buf, sizeof(buf), "len=%u,hex=", (unsigned)len);
    if (n <= 0 || (size_t)n >= sizeof(buf))
    {
        serial_cmd_reply_ng();
        return;
    }
    for (i = 0U; i < len; i++)
    {
        int m = snprintf(buf + n, sizeof(buf) - (size_t)n, "%02X", data[i]);
        if (m <= 0 || (size_t)m >= (sizeof(buf) - (size_t)n))
        {
            serial_cmd_reply_ng();
            return;
        }
        n += m;
    }
    serial_cmd_reply_ok(cmd, buf);
}

void proto_init(UART_HandleTypeDef *huart)
{
    s_uart1 = huart;
    proto_clear();
    s_proto_active = 0U;
    s_proto_monitor_task = NULL;
}

void proto_on_rx_from_isr(uint8_t byte)
{
    uint16_t head = s_rx_head;
    uint16_t next = (uint16_t)((head + 1U) % PROTO_RX_BUF_SIZE);
    if (next == s_rx_tail)
    {
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % PROTO_RX_BUF_SIZE);
    }
    s_rx_buf[head] = byte;
    s_rx_head = next;
}

void proto_cmd_tx(int argc, const char *argv[])
{
    uint8_t tx[PROTO_MAX_TX_BYTES];
    uint16_t tx_len = 0U;
    if (!proto_parse_tx_bytes(argc, argv, 1, tx, &tx_len))
    {
        serial_cmd_reply_ng();
        return;
    }
    if (!proto_send(tx, tx_len))
    {
        serial_cmd_reply_ng();
        return;
    }
    serial_cmd_reply_ok("u1tx", "ok");
}

void proto_cmd_rx(int argc, const char *argv[])
{
    uint8_t rx[PROTO_RX_DUMP_MAX];
    uint32_t req = PROTO_RX_DUMP_DEFAULT;
    uint16_t rx_len;
    if (argc == 2)
    {
        req = strtoul(argv[1], NULL, 10);
        if (req == 0U || req > PROTO_RX_DUMP_MAX)
        {
            serial_cmd_reply_ng();
            return;
        }
    }
    else if (argc != 1)
    {
        serial_cmd_reply_ng();
        return;
    }

    rx_len = proto_pop(rx, (uint16_t)req);
    proto_reply_hex("u1rx", rx, rx_len);
}

void proto_cmd_clr(int argc, const char *argv[])
{
    (void)argv;
    if (argc != 1)
    {
        serial_cmd_reply_ng();
        return;
    }
    proto_clear();
    serial_cmd_reply_ok("u1clr", "ok");
}

void proto_cmd_xfer(int argc, const char *argv[])
{
    uint8_t tx[PROTO_MAX_TX_BYTES];
    uint8_t rx[PROTO_RX_DUMP_MAX];
    uint16_t tx_len = 0U;
    uint16_t rx_len;
    uint32_t wait_ms;
    if (argc < 3)
    {
        serial_cmd_reply_ng();
        return;
    }
    wait_ms = strtoul(argv[1], NULL, 10);
    if (wait_ms > PROTO_MAX_WAIT_MS)
    {
        serial_cmd_reply_ng();
        return;
    }
    if (!proto_parse_tx_bytes(argc, argv, 2, tx, &tx_len))
    {
        serial_cmd_reply_ng();
        return;
    }

    proto_clear();
    if (!proto_send(tx, tx_len))
    {
        serial_cmd_reply_ng();
        return;
    }
    if (wait_ms > 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    rx_len = proto_pop(rx, PROTO_RX_DUMP_MAX);
    proto_reply_hex("u1xfer", rx, rx_len);
}

void proto_enter(void)
{
    if (s_proto_monitor_task == NULL)
    {
        s_proto_monitor_task = osThreadNew(proto_monitor_task, NULL, &s_proto_monitor_task_attr);
        if (s_proto_monitor_task == NULL)
        {
            serial_cmd_send_str("proto:monitor task create failed\r\n");
            return;
        }
    }
    s_proto_active = 1U;
    serial_cmd_send_str("proto:enter transparent mode (type exit to leave)\r\n");
}

void proto_exit(void)
{
    s_proto_active = 0U;
    if (s_proto_monitor_task != NULL)
    {
        (void)osThreadTerminate(s_proto_monitor_task);
        s_proto_monitor_task = NULL;
    }
    serial_cmd_send_str("proto:exit\r\n");
}

uint8_t proto_is_active(void)
{
    return s_proto_active;
}

void proto_poll_monitor(void)
{
    uint8_t rx[PROTO_RX_DUMP_MAX];
    uint16_t rx_len;
    uint16_t i;
    uint8_t has_eol = 0U;

    if (s_proto_active == 0U)
    {
        return;
    }

    rx_len = proto_pop(rx, PROTO_RX_DUMP_MAX);
    if (rx_len == 0U)
    {
        return;
    }

    (void)serial_cmd_send(rx, rx_len);
    for (i = 0U; i < rx_len; i++)
    {
        if (rx[i] == '\r' || rx[i] == '\n')
        {
            has_eol = 1U;
            break;
        }
    }
    if (has_eol == 0U)
    {
        (void)serial_cmd_send((const uint8_t *)"\r\n", 2U);
    }
}

void proto_process_line(const char *line)
{
    uint8_t tx[PROTO_MAX_TX_BYTES];
    uint16_t tx_len = 0U;
    size_t i = 0U;
    size_t n = 0U;
    size_t hex_start = 0U;

    if (line == NULL)
    {
        serial_cmd_reply_ng();
        return;
    }

    n = strlen(line);
    while (i < n && (line[i] == ' ' || line[i] == '\t'))
    {
        i++;
    }
    if (strcmp(&line[i], "exit") == 0)
    {
        proto_exit();
        return;
    }

    if (proto_is_hex_prefix(&line[i], n - i, &hex_start) != 0U)
    {
        if (!proto_parse_inline_hex(&line[i + hex_start], tx, &tx_len))
        {
            serial_cmd_reply_ng();
            return;
        }
    }
    else
    {
        if (n > (size_t)PROTO_MAX_TX_BYTES)
        {
            serial_cmd_reply_ng();
            return;
        }
        memcpy(tx, line, n);
        tx_len = (uint16_t)n;
    }

    if (!proto_send(tx, tx_len))
    {
        serial_cmd_reply_ng();
    }
}
