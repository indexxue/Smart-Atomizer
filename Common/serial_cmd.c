/**
 * @file    serial_cmd.c
 * @brief   UART command framework with registration and unified reply
 *
 * Used from Factory flow (e.g. Factory/start.c). Application firmware should not
 * call serial_cmd_init / register_defaults so USART1 stays with ty_link (see Core/Src/freertos.c).
 */

#include "serial_cmd.h"
#include "boot_slot.h"
#include "usart.h"
#include "i2c.h"
#include "nvs.h"
#include "led_scene.h"
#include "button.h"
#include "log.h"
#include "proto.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static UART_HandleTypeDef *s_huart = NULL;
static serial_cmd_entry_t s_cmd_table[SERIAL_CMD_MAX_COMMANDS];
static int s_cmd_count = 0;
static SemaphoreHandle_t s_uart_mutex = NULL;

static UART_HandleTypeDef *get_huart(void)
{
    return s_huart != NULL ? s_huart : &huart3;
}

void serial_cmd_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_cmd_count = 0;
    memset(s_cmd_table, 0, sizeof(s_cmd_table));
    proto_init(&huart1);
    if (s_uart_mutex == NULL)
    {
        s_uart_mutex = xSemaphoreCreateMutex();
    }
}

int serial_cmd_register(const char *name, serial_cmd_handler_t handler, const char *help)
{
    if (name == NULL || handler == NULL || s_cmd_count >= SERIAL_CMD_MAX_COMMANDS)
    {
        return -1;
    }
    size_t nlen = strlen(name);
    if (nlen == 0 || nlen >= SERIAL_CMD_NAME_MAX)
    {
        return -1;
    }
    strncpy(s_cmd_table[s_cmd_count].name, name, SERIAL_CMD_NAME_MAX - 1);
    s_cmd_table[s_cmd_count].name[SERIAL_CMD_NAME_MAX - 1] = '\0';
    s_cmd_table[s_cmd_count].handler = handler;
    if (help != NULL)
    {
        strncpy(s_cmd_table[s_cmd_count].help, help, SERIAL_CMD_HELP_MAX - 1);
        s_cmd_table[s_cmd_count].help[SERIAL_CMD_HELP_MAX - 1] = '\0';
    }
    else
    {
        s_cmd_table[s_cmd_count].help[0] = '\0';
    }
    s_cmd_count++;
    return 0;
}

void serial_cmd_reply_ok(const char *cmd, const char *value)
{
    char buf[SERIAL_CMD_LINE_MAX + 16];
    int n = snprintf(buf, sizeof(buf), "%s:%s\r\n", cmd ? cmd : "", value ? value : "");
    if (n > 0 && (size_t)n < sizeof(buf))
    {
        serial_cmd_send((const uint8_t *)buf, (uint16_t)n);
    }
}

void serial_cmd_reply_ng(void)
{
    serial_cmd_send_str("ng\r\n");
}

bool serial_cmd_send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        return false;
    }
    if (s_uart_mutex != NULL)
    {
        if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
        {
            return false;
        }
    }
    HAL_StatusTypeDef ok = HAL_UART_Transmit(get_huart(), (uint8_t *)data, len, SERIAL_CMD_TX_TIMEOUT_MS);
    if (s_uart_mutex != NULL)
    {
        xSemaphoreGive(s_uart_mutex);
    }
    return (ok == HAL_OK);
}

void serial_cmd_uart_lock(void)
{
    if (s_uart_mutex != NULL)
    {
        xSemaphoreTake(s_uart_mutex, portMAX_DELAY);
    }
}

void serial_cmd_uart_unlock(void)
{
    if (s_uart_mutex != NULL)
    {
        xSemaphoreGive(s_uart_mutex);
    }
}

bool serial_cmd_send_str(const char *str)
{
    if (str == NULL)
    {
        return false;
    }
    uint16_t len = (uint16_t)strlen(str);
    return len == 0 ? true : serial_cmd_send((const uint8_t *)str, len);
}

static void trim_and_tokenize(char *line, const char *argv[], int *argc, int max_argc)
{
    *argc = 0;
    while (*line == ' ' || *line == '\t')
    {
        line++;
    }
    for (; *line != '\0' && *argc < max_argc; )
    {
        argv[*argc] = line;
        (*argc)++;
        while (*line != '\0'
               && *line != ' '
               && *line != '\t'
               && *line != '\r'
               && *line != '\n')
        {
            *line = (char)tolower((unsigned char)*line);
            line++;
        }
        if (*line == '\0')
        {
            break;
        }
        *line = '\0';
        line++;
        while (*line == ' ' || *line == '\t')
        {
            line++;
        }
    }
}

