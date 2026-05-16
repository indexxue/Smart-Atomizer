#include "boot_slot.h"
#include "main.h"

#define STM32F1_PAGE_SIZE  0x800U

static uint32_t flash_page_base(uint32_t addr)
{
    return addr & ~(STM32F1_PAGE_SIZE - 1U);
}

static int flash_erase_one_page(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_addr;
    erase.NbPages = 1U;
    return (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) ? 0 : -1;
}

static bool boot_slot_program_u32(uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)BOOT_SLOT_FLAG_ADDR;

    if (*p == val)
    {
        return true;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    uint32_t addr = BOOT_SLOT_FLAG_ADDR;
    uint16_t hw0 = (uint16_t)(val & 0xFFFFU);
    uint16_t hw1 = (uint16_t)((val >> 16) & 0xFFFFU);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + 0U, hw0) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + 2U, hw1) != HAL_OK)
    {
        if (flash_erase_one_page(flash_page_base(addr)) != 0)
        {
            HAL_FLASH_Lock();
            return false;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + 0U, hw0) != HAL_OK ||
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + 2U, hw1) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

bool boot_slot_request_factory(void)
{
    return boot_slot_program_u32(BOOT_SLOT_FLAG_FACTORY);
}

bool boot_slot_request_app_b(void)
{
    return boot_slot_program_u32(BOOT_SLOT_FLAG_APP_B);
}

bool boot_slot_request_app_a(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    if (flash_erase_one_page(flash_page_base(BOOT_SLOT_FLAG_ADDR)) != 0)
    {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}

void boot_slot_system_reset(void)
{
    __disable_irq();
    NVIC_SystemReset();
}

bool boot_slot_running_from_b(void)
{
    return (SCB->VTOR == BOOT_SLOT_APP_B_VTOR);
}

uint32_t boot_slot_flag_peek(void)
{
    return *(volatile const uint32_t *)BOOT_SLOT_FLAG_ADDR;
}

bool boot_slot_toggle_partition_and_reset(void)
{
    bool ok = boot_slot_running_from_b() ? boot_slot_request_app_a() : boot_slot_request_app_b();
    if (!ok)
    {
        return false;
    }
    boot_slot_system_reset();
    return true;
}
