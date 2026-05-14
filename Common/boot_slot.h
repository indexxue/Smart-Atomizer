#ifndef BOOT_SLOT_H
#define BOOT_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BOOT_SLOT_FLAG_ADDR     0x08037FFCU
#define BOOT_SLOT_FLAG_FACTORY  0xAAAAAAAAU

bool boot_slot_request_factory(void);
bool boot_slot_request_app_a(void);
void boot_slot_system_reset(void);

#ifdef __cplusplus
}
#endif

#endif