static void do_help(void)
{
    char buf[SERIAL_CMD_STATUS_BUF_SIZE];
    int n = snprintf(buf, sizeof(buf), "help: list commands and usage\r\n");
    if (n > 0 && (size_t)n < sizeof(buf))
    {
        serial_cmd_send((const uint8_t *)buf, (uint16_t)n);
    }

    for (int i = 0; i < s_cmd_count; i++)
    {
        const char *help = s_cmd_table[i].help;
        if (help[0] == '\0')
        {
            help = "";
        }
        n = snprintf(buf, sizeof(buf), "%s: %s\r\n", s_cmd_table[i].name, help);
        if (n > 0 && (size_t)n < sizeof(buf))
        {
            serial_cmd_send((const uint8_t *)buf, (uint16_t)n);
        }
    }
}

void serial_cmd_process_line(const char *line)
{
    if (line == NULL)
    {
        serial_cmd_reply_ng();
        return;
    }
    char buf[SERIAL_CMD_LINE_MAX];
    size_t len = strlen(line);
    if (len >= sizeof(buf))
    {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == '\r' || buf[i] == '\n')
        {
            buf[i] = '\0';
            break;
        }
    }
    {
        char *space_pos = strchr(buf, ' ');
        char *tab_pos = strchr(buf, '\t');
        char *sep = strchr(buf, ':');
        char *first_blank = space_pos;
        if (first_blank == NULL || (tab_pos != NULL && tab_pos < first_blank))
        {
            first_blank = tab_pos;
        }
        if (sep != NULL && (first_blank == NULL || sep < first_blank))
        {
            *sep = ' ';
        }
    }
    const char *argv[SERIAL_CMD_MAX_ARGC];
    int argc = 0;
    trim_and_tokenize(buf, argv, &argc, SERIAL_CMD_MAX_ARGC);
    if (argc == 0)
    {
        serial_cmd_reply_ng();
        return;
    }
    if (strcmp(argv[0], "help") == 0)
    {
        do_help();
        return;
    }
    for (int i = 0; i < s_cmd_count; i++)
    {
        if (strcmp(argv[0], s_cmd_table[i].name) == 0)
        {
            s_cmd_table[i].handler(argc, argv);
            return;
        }
    }
    serial_cmd_reply_ng();
}

static void cmd_sn(int argc, const char *argv[])
{
    if (argc == 1)
    {
        char sn[NVS_SN_SIZE];
        if (nvs_sn_get(sn))
        {
            serial_cmd_reply_ok("sn", sn);
        }
        else
        {
            serial_cmd_reply_ng();
        }
        return;
    }

    if (argc == 2)
    {
        const char *value = argv[1];
        size_t len = strlen(value);
        if (len >= 10U && len <= 12U)
        {
            if (nvs_sn_set(value))
            {
                serial_cmd_reply_ok("sn", "set");
                return;
            }
        }
        serial_cmd_reply_ng();
        return;
    }

    serial_cmd_reply_ng();
}

static void cmd_devtype(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)nvs_device_type_get());
    serial_cmd_reply_ok("devtype", buf);
}

static void cmd_mac(int argc, const char *argv[])
{
    if (argc == 1)
    {
        uint8_t mac[NVS_MAC_SIZE];
        char buf[32];
        if (nvs_mac_get(mac))
        {
            snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
            serial_cmd_reply_ok("mac", buf);
        }
        else
        {
            serial_cmd_reply_ng();
        }
        return;
    }

    if (argc == 2)
    {
        const char *value = argv[1];
        size_t len = strlen(value);
        if (len == NVS_MAC_SIZE)
        {
            uint8_t mac[NVS_MAC_SIZE];
            bool ok = true;
            for (size_t i = 0; i < NVS_MAC_SIZE; i++)
            {
                if (value[i] < '0' || value[i] > '9')
                {
                    ok = false;
                    break;
                }
                mac[i] = (uint8_t)(value[i] - '0');
            }
            if (ok && nvs_mac_set(mac))
            {
                serial_cmd_reply_ok("mac", "set");
                return;
            }
        }
        serial_cmd_reply_ng();
        return;
    }

    serial_cmd_reply_ng();
}

static void cmd_led(int argc, const char *argv[])
{
    if (argc < 3)
    {
        serial_cmd_reply_ng();
        return;
    }
    bool all = false;
    led_scene_led_e led = LED_SCENE_LED_MAX_NUM;
    if (strcmp(argv[1], "r") == 0)
    {
        led = LED_SCENE_LED_0;
    }
    else if (strcmp(argv[1], "b") == 0)
    {
        led = LED_SCENE_LED_1;
    }
    else if (strcmp(argv[1], "all") == 0)
    {
        all = true;
    }
    else
    {
        serial_cmd_reply_ng();
        return;
    }
    bool on = false;
    if (strcmp(argv[2], "on") == 0)
    {
        on = true;
    }
    else if (strcmp(argv[2], "off") == 0)
    {
        on = false;
    }
    else
    {
        serial_cmd_reply_ng();
        return;
    }
    if (all)
    {
        led_scene_led_direct_set(LED_SCENE_LED_0, on);
        led_scene_led_direct_set(LED_SCENE_LED_1, on);
    }
    else
    {
        led_scene_led_direct_set(led, on);
    }
    serial_cmd_reply_ok("led", "ok");
}

