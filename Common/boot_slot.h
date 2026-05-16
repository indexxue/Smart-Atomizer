#ifndef BOOT_SLOT_H
#define BOOT_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BOOT_SLOT_FLAG_ADDR     0x08037FFCU
/** Next boot / OTA: jump to APP-B (same physical slot as factory image; see flash_partition). */
#define BOOT_SLOT_FLAG_FACTORY  0xAAAAAAAAU
/** User-selected dual-app slot B (Bootloader treats like FACTORY for jump target). */
#define BOOT_SLOT_FLAG_APP_B    0xB10BB10BU

/** VTOR after reset for images linked at APP-A / APP-B (must match Core/Factory main.c). */
#define BOOT_SLOT_APP_A_VTOR    (0x08000000U + 0x8000U)
#define BOOT_SLOT_APP_B_VTOR    (0x08000000U + 0x20000U)

bool boot_slot_request_factory(void);
bool boot_slot_request_app_b(void);
bool boot_slot_request_app_a(void);
void boot_slot_system_reset(void);

bool boot_slot_running_from_b(void);
uint32_t boot_slot_flag_peek(void);
/** Write opposite slot flag then NVIC reset. Returns false if flash failed (no reset). */
bool boot_slot_toggle_partition_and_reset(void);

#ifdef __cplusplus
}
#endif

#endif
