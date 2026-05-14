/**
 * @file    log.h
 * @brief   Logging system with level support for embedded systems
 */

#ifndef __LOG_H
#define __LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

typedef enum
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE,
    LOG_LEVEL_MAX
} log_level_t;

typedef enum
{
    LOG_COLOR_NONE = 0,
    LOG_COLOR_ENABLE
} log_color_t;

typedef enum
{
    LOG_OK = 0,
    LOG_ERROR_INVALID_PARAM,
    LOG_ERROR_NOT_INITIALIZED
} log_status_t;

typedef struct
{
    log_level_t level;
    log_color_t color;
    bool timestamp_enable;
    bool file_line_enable;
    bool initialized;
} log_config_t;

typedef void (*log_output_func_t)(const char *str, uint16_t len);

log_level_t log_get_level(void);
log_status_t log_set_level(log_level_t level);
log_status_t log_set_color(log_color_t color);
log_status_t log_set_timestamp(bool enable);
log_status_t log_set_file_line(bool enable);
log_status_t log_init(log_output_func_t output_func);
log_status_t log_deinit(void);
void log_output(log_level_t level, const char *file, uint16_t line, const char *fmt, ...);

/*
 * Define LOG_DETERMINISTIC_BUILD in release builds to avoid binary drift
 * caused by source line movement (for example, adding/removing comments).
 */
#if defined(LOG_DETERMINISTIC_BUILD)
#define LOG_CALLSITE_LINE ((uint16_t)0U)
#else
#define LOG_CALLSITE_LINE ((uint16_t)__LINE__)
#endif

#define LOG_FATAL(...)   log_output(LOG_LEVEL_FATAL, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)
#define LOG_ERROR(...)   log_output(LOG_LEVEL_ERROR, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)
#define LOG_WARN(...)    log_output(LOG_LEVEL_WARN, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)
#define LOG_INFO(...)    log_output(LOG_LEVEL_INFO, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)
#define LOG_DEBUG(...)   log_output(LOG_LEVEL_DEBUG, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)
#define LOG_VERBOSE(...) log_output(LOG_LEVEL_VERBOSE, __FILE__, LOG_CALLSITE_LINE, __VA_ARGS__)

#ifdef LOG_DISABLE_DEBUG
#define LOG_DEBUG(fmt, ...)   ((void)0)
#endif

#ifdef LOG_DISABLE_VERBOSE
#define LOG_VERBOSE(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H */
