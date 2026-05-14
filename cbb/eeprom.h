/**
 * @file    eeprom.h
 * @brief   EEPROM driver - modular design for reuse
 */

#ifndef __EEPROM_H
#define __EEPROM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int  (*eeprom_i2c_write_func_t)(uint8_t addr, const uint8_t *data, uint16_t len);
typedef int  (*eeprom_i2c_read_func_t)(uint8_t addr, uint8_t *data, uint16_t len);
typedef int  (*eeprom_i2c_is_ready_func_t)(uint8_t addr);
typedef void (*eeprom_delay_ms_func_t)(uint32_t ms);

typedef enum {
    EEPROM_24C02 = 0,
    EEPROM_24C04,
    EEPROM_24C08,
    EEPROM_24C16,
    EEPROM_24C32,
    EEPROM_24C64,
    EEPROM_24C128,
    EEPROM_24C256,
    EEPROM_24C512,
    EEPROM_24C1024
} eeprom_type_t;

typedef enum {
    EEPROM_OK = 0,
    EEPROM_ERROR_I2C,
    EEPROM_ERROR_ADDRESS,
    EEPROM_ERROR_SIZE,
    EEPROM_ERROR_WRITE,
    EEPROM_ERROR_READ,
    EEPROM_ERROR_NOT_INIT,
    EEPROM_ERROR_VERIFY,
    EEPROM_ERROR_TYPE,
    EEPROM_ERROR_TIMEOUT
} eeprom_status_t;


typedef struct {
    eeprom_type_t type;
    uint16_t page_size;
    uint32_t total_size;
    uint16_t page_count;
    uint8_t address_bytes;
    uint8_t device_address;
    uint8_t write_delay_ms;
} eeprom_config_t;

typedef struct {
    eeprom_i2c_write_func_t write_func;
    eeprom_i2c_read_func_t read_func;
    eeprom_i2c_is_ready_func_t is_ready_func;
    eeprom_delay_ms_func_t delay_ms;
    eeprom_config_t config;
    bool initialized;
    uint32_t write_count;
    uint32_t read_count;
    uint32_t error_count;
} eeprom_t;

int eeprom_init(eeprom_t *eeprom, eeprom_type_t type, uint8_t address,
                eeprom_i2c_write_func_t write_func,
                eeprom_i2c_read_func_t read_func,
                eeprom_i2c_is_ready_func_t is_ready_func,
                eeprom_delay_ms_func_t delay_ms);

eeprom_status_t eeprom_is_ready(eeprom_t *eeprom);
eeprom_status_t eeprom_write_byte(eeprom_t *eeprom, uint32_t address, uint8_t data);
eeprom_status_t eeprom_read_byte(eeprom_t *eeprom, uint32_t address, uint8_t *data);
eeprom_status_t eeprom_write_page(eeprom_t *eeprom, uint32_t start_address, const uint8_t *data, uint16_t size);
eeprom_status_t eeprom_read(eeprom_t *eeprom, uint32_t start_address, uint8_t *data, uint16_t size);
eeprom_status_t eeprom_write_stream(eeprom_t *eeprom, uint32_t start_address, const uint8_t *data, uint32_t size);
eeprom_status_t eeprom_chip_erase(eeprom_t *eeprom);
eeprom_status_t eeprom_fill(eeprom_t *eeprom, uint8_t value);
eeprom_status_t eeprom_verify(eeprom_t *eeprom, uint32_t address, const uint8_t *data, uint32_t size);
eeprom_status_t eeprom_get_info(eeprom_t *eeprom, uint32_t *write_count, uint32_t *read_count, uint32_t *error_count);
eeprom_status_t eeprom_self_test(eeprom_t *eeprom);

eeprom_status_t eeprom_write_string(eeprom_t *eeprom, uint32_t address, const char *str);
eeprom_status_t eeprom_read_string(eeprom_t *eeprom, uint32_t address, char *buffer, uint16_t max_len);
eeprom_status_t eeprom_write_u16(eeprom_t *eeprom, uint32_t address, uint16_t value);
eeprom_status_t eeprom_read_u16(eeprom_t *eeprom, uint32_t address, uint16_t *value);
eeprom_status_t eeprom_write_u32(eeprom_t *eeprom, uint32_t address, uint32_t value);
eeprom_status_t eeprom_read_u32(eeprom_t *eeprom, uint32_t address, uint32_t *value);
eeprom_status_t eeprom_write_float(eeprom_t *eeprom, uint32_t address, float value);
eeprom_status_t eeprom_read_float(eeprom_t *eeprom, uint32_t address, float *value);

#ifdef __cplusplus
}
#endif

#endif /* __EEPROM_H */
