/**
 * @file    eeprom.c
 * @brief   EEPROM driver implementation - modular design
 */

#include "eeprom.h"
#include <string.h>

#define EEPROM_WRITE_TIMEOUT_MS    100u
#define EEPROM_READY_RETRY_COUNT   3u
#define EEPROM_MAX_PAGE_SIZE       128u
/* 24Cxx devices use 7-bit address, typical base is 0x50 */
#define EEPROM_DEFAULT_ADDRESS     0x50u

 static eeprom_status_t eeprom_get_config(eeprom_type_t type, eeprom_config_t *config)
 {
     if (config == NULL)
     {
         return EEPROM_ERROR_TYPE;
     }
     
     config->type = type;
     
     switch (type)
     {
         case EEPROM_24C02:
             config->page_size = 8;
             config->total_size = 256;
             config->page_count = 32;
             config->address_bytes = 1;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C04:
             config->page_size = 16;
             config->total_size = 512;
             config->page_count = 32;
             config->address_bytes = 1;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C08:
             config->page_size = 16;
             config->total_size = 1024;
             config->page_count = 64;
             config->address_bytes = 1;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C16:
             config->page_size = 16;
             config->total_size = 2048;
             config->page_count = 128;
             config->address_bytes = 1;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C32:
             config->page_size = 32;
             config->total_size = 4096;
             config->page_count = 128;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C64:
             config->page_size = 32;
             config->total_size = 8192;
             config->page_count = 256;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C128:
             config->page_size = 64;
             config->total_size = 16384;
             config->page_count = 256;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C256:
             config->page_size = 64;
             config->total_size = 32768;
             config->page_count = 512;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C512:
             config->page_size = 128;
             config->total_size = 65536;
             config->page_count = 512;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         case EEPROM_24C1024:
             config->page_size = 128;
             config->total_size = 131072;
             config->page_count = 1024;
             config->address_bytes = 2;
             config->write_delay_ms = 5;
             break;
             
         default:
             return EEPROM_ERROR_TYPE;
     }
     
     return EEPROM_OK;
 }
 