static void cmd_reboot(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    serial_cmd_reply_ok("reboot", "now");
    NVIC_SystemReset();
}

static void cmd_ftmexit(int argc, const char *argv[])
{
    (void)argv;
    if (argc != 1)
    {
        serial_cmd_reply_ng();
        return;
    }
    if (!boot_slot_request_app_a())
    {
        serial_cmd_reply_ng();
        return;
    }
    serial_cmd_reply_ok("ftmexit", "now");
    boot_slot_system_reset();
}

static void cmd_log(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    LOG_INFO("[CMD] uart3 log test ok");

    serial_cmd_reply_ok("log", "ok");
}

static void cmd_i2c(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    char buf[SERIAL_CMD_STATUS_BUF_SIZE];
    int n = 0;

    for (uint8_t addr = 1; addr < 0x7F; addr++)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 10) == HAL_OK)
        {
            if (n == 0)
            {
                n += snprintf(buf + n, sizeof(buf) - (size_t)n, "0x%02X", addr);
            }
            else
            {
                n += snprintf(buf + n, sizeof(buf) - (size_t)n, ",0x%02X", addr);
            }
        }
    }

    if (n == 0)
    {
        snprintf(buf, sizeof(buf), "none");
    }

    serial_cmd_reply_ok("i2c", buf);
}

static void cmd_btn(int argc, const char *argv[])
{
    if (argc != 2)
    {
        serial_cmd_reply_ng();
        return;
    }

    btn_id_e id = BTN_ID_MAX_NUMBER;
    if (strcmp(argv[1], "mode") == 0 || strcmp(argv[1], "wake") == 0)
    {
        id = BTN_ID_MODE;
    }
    else if (strcmp(argv[1], "up") == 0 || strcmp(argv[1], "inc") == 0)
    {
        id = BTN_ID_NAV_UP;
    }
    else if (strcmp(argv[1], "down") == 0 || strcmp(argv[1], "dec") == 0)
    {
        id = BTN_ID_NAV_DOWN;
    }
    else
    {
        serial_cmd_reply_ng();
        return;
    }

    uint8_t level = button_get_level(id);
    if (level > 1U)
    {
        serial_cmd_reply_ng();
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%u", (unsigned)level);
    serial_cmd_reply_ok("btn", buf);
}

static void cmd_proto(int argc, const char *argv[])
{
    (void)argv;
    if (argc != 1)
    {
        serial_cmd_reply_ng();
        return;
    }
    proto_enter();
}

static void cmd_u1tx(int argc, const char *argv[])
{
    proto_cmd_tx(argc, argv);
}

static void cmd_u1rx(int argc, const char *argv[])
{
    proto_cmd_rx(argc, argv);
}

static void cmd_u1clr(int argc, const char *argv[])
{
    proto_cmd_clr(argc, argv);
}

static void cmd_u1xfer(int argc, const char *argv[])
{
    proto_cmd_xfer(argc, argv);
}

void serial_cmd_register_defaults(void)
{
    serial_cmd_register("sn", cmd_sn, "usage: sn; sn:xxxxxxxxxx");
    serial_cmd_register("devtype", cmd_devtype, "usage: devtype");
    serial_cmd_register("mac", cmd_mac, "usage: mac; mac:xxxxxxxx");
    serial_cmd_register("led", cmd_led, "usage: led r on|off; led b on|off; led all on|off");
    serial_cmd_register("reboot", cmd_reboot, "usage: reboot");
    serial_cmd_register("ftmexit", cmd_ftmexit, "usage: ftmexit");
    serial_cmd_register("log", cmd_log, "usage: log");
    serial_cmd_register("i2c", cmd_i2c, "usage: i2c");
    serial_cmd_register("btn", cmd_btn, "usage: btn wake|up|down|left|right");
    serial_cmd_register("proto", cmd_proto, "enter proto mode; use exit in proto mode");
    serial_cmd_register("u1tx", cmd_u1tx, "usage: u1tx <hh> <hh> ...");
    serial_cmd_register("u1rx", cmd_u1rx, "usage: u1rx [n]");
    serial_cmd_register("u1clr", cmd_u1clr, "usage: u1clr");
    serial_cmd_register("u1xfer", cmd_u1xfer, "usage: u1xfer <wait_ms> <hh> ...");
}
