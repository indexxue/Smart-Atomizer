/**
 * @file    nvs.c
 * @brief   Non-Volatile Storage driver implementation for STM32F103RCT6
 */

#include "nvs.h"
#include "main.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "log.h"
#include "boot_slot.h"

#define NVS_KEY_REBOOT_COUNT      "rcount"
#define NVS_KEY_FRAME_COUNT       "fcount"
#define NVS_KEY_HEARTBEAT_SEQ     "hseq"
#define NVS_KEY_MAC               "mac"
#define NVS_KEY_SN                "sn"
#define NVS_KEY_REGION            "region"
#define NVS_KEY_DEVICE_TYPE       "dtype"
#define NVS_KEY_RUN_TIME          "rtime"

nvs_handle_t g_nvs_handle;
static bool g_nvs_initialized = false;

static bool nvs_copy_version_string(char *buf, size_t buf_size, const char *src)
{
    if (buf == NULL || buf_size == 0U || src == NULL)
    {
        return false;
    }
    (void)snprintf(buf, buf_size, "%s", src);
    return true;
}

static uint32_t nvs_crc32_table[256];
static uint8_t nvs_crc32_table_init = 0;

static void nvs_crc32_init_table(void)
{
    uint32_t c;
    unsigned int n, k;
    for (n = 0; n < 256u; n++)
    {
        c = (uint32_t)n;
        for (k = 0; k < 8u; k++)
        {
            if (c & 1u)
                c = 0xEDB88320u ^ (c >> 1u);
            else
                c = c >> 1u;
        }
        nvs_crc32_table[n] = c;
    }
    nvs_crc32_table_init = 1;
}

static uint32_t nvs_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    size_t i;
    if (!nvs_crc32_table_init)
    {
        nvs_crc32_init_table();
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (i = 0; i < len; i++)
    {
        crc = nvs_crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t nvs_crc32(const uint8_t *data, size_t len)
{
    return nvs_crc32_update(0u, data, len);
}

static inline uint32_t nvs_get_sector_address(uint32_t sector_index)
{
    return NVS_FLASH_START_ADDR + (sector_index * NVS_SECTOR_SIZE);
}

static inline uint16_t nvs_get_half_word(const uint8_t *data, size_t index, size_t len)
{
    if (index + 1 < len)
    {
        return data[index] | (data[index + 1] << 8);
    }
    return data[index] | 0xFF00;
}

static inline bool nvs_wait_flash_ready(void)
{
    uint32_t timeout = 1000000u;
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) && (timeout-- > 0u))
    {
    }
    if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY))
    {
        return false;
    }
    if (__HAL_FLASH_GET_FLAG(FLASH_FLAG_PGERR) || __HAL_FLASH_GET_FLAG(FLASH_FLAG_WRPERR))
    {
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
        return false;
    }
    return true;
}

#define NVS_STM32F1_PAGE_SIZE       0x800u

static nvs_status_t nvs_erase_sector(uint32_t sector_addr)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return NVS_ERROR_FLASH_ERASE;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = sector_addr;
    erase.NbPages = (uint32_t)(NVS_SECTOR_SIZE / NVS_STM32F1_PAGE_SIZE);

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        LOG_ERROR("NVS: Sector erase error at 0x%08lX", (unsigned long)sector_addr);
        HAL_FLASH_Lock();
        return NVS_ERROR_FLASH_ERASE;
    }

    HAL_FLASH_Lock();
    return NVS_OK;
}

static nvs_status_t nvs_write_flash(uint32_t addr, const uint8_t *data, size_t len)
{
    if ((addr & 0x1u) != 0u)
    {
        LOG_ERROR("NVS: Address not half-word aligned: 0x%08lX", (unsigned long)addr);
        return NVS_ERROR_FLASH_WRITE;
    }

    if (data == NULL || len == 0)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return NVS_ERROR_FLASH_WRITE;
    }

    for (size_t i = 0; i < len; i += 2)
    {
        uint16_t current_value = *(volatile uint16_t *)(addr + i);
        uint16_t half_word = nvs_get_half_word(data, i, len);

        if (current_value != 0xFFFFu && current_value != half_word)
        {
            return NVS_ERROR_FLASH_WRITE;
        }

        if (current_value == half_word)
        {
            continue;
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, (uint32_t)half_word) != HAL_OK)
        {
            LOG_ERROR("NVS: Flash write error at 0x%08lX", (unsigned long)(addr + i));
            return NVS_ERROR_FLASH_WRITE;
        }

        if (!nvs_wait_flash_ready())
        {
            HAL_FLASH_Lock();
            return NVS_ERROR_FLASH_WRITE;
        }
    }

    HAL_FLASH_Lock();
    return NVS_OK;
}