static eeprom_status_t eeprom_wait_write_complete(eeprom_t *eeprom)
{
    if (eeprom == NULL || eeprom->is_ready_func == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    for (uint32_t retry = 0u; retry < EEPROM_WRITE_TIMEOUT_MS; retry++)
    {
        if (eeprom->is_ready_func(eeprom->config.device_address) == 0)
        {
            return EEPROM_OK;
        }
        if (eeprom->delay_ms != NULL)
        {
            eeprom->delay_ms(1u);
        }
    }
    
    if (eeprom != NULL)
    {
        eeprom->error_count++;
    }
    return EEPROM_ERROR_TIMEOUT;
}
 
 int eeprom_init(eeprom_t *eeprom, eeprom_type_t type, uint8_t address,
                 eeprom_i2c_write_func_t write_func,
                 eeprom_i2c_read_func_t read_func,
                 eeprom_i2c_is_ready_func_t is_ready_func,
                 eeprom_delay_ms_func_t delay_ms)
 {
    if (eeprom == NULL || write_func == NULL || read_func == NULL)
     {
         return -1;
     }
     
     eeprom_status_t status = eeprom_get_config(type, &eeprom->config);
     if (status != EEPROM_OK)
     {
         return -1;
     }
     
     if (address != 0)
     {
         eeprom->config.device_address = address;
     }
     else
     {
         eeprom->config.device_address = EEPROM_DEFAULT_ADDRESS;
     }
     
     eeprom->write_func = write_func;
     eeprom->read_func = read_func;
    eeprom->is_ready_func = is_ready_func;
    eeprom->delay_ms      = delay_ms;
     eeprom->write_count = 0;
     eeprom->read_count = 0;
     eeprom->error_count = 0;
     
     status = eeprom_is_ready(eeprom);
     if (status == EEPROM_OK)
     {
         eeprom->initialized = true;
     }
     else
     {
         eeprom->initialized = true;
     }
     
     return 0;
 }
 
eeprom_status_t eeprom_is_ready(eeprom_t *eeprom)
{
    if (eeprom == NULL || eeprom->is_ready_func == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    for (uint32_t i = 0u; i < EEPROM_READY_RETRY_COUNT; i++)
    {
        if (eeprom->is_ready_func(eeprom->config.device_address) == 0)
        {
            return EEPROM_OK;
        }
        if (eeprom->delay_ms != NULL)
        {
            eeprom->delay_ms(1u);
        }
    }
    
    if (eeprom != NULL)
    {
        eeprom->error_count++;
    }
    return EEPROM_ERROR_I2C;
}
 
 eeprom_status_t eeprom_write_byte(eeprom_t *eeprom, uint32_t address, uint8_t data)
 {
     if (eeprom == NULL || !eeprom->initialized || eeprom->write_func == NULL)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (address >= eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_ADDRESS;
     }
     
     uint8_t buffer[3];
     uint8_t buffer_size;
     
     if (eeprom->config.address_bytes == 2)
     {
         buffer[0] = (uint8_t)((address >> 8) & 0xFF);
         buffer[1] = (uint8_t)(address & 0xFF);
         buffer[2] = data;
         buffer_size = 3;
     }
     else
     {
         buffer[0] = (uint8_t)(address & 0xFF);
         buffer[1] = data;
         buffer_size = 2;
     }
     
     if (eeprom->write_func(eeprom->config.device_address, buffer, buffer_size) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_WRITE;
     }
     
     eeprom->write_count++;
     
     return eeprom_wait_write_complete(eeprom);
 }
 
 eeprom_status_t eeprom_read_byte(eeprom_t *eeprom, uint32_t address, uint8_t *data)
 {
     if (eeprom == NULL || !eeprom->initialized || eeprom->read_func == NULL || eeprom->write_func == NULL)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (data == NULL)
     {
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (address >= eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_ADDRESS;
     }
     
     uint8_t addr_buffer[2];
     uint8_t addr_size;
     
     if (eeprom->config.address_bytes == 2)
     {
         addr_buffer[0] = (uint8_t)((address >> 8) & 0xFF);
         addr_buffer[1] = (uint8_t)(address & 0xFF);
         addr_size = 2;
     }
     else
     {
         addr_buffer[0] = (uint8_t)(address & 0xFF);
         addr_size = 1;
     }
     
     if (eeprom->write_func(eeprom->config.device_address, addr_buffer, addr_size) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_READ;
     }
     
     if (eeprom->read_func(eeprom->config.device_address, data, 1) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_READ;
     }
     
     eeprom->read_count++;
     return EEPROM_OK;
 }
 
 eeprom_status_t eeprom_write_page(eeprom_t *eeprom, uint32_t start_address, const uint8_t *data, uint16_t size)
 {
     if (eeprom == NULL || !eeprom->initialized || eeprom->write_func == NULL)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (data == NULL)
     {
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (start_address >= eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (size == 0 || size > eeprom->config.page_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
     uint32_t page_start = (start_address / eeprom->config.page_size) * eeprom->config.page_size;
     uint32_t page_offset = start_address - page_start;
     
     if (size > (eeprom->config.page_size - page_offset))
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
     if ((start_address + size) > eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
     uint8_t buffer[EEPROM_MAX_PAGE_SIZE + 2];
     uint8_t buffer_size;
     
     if (eeprom->config.address_bytes == 2)
     {
         buffer[0] = (uint8_t)((start_address >> 8) & 0xFF);
         buffer[1] = (uint8_t)(start_address & 0xFF);
         memcpy(&buffer[2], data, size);
         buffer_size = (uint8_t)(size + 2);
     }
     else
     {
         buffer[0] = (uint8_t)(start_address & 0xFF);
         memcpy(&buffer[1], data, size);
         buffer_size = (uint8_t)(size + 1);
     }
     
     if (eeprom->write_func(eeprom->config.device_address, buffer, buffer_size) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_WRITE;
     }
     
     eeprom->write_count++;
     
     return eeprom_wait_write_complete(eeprom);
 }
 
 eeprom_status_t eeprom_read(eeprom_t *eeprom, uint32_t start_address, uint8_t *data, uint16_t size)
 {
     if (eeprom == NULL || !eeprom->initialized || eeprom->read_func == NULL || eeprom->write_func == NULL)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (data == NULL)
     {
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (start_address >= eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (size == 0 || (start_address + size) > eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
     uint8_t addr_buffer[2];
     uint8_t addr_size;
     
     if (eeprom->config.address_bytes == 2)
     {
         addr_buffer[0] = (uint8_t)((start_address >> 8) & 0xFF);
         addr_buffer[1] = (uint8_t)(start_address & 0xFF);
         addr_size = 2;
     }
     else
     {
         addr_buffer[0] = (uint8_t)(start_address & 0xFF);
         addr_size = 1;
     }
     
     if (eeprom->write_func(eeprom->config.device_address, addr_buffer, addr_size) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_READ;
     }
     
     if (eeprom->read_func(eeprom->config.device_address, data, size) != 0)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_READ;
     }
     
     eeprom->read_count++;
     return EEPROM_OK;
 }
 
 eeprom_status_t eeprom_write_stream(eeprom_t *eeprom, uint32_t start_address, const uint8_t *data, uint32_t size)
 {
     if (eeprom == NULL || !eeprom->initialized)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (data == NULL)
     {
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (start_address >= eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (size == 0 || (start_address + size) > eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
     uint32_t bytes_written = 0;
     uint32_t current_address = start_address;
     
     while (bytes_written < size)
     {
         uint32_t page_start = (current_address / eeprom->config.page_size) * eeprom->config.page_size;
         uint32_t page_offset = current_address - page_start;
         uint16_t bytes_to_write = (uint16_t)(eeprom->config.page_size - page_offset);
         
         if (bytes_to_write > (size - bytes_written))
         {
             bytes_to_write = (uint16_t)(size - bytes_written);
         }
         
        eeprom_status_t status = eeprom_write_page(eeprom, current_address, &data[bytes_written], bytes_to_write);
        if (status != EEPROM_OK)
        {
            return status;
        }
        
        bytes_written += bytes_to_write;
        current_address += bytes_to_write;
        
        if (eeprom->config.write_delay_ms > 0 && eeprom->delay_ms != NULL)
        {
            eeprom->delay_ms(eeprom->config.write_delay_ms);
        }
    }
    
    return EEPROM_OK;
}
 
 eeprom_status_t eeprom_chip_erase(eeprom_t *eeprom)
 {
     if (eeprom == NULL || !eeprom->initialized)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     uint8_t fill_data[EEPROM_MAX_PAGE_SIZE];
     memset(fill_data, 0xFF, sizeof(fill_data));
     
     uint32_t address = 0;
     while (address < eeprom->config.total_size)
     {
         uint32_t remaining = eeprom->config.total_size - address;
         uint16_t bytes_to_write = (remaining < sizeof(fill_data)) ? (uint16_t)remaining : sizeof(fill_data);
         
         eeprom_status_t status = eeprom_write_stream(eeprom, address, fill_data, bytes_to_write);
         if (status != EEPROM_OK)
         {
             return status;
         }
         
         address += bytes_to_write;
     }
     
     return EEPROM_OK;
 }
 
 eeprom_status_t eeprom_fill(eeprom_t *eeprom, uint8_t value)
 {
     if (eeprom == NULL || !eeprom->initialized)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     uint8_t fill_data[EEPROM_MAX_PAGE_SIZE];
     memset(fill_data, value, sizeof(fill_data));
     
     uint32_t address = 0;
     while (address < eeprom->config.total_size)
     {
         uint32_t remaining = eeprom->config.total_size - address;
         uint16_t bytes_to_write = (remaining < sizeof(fill_data)) ? (uint16_t)remaining : sizeof(fill_data);
         
         eeprom_status_t status = eeprom_write_stream(eeprom, address, fill_data, bytes_to_write);
         if (status != EEPROM_OK)
         {
             return status;
         }
         
         address += bytes_to_write;
     }
     
     return EEPROM_OK;
 }
 
 eeprom_status_t eeprom_verify(eeprom_t *eeprom, uint32_t address, const uint8_t *data, uint32_t size)
 {
     if (eeprom == NULL || !eeprom->initialized)
     {
         return EEPROM_ERROR_NOT_INIT;
     }
     
     if (data == NULL)
     {
         return EEPROM_ERROR_ADDRESS;
     }
     
     if (size == 0 || (address + size) > eeprom->config.total_size)
     {
         eeprom->error_count++;
         return EEPROM_ERROR_SIZE;
     }
     
    uint8_t read_data[256];
    uint32_t bytes_verified = 0;
    
    while (bytes_verified < size)
    {
        uint32_t remaining = size - bytes_verified;
        uint16_t bytes_to_read = (remaining < sizeof(read_data)) ? (uint16_t)remaining : sizeof(read_data);
        
        eeprom_status_t status = eeprom_read(eeprom, address + bytes_verified, read_data, bytes_to_read);
        if (status != EEPROM_OK)
        {
            return status;
        }
        
        if (memcmp(&data[bytes_verified], read_data, bytes_to_read) != 0)
        {
            eeprom->error_count++;
            return EEPROM_ERROR_VERIFY;
        }
        
        bytes_verified += bytes_to_read;
    }
    
    return EEPROM_OK;
}

eeprom_status_t eeprom_get_info(eeprom_t *eeprom, uint32_t *write_count, 
                                uint32_t *read_count, uint32_t *error_count)
{
    if (eeprom == NULL || !eeprom->initialized)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    if (write_count != NULL)
    {
        *write_count = eeprom->write_count;
    }
    
    if (read_count != NULL)
    {
        *read_count = eeprom->read_count;
    }
    
    if (error_count != NULL)
    {
        *error_count = eeprom->error_count;
    }
    
    return EEPROM_OK;
}

eeprom_status_t eeprom_self_test(eeprom_t *eeprom)
{
    if (eeprom == NULL || !eeprom->initialized)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t test_data[] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t read_data[sizeof(test_data)];
    
    uint32_t test_address = (eeprom->config.total_size > 256) ? 0x100 : 0x10;
    
    eeprom_status_t status = eeprom_write_page(eeprom, test_address, test_data, sizeof(test_data));
    if (status != EEPROM_OK)
    {
        return status;
    }
    
    if (eeprom->delay_ms != NULL)
    {
        eeprom->delay_ms(10);
    }
    
    status = eeprom_read(eeprom, test_address, read_data, sizeof(read_data));
    if (status != EEPROM_OK)
    {
        return status;
    }
    
    if (memcmp(test_data, read_data, sizeof(test_data)) != 0)
    {
        eeprom->error_count++;
        return EEPROM_ERROR_VERIFY;
    }
    
    return EEPROM_OK;
}

eeprom_status_t eeprom_write_string(eeprom_t *eeprom, uint32_t address, const char *str)
{
    if (eeprom == NULL || !eeprom->initialized || str == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint16_t len = (uint16_t)(strlen(str) + 1);
    if (len == 0 || (address + len) > eeprom->config.total_size)
    {
        eeprom->error_count++;
        return EEPROM_ERROR_SIZE;
    }
    
    return eeprom_write_stream(eeprom, address, (const uint8_t*)str, len);
}

eeprom_status_t eeprom_read_string(eeprom_t *eeprom, uint32_t address, char *buffer, uint16_t max_len)
{
    if (eeprom == NULL || !eeprom->initialized || buffer == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    if (max_len == 0)
    {
        return EEPROM_ERROR_SIZE;
    }
    
    if (address >= eeprom->config.total_size)
    {
        eeprom->error_count++;
        return EEPROM_ERROR_ADDRESS;
    }
    
    uint16_t i;
    for (i = 0; i < (max_len - 1); i++)
    {
        uint8_t ch;
        eeprom_status_t status = eeprom_read_byte(eeprom, address + i, &ch);
        if (status != EEPROM_OK)
        {
            buffer[0] = '\0';
            return status;
        }
        
        buffer[i] = (char)ch;
        if (ch == '\0')
        {
            break;
        }
    }
    
    buffer[i] = '\0';
    return EEPROM_OK;
}

eeprom_status_t eeprom_write_u16(eeprom_t *eeprom, uint32_t address, uint16_t value)
{
    if (eeprom == NULL || !eeprom->initialized)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return eeprom_write_stream(eeprom, address, data, 2);
}

eeprom_status_t eeprom_read_u16(eeprom_t *eeprom, uint32_t address, uint16_t *value)
{
    if (eeprom == NULL || !eeprom->initialized || value == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[2];
    eeprom_status_t status = eeprom_read(eeprom, address, data, 2);
    if (status == EEPROM_OK)
    {
        *value = (uint16_t)((data[0] << 8) | data[1]);
    }
    return status;
}

eeprom_status_t eeprom_write_u32(eeprom_t *eeprom, uint32_t address, uint32_t value)
{
    if (eeprom == NULL || !eeprom->initialized)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    return eeprom_write_stream(eeprom, address, data, 4);
}

eeprom_status_t eeprom_read_u32(eeprom_t *eeprom, uint32_t address, uint32_t *value)
{
    if (eeprom == NULL || !eeprom->initialized || value == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[4];
    eeprom_status_t status = eeprom_read(eeprom, address, data, 4);
    if (status == EEPROM_OK)
    {
        *value = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    }
    return status;
}

eeprom_status_t eeprom_write_float(eeprom_t *eeprom, uint32_t address, float value)
{
    if (eeprom == NULL || !eeprom->initialized)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[4];
    memcpy(data, &value, 4);
    return eeprom_write_stream(eeprom, address, data, 4);
}

eeprom_status_t eeprom_read_float(eeprom_t *eeprom, uint32_t address, float *value)
{
    if (eeprom == NULL || !eeprom->initialized || value == NULL)
    {
        return EEPROM_ERROR_NOT_INIT;
    }
    
    uint8_t data[4];
    eeprom_status_t status = eeprom_read(eeprom, address, data, 4);
    if (status == EEPROM_OK)
    {
        memcpy(value, data, sizeof(float));
    }
    return status;
}
