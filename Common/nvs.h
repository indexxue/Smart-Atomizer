/**
 * @file    nvs.h
 * @brief   Non-Volatile Storage driver for STM32F103RCT6
 *
 * Key-value storage in internal Flash with wear leveling across two logical sectors.
 * Keys are C strings up to (NVS_KEY_MAX_LEN - 1) characters plus a terminating NUL.
 * nvs_delete() removes a key by compacting into the other sector (erase + copy); cost
 * is similar to a full compaction, not an in-place erase of one entry.
 *
 * Layout: 32KB at 0x08038000, two 16KB logical sectors. If you previously used 8KB at
 * 0x0803E000, run nvs_factory_reset() once after upgrade (or migrate old data).
 *
 * HW / app / factory version strings (NVS_*_VERSION_STRING) are compile-time only;
 * use nvs_hw_version_get, nvs_app_version_get, nvs_factory_version_get to read them.
 */

#ifndef __NVS_H
#define __NVS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NVS_KEY_MAX_LEN           16
#define NVS_VALUE_MAX_LEN         128
#define NVS_ENTRY_HEADER_SIZE     (NVS_KEY_MAX_LEN + 4)
#define NVS_ENTRY_MAX_SIZE        (NVS_ENTRY_HEADER_SIZE + NVS_VALUE_MAX_LEN)

#define NVS_MAGIC                 0x4E565320
#define NVS_VERSION               0x0002

#define NVS_HEADER_CRC_SIZE       4
#define NVS_ENTRY_CRC_SIZE        4

#define NVS_FLASH_START_ADDR      0x08038000
#define NVS_FLASH_SIZE            (32 * 1024)
#define NVS_SECTOR_SIZE           (16 * 1024)
#define NVS_SECTOR_COUNT          2

#define NVS_MAC_SIZE              8
#define NVS_SN_SIZE               16
#define NVS_HW_VERSION_SIZE       16
#define NVS_APP_VERSION_SIZE      24
#define NVS_FACTORY_VERSION_SIZE  24
#define NVS_REGION_SIZE           8

#define NVS_DEFAULT_MAC            {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}
#define NVS_DEFAULT_SN             "T90M0123456789"
#define NVS_DEFAULT_REGION         "US915"

#define NVS_HW_VERSION_STRING      "1.0.0"
#define NVS_APP_VERSION_STRING     "1.0.0"
#define NVS_FACTORY_VERSION_STRING "1.0.0"
#define NVS_DEFAULT_DEVICE_TYPE    1
#define NVS_DEFAULT_RUN_TIME       0U

typedef enum
{
    NVS_OK = 0,
    NVS_ERROR_INVALID_PARAM,
    NVS_ERROR_NOT_INITIALIZED,
    NVS_ERROR_NOT_FOUND,
    NVS_ERROR_NO_SPACE,
    NVS_ERROR_FLASH_ERASE,
    NVS_ERROR_FLASH_WRITE,
    NVS_ERROR_INVALID_STATE,
    NVS_ERROR_BUSY,
    NVS_ERROR_CRC
} nvs_status_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint32_t crc32;
} __attribute__((packed)) nvs_header_t;

typedef struct
{
    char key[NVS_KEY_MAX_LEN];
    uint32_t value_len;
} __attribute__((packed)) nvs_entry_header_t;

typedef struct
{
    bool initialized;
    uint32_t active_sector;
    uint32_t write_offset;
    nvs_header_t header;
} nvs_handle_t;

nvs_status_t nvs_set(nvs_handle_t *nvs, const char *key, const void *value, size_t value_len);
nvs_status_t nvs_get(nvs_handle_t *nvs, const char *key, void *value, size_t *value_len);
/** Erase other sector, copy all entries except @a key, then erase old sector (STM32F1 cannot rewrite Flash to 0xFF in place). */
nvs_status_t nvs_delete(nvs_handle_t *nvs, const char *key);

extern nvs_handle_t g_nvs_handle;

void nvs_init(void);
void nvs_factory_reset(void);

void nvs_reboot_count_set(uint32_t count);
uint32_t nvs_reboot_count_get(void);
void nvs_frame_count_set(uint32_t count);
uint32_t nvs_frame_count_get(void);
void nvs_heartbeat_seq_set(uint32_t seq);
uint32_t nvs_heartbeat_seq_get(void);

bool nvs_mac_set(const uint8_t *mac);
bool nvs_mac_get(uint8_t *mac);
bool nvs_sn_set(const char *sn);
bool nvs_sn_get(char *sn);
bool nvs_hw_version_get(char *version);
bool nvs_app_version_get(char *version);
bool nvs_factory_version_get(char *version);
bool nvs_region_set(const char *region);
bool nvs_region_get(char *region);
void nvs_device_type_set(uint8_t type);
uint8_t nvs_device_type_get(void);

void nvs_run_time_set(uint32_t sec);
uint32_t nvs_run_time_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __NVS_H */