static nvs_status_t nvs_read_header(nvs_handle_t *nvs, uint32_t sector_addr, nvs_header_t *header)
{
    if (nvs == NULL || header == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    memcpy(header, (const void *)sector_addr, sizeof(nvs_header_t));

    if (header->magic != NVS_MAGIC)
    {
        return NVS_ERROR_INVALID_STATE;
    }

    if (header->version >= 0x0002u)
    {
        uint32_t computed = nvs_crc32((const uint8_t *)&header->version, sizeof(header->version) + sizeof(header->entry_count));
        if (computed != header->crc32)
        {
            return NVS_ERROR_CRC;
        }
    }

    return NVS_OK;
}

static nvs_status_t nvs_write_header(nvs_handle_t *nvs, uint32_t sector_addr, uint16_t entry_count)
{
    nvs_header_t hdr;

    if (nvs == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    hdr.magic = NVS_MAGIC;
    hdr.version = NVS_VERSION;
    hdr.entry_count = entry_count;
    hdr.crc32 = nvs_crc32((const uint8_t *)&hdr.version, sizeof(hdr.version) + sizeof(hdr.entry_count));

    return nvs_write_flash(sector_addr, (const uint8_t *)&hdr, sizeof(nvs_header_t));
}

static size_t nvs_entry_size_with_crc(uint32_t value_len)
{
    size_t sz = NVS_ENTRY_HEADER_SIZE + value_len + NVS_ENTRY_CRC_SIZE;
    if (sz & 0x1u)
    {
        sz++;
    }
    return sz;
}

static nvs_status_t nvs_find_entry(nvs_handle_t *nvs, const char *key,
                                    uint32_t *entry_addr, uint8_t *value_buf,
                                    uint32_t *value_len)
{
    if (nvs == NULL || key == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    uint32_t sector_addr = nvs_get_sector_address(nvs->active_sector);
    nvs_header_t header;

    if (nvs_read_header(nvs, sector_addr, &header) != NVS_OK)
    {
        return NVS_ERROR_INVALID_STATE;
    }

    uint32_t offset = sizeof(nvs_header_t);
    const uint32_t sector_end = sector_addr + NVS_SECTOR_SIZE;
    uint32_t write_end = sector_addr + nvs->write_offset;
    if (write_end > sector_end || nvs->write_offset < sizeof(nvs_header_t))
    {
        write_end = sector_end;
    }
    uint32_t found_addr = 0;
    uint32_t found_value_len = 0;
    const int use_crc = (header.version >= 0x0002u) ? 1 : 0;

    while (offset < write_end && offset < sector_end)
    {
        if (offset + NVS_ENTRY_HEADER_SIZE > sector_end)
        {
            break;
        }

        nvs_entry_header_t entry_header;
        memcpy(&entry_header, (const void *)(sector_addr + offset), sizeof(nvs_entry_header_t));

        if (entry_header.key[0] == 0xFF)
        {
            if (entry_header.value_len > NVS_VALUE_MAX_LEN)
            {
                break;
            }
            size_t entry_size = use_crc ? nvs_entry_size_with_crc(entry_header.value_len)
                                        : (NVS_ENTRY_HEADER_SIZE + entry_header.value_len + ((NVS_ENTRY_HEADER_SIZE + entry_header.value_len) & 1u));
            if (entry_size == 0 || offset + entry_size > sector_end)
            {
                break;
            }
            offset += entry_size;
            continue;
        }

        if (entry_header.value_len == 0 || entry_header.value_len > NVS_VALUE_MAX_LEN)
        {
            break;
        }

        size_t entry_size = use_crc ? nvs_entry_size_with_crc(entry_header.value_len)
                                    : (NVS_ENTRY_HEADER_SIZE + entry_header.value_len + ((NVS_ENTRY_HEADER_SIZE + entry_header.value_len) & 1u));

        if (offset + entry_size > sector_end)
        {
            break;
        }

        if (strncmp(entry_header.key, key, NVS_KEY_MAX_LEN) == 0)
        {
            if (use_crc)
            {
                uint32_t stored_crc = *(const uint32_t *)(sector_addr + offset + NVS_ENTRY_HEADER_SIZE + entry_header.value_len);
                uint32_t computed = nvs_crc32((const uint8_t *)(sector_addr + offset), NVS_ENTRY_HEADER_SIZE + entry_header.value_len);
                if (computed != stored_crc)
                {
                    offset += entry_size;
                    continue;
                }
            }
            found_addr = sector_addr + offset;
            found_value_len = entry_header.value_len;
        }

        if (entry_size == 0)
        {
            break;
        }
        offset += entry_size;
    }

    if (found_addr != 0)
    {
        if (value_buf != NULL && value_len != NULL)
        {
            if (*value_len < found_value_len)
            {
                *value_len = found_value_len;
                return NVS_ERROR_INVALID_PARAM;
            }
            memcpy(value_buf, (const void *)(found_addr + NVS_ENTRY_HEADER_SIZE), found_value_len);
            *value_len = found_value_len;
        }

        if (entry_addr != NULL)
        {
            *entry_addr = found_addr;
        }

        return NVS_OK;
    }

    return NVS_ERROR_NOT_FOUND;
}

static nvs_status_t nvs_compact_to_other_sector(nvs_handle_t *nvs, const char *drop_key)
{
    if (nvs == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    uint32_t old_sector = nvs->active_sector;
    uint32_t new_sector = (old_sector + 1) % NVS_SECTOR_COUNT;

    uint32_t old_sector_addr = nvs_get_sector_address(old_sector);
    uint32_t new_sector_addr = nvs_get_sector_address(new_sector);

    nvs_header_t old_header;
    if (nvs_read_header(nvs, old_sector_addr, &old_header) != NVS_OK)
    {
        return NVS_ERROR_INVALID_STATE;
    }

    if (nvs_erase_sector(new_sector_addr) != NVS_OK)
    {
        return NVS_ERROR_FLASH_ERASE;
    }

    nvs_header_t new_header;
    new_header.magic = NVS_MAGIC;
    new_header.version = NVS_VERSION;
    new_header.entry_count = 0;

    const int old_use_crc = (old_header.version >= 0x0002u) ? 1 : 0;
    uint32_t new_offset = sizeof(nvs_header_t);
    uint32_t old_offset = sizeof(nvs_header_t);
    const uint32_t sector_end = old_sector_addr + NVS_SECTOR_SIZE;
    uint8_t entry_buf[NVS_ENTRY_HEADER_SIZE + NVS_VALUE_MAX_LEN + NVS_ENTRY_CRC_SIZE];

    uint32_t write_end = old_sector_addr + nvs->write_offset;
    if (write_end > sector_end)
    {
        write_end = sector_end;
    }

    while (old_offset + sizeof(nvs_entry_header_t) <= write_end && old_offset < sector_end)
    {
        nvs_entry_header_t entry_header;
        memcpy(&entry_header, (const void *)(old_sector_addr + old_offset), sizeof(nvs_entry_header_t));

        if (entry_header.key[0] == 0xFF)
        {
            break;
        }

        if (entry_header.value_len == 0 || entry_header.value_len > NVS_VALUE_MAX_LEN)
        {
            break;
        }

        size_t old_entry_size = old_use_crc ? nvs_entry_size_with_crc(entry_header.value_len)
                                            : (NVS_ENTRY_HEADER_SIZE + entry_header.value_len + ((NVS_ENTRY_HEADER_SIZE + entry_header.value_len) & 1u));

        if (old_offset + old_entry_size > sector_end)
        {
            break;
        }

        size_t new_entry_size = nvs_entry_size_with_crc(entry_header.value_len);
        if (new_offset + new_entry_size > new_sector_addr + NVS_SECTOR_SIZE)
        {
            break;
        }

        uint32_t check_offset = old_offset + old_entry_size;
        bool is_last = true;

        while (check_offset + sizeof(nvs_entry_header_t) <= write_end && check_offset < sector_end)
        {
            nvs_entry_header_t check_header;
            memcpy(&check_header, (const void *)(old_sector_addr + check_offset), sizeof(nvs_entry_header_t));

            if (check_header.key[0] == 0xFF ||
                check_header.value_len == 0 ||
                check_header.value_len > NVS_VALUE_MAX_LEN)
            {
                break;
            }

            if (strncmp(entry_header.key, check_header.key, NVS_KEY_MAX_LEN) == 0)
            {
                is_last = false;
                break;
            }

            size_t check_entry_size = old_use_crc ? nvs_entry_size_with_crc(check_header.value_len)
                                                  : (NVS_ENTRY_HEADER_SIZE + check_header.value_len + ((NVS_ENTRY_HEADER_SIZE + check_header.value_len) & 1u));
            check_offset += check_entry_size;
        }

        if (is_last)
        {
            bool skip = (drop_key != NULL &&
                         strncmp(entry_header.key, drop_key, NVS_KEY_MAX_LEN) == 0);
            if (!skip)
            {
                memcpy(entry_buf, (const void *)(old_sector_addr + old_offset),
                       NVS_ENTRY_HEADER_SIZE + entry_header.value_len);
                {
                    uint32_t crc = nvs_crc32(entry_buf, NVS_ENTRY_HEADER_SIZE + entry_header.value_len);
                    memcpy(entry_buf + NVS_ENTRY_HEADER_SIZE + entry_header.value_len, &crc, NVS_ENTRY_CRC_SIZE);
                }
                if (new_entry_size > NVS_ENTRY_HEADER_SIZE + entry_header.value_len + NVS_ENTRY_CRC_SIZE)
                {
                    entry_buf[new_entry_size - 1] = 0xFF;
                }

                if (nvs_write_flash(new_sector_addr + new_offset, entry_buf, new_entry_size) != NVS_OK)
                {
                    return NVS_ERROR_FLASH_WRITE;
                }

                new_offset += new_entry_size;
                new_header.entry_count++;
            }
        }

        old_offset += old_entry_size;
    }

    new_header.crc32 = nvs_crc32((const uint8_t *)&new_header.version, sizeof(new_header.version) + sizeof(new_header.entry_count));
    nvs_status_t header_status = nvs_write_flash(new_sector_addr, (const uint8_t *)&new_header, sizeof(nvs_header_t));
    if (header_status != NVS_OK)
    {
        LOG_ERROR("NVS: Failed to write header during compaction");
        return NVS_ERROR_FLASH_WRITE;
    }

    if (new_offset & 0x1)
    {
        new_offset++;
    }

    nvs->active_sector = new_sector;
    nvs->write_offset = new_offset;
    nvs->header = new_header;

    if (nvs_erase_sector(old_sector_addr) != NVS_OK)
    {
        LOG_WARN("%s", "NVS: Failed to erase old sector");
    }

    return NVS_OK;
}

static nvs_status_t nvs_compact_sector(nvs_handle_t *nvs)
{
    return nvs_compact_to_other_sector(nvs, NULL);
}

static nvs_status_t nvs_init_handle(nvs_handle_t *nvs)
{
    if (nvs == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    memset(nvs, 0, sizeof(nvs_handle_t));

    uint32_t best_sector = 0;
    uint32_t best_write_offset = 0;
    nvs_header_t best_header = {0};

    for (uint32_t i = 0; i < NVS_SECTOR_COUNT; i++)
    {
        uint32_t sector_addr = nvs_get_sector_address(i);
        nvs_header_t header;

        if (nvs_read_header(nvs, sector_addr, &header) == NVS_OK)
        {
            uint32_t offset = sizeof(nvs_header_t);
            uint16_t actual_entries = 0;
            const uint32_t sector_end = sector_addr + NVS_SECTOR_SIZE;
            const int use_crc = (header.version >= 0x0002u) ? 1 : 0;

            while (offset + sizeof(nvs_entry_header_t) <= sector_end)
            {
                nvs_entry_header_t entry_header;
                memcpy(&entry_header, (const void *)(sector_addr + offset), sizeof(nvs_entry_header_t));

                if (entry_header.key[0] == 0xFF)
                {
                    break;
                }

                if (entry_header.value_len == 0 || entry_header.value_len > NVS_VALUE_MAX_LEN)
                {
                    break;
                }

                size_t entry_size = use_crc ? nvs_entry_size_with_crc(entry_header.value_len)
                                            : (NVS_ENTRY_HEADER_SIZE + entry_header.value_len + ((NVS_ENTRY_HEADER_SIZE + entry_header.value_len) & 1u));

                if (offset + entry_size > sector_end)
                {
                    break;
                }

                offset += entry_size;
                actual_entries++;

                if (actual_entries >= header.entry_count)
                {
                    break;
                }
            }
            
            if (offset & 0x1)
            {
                offset++;
            }
            
            if (offset > best_write_offset)
            {
                best_sector = i;
                best_write_offset = offset;
                best_header = header;
                best_header.entry_count = actual_entries;
            }
        }
    }

    if (best_write_offset > 0)
    {
        nvs->active_sector = best_sector;
        nvs->header = best_header;
        nvs->write_offset = best_write_offset;
        nvs->initialized = true;
        LOG_INFO("NVS: Initialized from sector %lu, entries: %u", best_sector, nvs->header.entry_count);
        return NVS_OK;
    }

    uint32_t sector_addr = nvs_get_sector_address(0);
    if (nvs_erase_sector(sector_addr) != NVS_OK)
    {
        return NVS_ERROR_FLASH_ERASE;
    }

    if (nvs_write_header(nvs, sector_addr, 0) != NVS_OK)
    {
        return NVS_ERROR_FLASH_WRITE;
    }

    nvs->active_sector = 0;
    nvs->write_offset = sizeof(nvs_header_t);
    nvs->header.magic = NVS_MAGIC;
    nvs->header.version = NVS_VERSION;
    nvs->header.entry_count = 0;
    nvs->initialized = true;

    LOG_INFO("%s", "NVS: Initialized with new storage");
    return NVS_OK;
}

nvs_status_t nvs_set(nvs_handle_t *nvs, const char *key, const void *value, size_t value_len)
{
    if (nvs == NULL || key == NULL || value == NULL || value_len == 0 || value_len > NVS_VALUE_MAX_LEN)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    if (!nvs->initialized)
    {
        return NVS_ERROR_NOT_INITIALIZED;
    }

    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > NVS_KEY_MAX_LEN - 1)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    uint32_t sector_addr = nvs_get_sector_address(nvs->active_sector);
    uint32_t entry_addr;
    uint32_t old_value_len = 0;

    bool entry_exists = (nvs_find_entry(nvs, key, &entry_addr, NULL, &old_value_len) == NVS_OK);

    uint8_t entry_buf[NVS_ENTRY_HEADER_SIZE + NVS_VALUE_MAX_LEN + NVS_ENTRY_CRC_SIZE];
    nvs_entry_header_t *entry_header = (nvs_entry_header_t *)entry_buf;

    memset(entry_header, 0, NVS_ENTRY_HEADER_SIZE);
    strncpy(entry_header->key, key, NVS_KEY_MAX_LEN - 1);
    entry_header->key[NVS_KEY_MAX_LEN - 1] = '\0';
    entry_header->value_len = (uint32_t)value_len;
    memcpy(entry_buf + NVS_ENTRY_HEADER_SIZE, value, value_len);

    {
        uint32_t crc = nvs_crc32(entry_buf, NVS_ENTRY_HEADER_SIZE + value_len);
        memcpy(entry_buf + NVS_ENTRY_HEADER_SIZE + value_len, &crc, NVS_ENTRY_CRC_SIZE);
    }

    size_t entry_size = nvs_entry_size_with_crc((uint32_t)value_len);
    if (entry_size > NVS_ENTRY_HEADER_SIZE + value_len + NVS_ENTRY_CRC_SIZE)
    {
        entry_buf[entry_size - 1] = 0xFF;
    }

    if (nvs->write_offset + entry_size > sector_addr + NVS_SECTOR_SIZE)
    {
        if (nvs_compact_sector(nvs) != NVS_OK)
        {
            return NVS_ERROR_NO_SPACE;
        }
        sector_addr = nvs_get_sector_address(nvs->active_sector);
    }

    if (nvs->write_offset + entry_size > sector_addr + NVS_SECTOR_SIZE)
    {
        return NVS_ERROR_NO_SPACE;
    }

    uint32_t write_addr = sector_addr + nvs->write_offset;
    nvs_status_t write_status = nvs_write_flash(write_addr, entry_buf, entry_size);
    
    if (write_status != NVS_OK)
    {
        LOG_DEBUG("NVS: Write failed, triggering compaction");
        if (nvs_compact_sector(nvs) != NVS_OK)
        {
            return NVS_ERROR_FLASH_WRITE;
        }
        sector_addr = nvs_get_sector_address(nvs->active_sector);
        write_addr = sector_addr + nvs->write_offset;
        
        for (size_t i = 0; i < entry_size; i += 2)
        {
            uint16_t check_value = *(volatile uint16_t *)(write_addr + i);
            if (check_value != 0xFFFF)
            {
                LOG_ERROR("NVS: Write address not erased after compaction at 0x%08X (value: 0x%04X)", 
                         write_addr + i, check_value);
                return NVS_ERROR_FLASH_WRITE;
            }
        }
        
        write_status = nvs_write_flash(write_addr, entry_buf, entry_size);
        if (write_status != NVS_OK)
        {
            LOG_ERROR("NVS: Write failed after compaction at 0x%08X", write_addr);
            return NVS_ERROR_FLASH_WRITE;
        }
    }

    nvs->write_offset += entry_size;

    if (!entry_exists)
    {
        uint16_t new_entry_count = nvs->header.entry_count + 1;
        nvs_status_t header_status = nvs_write_header(nvs, sector_addr, new_entry_count);
        if (header_status != NVS_OK)
        {
            LOG_DEBUG("NVS: Cannot update header entry_count, triggering compaction");
            
            if (nvs_compact_sector(nvs) != NVS_OK)
            {
                return NVS_ERROR_FLASH_WRITE;
            }
            
            nvs->header.entry_count = new_entry_count;
            sector_addr = nvs_get_sector_address(nvs->active_sector);
            if (nvs_write_header(nvs, sector_addr, nvs->header.entry_count) != NVS_OK)
            {
                return NVS_ERROR_FLASH_WRITE;
            }
        }
        else
        {
            nvs->header.entry_count = new_entry_count;
        }
    }

    return NVS_OK;
}

nvs_status_t nvs_get(nvs_handle_t *nvs, const char *key, void *value, size_t *value_len)
{
    if (nvs == NULL || key == NULL || value == NULL || value_len == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    if (!nvs->initialized)
    {
        return NVS_ERROR_NOT_INITIALIZED;
    }

    return nvs_find_entry(nvs, key, NULL, (uint8_t *)value, (uint32_t *)value_len);
}

nvs_status_t nvs_delete(nvs_handle_t *nvs, const char *key)
{
    if (nvs == NULL || key == NULL)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    if (!nvs->initialized)
    {
        return NVS_ERROR_NOT_INITIALIZED;
    }

    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > NVS_KEY_MAX_LEN - 1)
    {
        return NVS_ERROR_INVALID_PARAM;
    }

    uint32_t entry_addr;
    uint32_t value_len = 0;

    if (nvs_find_entry(nvs, key, &entry_addr, NULL, &value_len) != NVS_OK)
    {
        return NVS_ERROR_NOT_FOUND;
    }

    return nvs_compact_to_other_sector(nvs, key);
}

static void nvs_print_boot_info(void)
{
    uint32_t reboot_count = nvs_reboot_count_get();
    uint32_t slot_flag = boot_slot_flag_peek();
    char sn_buf[NVS_SN_SIZE];
    char hw_buf[NVS_HW_VERSION_SIZE];
    char region_buf[NVS_REGION_SIZE];

    LOG_INFO("--- NVS boot ---");
    LOG_INFO("  reboot_count: %lu", (unsigned long)reboot_count);
    LOG_INFO("  flash_slot_flag: 0x%08lX", (unsigned long)slot_flag);
    LOG_INFO("  exec_image: %s", boot_slot_running_from_b() ? "B" : "A");
    if ((slot_flag == BOOT_SLOT_FLAG_APP_B || slot_flag == BOOT_SLOT_FLAG_FACTORY) && !boot_slot_running_from_b())
    {
        LOG_WARN("  slot_mismatch: flag selects APP-B but CPU is on A (B@0x08020000 invalid/empty, or Bootloader lacks APP_B flag handling)");
    }
    if (nvs_sn_get(sn_buf))
        LOG_INFO("  sn: %s", sn_buf);
    if (nvs_hw_version_get(hw_buf))
        LOG_INFO("  hw: %s", hw_buf);
    {
        char app_buf[NVS_APP_VERSION_SIZE];
        if (nvs_app_version_get(app_buf))
            LOG_INFO("  app: %s", app_buf);
    }
    {
        char ftm_buf[NVS_FACTORY_VERSION_SIZE];
        if (nvs_factory_version_get(ftm_buf))
            LOG_INFO("  factory: %s", ftm_buf);
    }
    if (nvs_region_get(region_buf))
        LOG_INFO("  region: %s", region_buf);
    LOG_INFO("  device_type: %u", (unsigned)nvs_device_type_get());
    LOG_INFO("----------------");
}

static void nvs_ensure_defaults(void)
{
    char sn_buf[NVS_SN_SIZE];
    if (!nvs_sn_get(sn_buf))
    {
        (void)nvs_sn_set(NVS_DEFAULT_SN);
    }

    char region_buf[NVS_REGION_SIZE];
    if (!nvs_region_get(region_buf))
    {
        (void)nvs_region_set(NVS_DEFAULT_REGION);
    }

    uint8_t type = 0;
    size_t len = sizeof(type);
    if (nvs_get(&g_nvs_handle, NVS_KEY_DEVICE_TYPE, &type, &len) != NVS_OK)
    {
        nvs_device_type_set((uint8_t)NVS_DEFAULT_DEVICE_TYPE);
    }
}

void nvs_init(void)
{
    if (g_nvs_initialized)
    {
        return;
    }

    if (nvs_init_handle(&g_nvs_handle) == NVS_OK)
    {
        g_nvs_initialized = true;
        nvs_ensure_defaults();
        {
            uint32_t count = nvs_reboot_count_get();
            count++;
            nvs_reboot_count_set(count);
        }
        LOG_INFO("NVS initialized");
        nvs_print_boot_info();
    }
    else
    {
        LOG_ERROR("NVS initialization failed");
        return;
    }
}

void nvs_factory_reset(void)
{
    if (!g_nvs_initialized)
    {
        LOG_ERROR("NVS: Not initialized");
        return;
    }

    for (uint32_t i = 0; i < NVS_SECTOR_COUNT; i++)
    {
        uint32_t sector_addr = nvs_get_sector_address(i);
        nvs_erase_sector(sector_addr);
    }

    uint32_t sector_addr = nvs_get_sector_address(0);
    nvs_write_header(&g_nvs_handle, sector_addr, 0);

    g_nvs_handle.active_sector = 0;
    g_nvs_handle.write_offset = sizeof(nvs_header_t);
    g_nvs_handle.header.magic = NVS_MAGIC;
    g_nvs_handle.header.version = NVS_VERSION;
    g_nvs_handle.header.entry_count = 0;

    LOG_INFO("NVS: Factory reset completed");
}

void nvs_reboot_count_set(uint32_t count)
{
    if (!g_nvs_initialized)
    {
        LOG_WARN("NVS: Not initialized, cannot set reboot count");
        return;
    }
    nvs_status_t status = nvs_set(&g_nvs_handle, NVS_KEY_REBOOT_COUNT, &count, sizeof(uint32_t));
    if (status != NVS_OK)
    {
        LOG_ERROR("NVS: Failed to set reboot count, status: %d", status);
    }
}

uint32_t nvs_reboot_count_get(void)
{
    if (!g_nvs_initialized)
    {
        return 0;
    }
    uint32_t count = 0;
    size_t len = sizeof(uint32_t);
    if (nvs_get(&g_nvs_handle, NVS_KEY_REBOOT_COUNT, &count, &len) != NVS_OK)
    {
        return 0;
    }
    return count;
}

void nvs_frame_count_set(uint32_t count)
{
    if (!g_nvs_initialized)
    {
        return;
    }
    nvs_set(&g_nvs_handle, NVS_KEY_FRAME_COUNT, &count, sizeof(uint32_t));
}

uint32_t nvs_frame_count_get(void)
{
    if (!g_nvs_initialized)
    {
        return 0;
    }
    uint32_t count = 0;
    size_t len = sizeof(uint32_t);
    if (nvs_get(&g_nvs_handle, NVS_KEY_FRAME_COUNT, &count, &len) != NVS_OK)
    {
        return 0;
    }
    return count;
}

void nvs_heartbeat_seq_set(uint32_t seq)
{
    if (!g_nvs_initialized)
    {
        return;
    }
    nvs_set(&g_nvs_handle, NVS_KEY_HEARTBEAT_SEQ, &seq, sizeof(uint32_t));
}

uint32_t nvs_heartbeat_seq_get(void)
{
    if (!g_nvs_initialized)
    {
        return 0;
    }
    uint32_t seq = 0;
    size_t len = sizeof(uint32_t);
    if (nvs_get(&g_nvs_handle, NVS_KEY_HEARTBEAT_SEQ, &seq, &len) != NVS_OK)
    {
        return 0;
    }
    return seq;
}

bool nvs_mac_set(const uint8_t *mac)
{
    if (!g_nvs_initialized || mac == NULL)
    {
        return false;
    }
    return (nvs_set(&g_nvs_handle, NVS_KEY_MAC, mac, NVS_MAC_SIZE) == NVS_OK);
}

bool nvs_mac_get(uint8_t *mac)
{
    if (!g_nvs_initialized || mac == NULL)
    {
        return false;
    }
    size_t len = NVS_MAC_SIZE;
    return (nvs_get(&g_nvs_handle, NVS_KEY_MAC, mac, &len) == NVS_OK);
}

bool nvs_sn_set(const char *sn)
{
    if (!g_nvs_initialized || sn == NULL)
    {
        return false;
    }
    if (strlen(sn) >= NVS_SN_SIZE)
    {
        LOG_ERROR("NVS: SN length too long");
        return false;
    }
    return (nvs_set(&g_nvs_handle, NVS_KEY_SN, sn, strlen(sn) + 1) == NVS_OK);
}

bool nvs_sn_get(char *sn)
{
    if (!g_nvs_initialized || sn == NULL)
    {
        return false;
    }
    size_t len = NVS_SN_SIZE;
    if (nvs_get(&g_nvs_handle, NVS_KEY_SN, sn, &len) == NVS_OK)
    {
        sn[NVS_SN_SIZE - 1] = '\0';
        return true;
    }
    return false;
}

bool nvs_hw_version_get(char *version)
{
    return nvs_copy_version_string(version, NVS_HW_VERSION_SIZE, NVS_HW_VERSION_STRING);
}

bool nvs_app_version_get(char *version)
{
    return nvs_copy_version_string(version, NVS_APP_VERSION_SIZE, NVS_APP_VERSION_STRING);
}

bool nvs_factory_version_get(char *version)
{
    return nvs_copy_version_string(version, NVS_FACTORY_VERSION_SIZE, NVS_FACTORY_VERSION_STRING);
}

bool nvs_region_set(const char *region)
{
    if (!g_nvs_initialized || region == NULL)
    {
        return false;
    }
    if (strlen(region) >= NVS_REGION_SIZE)
    {
        LOG_ERROR("NVS: Region length too long");
        return false;
    }
    return (nvs_set(&g_nvs_handle, NVS_KEY_REGION, region, strlen(region) + 1) == NVS_OK);
}

bool nvs_region_get(char *region)
{
    if (!g_nvs_initialized || region == NULL)
    {
        return false;
    }
    size_t len = NVS_REGION_SIZE;
    if (nvs_get(&g_nvs_handle, NVS_KEY_REGION, region, &len) == NVS_OK)
    {
        region[NVS_REGION_SIZE - 1] = '\0';
        return true;
    }
    return false;
}

void nvs_device_type_set(uint8_t type)
{
    if (!g_nvs_initialized)
    {
        return;
    }
    nvs_set(&g_nvs_handle, NVS_KEY_DEVICE_TYPE, &type, sizeof(uint8_t));
}

uint8_t nvs_device_type_get(void)
{
    if (!g_nvs_initialized)
    {
        return (uint8_t)NVS_DEFAULT_DEVICE_TYPE;
    }
    uint8_t type = 0;
    size_t len = sizeof(uint8_t);
    if (nvs_get(&g_nvs_handle, NVS_KEY_DEVICE_TYPE, &type, &len) != NVS_OK)
    {
        return (uint8_t)NVS_DEFAULT_DEVICE_TYPE;
    }
    return type;
}

void nvs_run_time_set(uint32_t sec)
{
    if (!g_nvs_initialized)
    {
        return;
    }
    nvs_set(&g_nvs_handle, NVS_KEY_RUN_TIME, &sec, sizeof(uint32_t));
}

uint32_t nvs_run_time_get(void)
{
    if (!g_nvs_initialized)
    {
        return NVS_DEFAULT_RUN_TIME;
    }
    uint32_t sec = NVS_DEFAULT_RUN_TIME;
    size_t len = sizeof(uint32_t);
    if (nvs_get(&g_nvs_handle, NVS_KEY_RUN_TIME, &sec, &len) != NVS_OK)
    {
        return NVS_DEFAULT_RUN_TIME;
    }
    return sec;
}
