/**
 * @file    log.c
 * @brief   Logging system implementation with level support
 * 
 * Simplified and optimized for embedded systems
 */

#include "log.h"
#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define LOG_UART_TIMEOUT_MS  100

#define LOG_BUFFER_SIZE          256
#define LOG_TIMESTAMP_SIZE       32
#define LOG_FILE_LINE_SIZE       64

static SemaphoreHandle_t log_mutex = NULL;
static log_output_func_t log_output_func = NULL;

static log_config_t log_config = {
    .level = LOG_LEVEL_INFO,
    .color = LOG_COLOR_NONE,
    .timestamp_enable = true,
    .file_line_enable = false,
    .initialized = false
};

static const char *log_level_strings[] = {
    [LOG_LEVEL_NONE]    = "NONE",
    [LOG_LEVEL_FATAL]   = "FATAL",
    [LOG_LEVEL_ERROR]   = "ERROR",
    [LOG_LEVEL_WARN]    = "WARN",
    [LOG_LEVEL_INFO]    = "INFO",
    [LOG_LEVEL_DEBUG]   = "DEBUG",
    [LOG_LEVEL_VERBOSE] = "VERBOSE"
};

#ifdef LOG_COLOR_ENABLE
static const char *log_color_codes[] = {
    [LOG_LEVEL_NONE]    = "\033[0m",
    [LOG_LEVEL_FATAL]   = "\033[1;31m",
    [LOG_LEVEL_ERROR]   = "\033[0;31m",
    [LOG_LEVEL_WARN]    = "\033[0;33m",
    [LOG_LEVEL_INFO]    = "\033[0;32m",
    [LOG_LEVEL_DEBUG]   = "\033[0;36m",
    [LOG_LEVEL_VERBOSE] = "\033[0;37m"
};
#endif

static void log_lock(void)
{
    if (log_mutex != NULL)
    {
        xSemaphoreTake(log_mutex, portMAX_DELAY);
    }
}

static void log_unlock(void)
{
    if (log_mutex != NULL)
    {
        xSemaphoreGive(log_mutex);
    }
}

static void log_output_string(const char *str, uint16_t len)
{
    if (len == 0)
    {
        return;
    }
    if (log_output_func != NULL)
    {
        log_output_func(str, len);
        return;
    }
    HAL_UART_Transmit(&huart3, (const uint8_t *)str, len, LOG_UART_TIMEOUT_MS);
}

static const char *log_get_filename(const char *filepath)
{
    const char *filename = strrchr(filepath, '/');
    if (filename != NULL)
    {
        return filename + 1;
    }
    filename = strrchr(filepath, '\\');
    if (filename != NULL)
    {
        return filename + 1;
    }
    return filepath;
}

static uint32_t log_get_timestamp_ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    return HAL_GetTick();
}

log_level_t log_get_level(void)
{
    return log_config.level;
}

log_status_t log_set_level(log_level_t level)
{
    if (level >= LOG_LEVEL_MAX)
    {
        return LOG_ERROR_INVALID_PARAM;
    }
    log_config.level = level;
    return LOG_OK;
}

log_status_t log_set_color(log_color_t color)
{
    log_config.color = color;
    return LOG_OK;
}

log_status_t log_set_timestamp(bool enable)
{
    log_config.timestamp_enable = enable;
    return LOG_OK;
}

log_status_t log_set_file_line(bool enable)
{
    log_config.file_line_enable = enable;
    return LOG_OK;
}

log_status_t log_init(log_output_func_t output_func)
{
    if (log_mutex == NULL)
    {
        log_mutex = xSemaphoreCreateMutex();
        if (log_mutex == NULL)
        {
            return LOG_ERROR_INVALID_PARAM;
        }
    }

    log_output_func = output_func;
    log_config.initialized = true;

    return LOG_OK;
}

log_status_t log_deinit(void)
{
    log_config.initialized = false;
    log_output_func = NULL;

    if (log_mutex != NULL)
    {
        vSemaphoreDelete(log_mutex);
        log_mutex = NULL;
    }

    return LOG_OK;
}

void log_output(log_level_t level, const char *file, uint16_t line, const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    int pos = 0;
    va_list args;

    if (!log_config.initialized || level == LOG_LEVEL_NONE || level >= LOG_LEVEL_MAX)
    {
        return;
    }

    if (level > log_config.level)
    {
        return;
    }

    log_lock();

#ifdef LOG_COLOR_ENABLE
    if (log_config.color == LOG_COLOR_ENABLE)
    {
        const char *color_code = log_color_codes[level];
        int color_len = strlen(color_code);
        if (pos + color_len < LOG_BUFFER_SIZE)
        {
            memcpy(buffer + pos, color_code, color_len);
            pos += color_len;
        }
    }
#endif

    if (log_config.timestamp_enable)
    {
        uint32_t timestamp = log_get_timestamp_ms();
        int ts_len = snprintf(buffer + pos, LOG_BUFFER_SIZE - pos, "[%05lu] ", timestamp);
        if (ts_len > 0 && pos + ts_len < LOG_BUFFER_SIZE)
        {
            pos += ts_len;
        }
    }

    const char *level_str = log_level_strings[level];
    int level_len = snprintf(buffer + pos, LOG_BUFFER_SIZE - pos, "[%s] ", level_str);
    if (level_len > 0 && pos + level_len < LOG_BUFFER_SIZE)
    {
        pos += level_len;
    }

    if (log_config.file_line_enable && file != NULL)
    {
        const char *filename = log_get_filename(file);
        int fl_len = snprintf(buffer + pos, LOG_BUFFER_SIZE - pos, "%s:%u ", filename, line);
        if (fl_len > 0 && pos + fl_len < LOG_BUFFER_SIZE)
        {
            pos += fl_len;
        }
    }

    va_start(args, fmt);
    int fmt_len = vsnprintf(buffer + pos, LOG_BUFFER_SIZE - pos, fmt, args);
    va_end(args);

    if (fmt_len > 0 && pos + fmt_len < LOG_BUFFER_SIZE)
    {
        pos += fmt_len;
    }

    if (pos < LOG_BUFFER_SIZE - 2)
    {
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }
    else
    {
        pos = LOG_BUFFER_SIZE - 2;
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }

#ifdef LOG_COLOR_ENABLE
    if (log_config.color == LOG_COLOR_ENABLE)
    {
        const char *reset_code = log_color_codes[LOG_LEVEL_NONE];
        int reset_len = strlen(reset_code);
        if (pos + reset_len < LOG_BUFFER_SIZE)
        {
            memcpy(buffer + pos, reset_code, reset_len);
            pos += reset_len;
        }
    }
#endif

    log_output_string(buffer, pos);
    log_unlock();
}
