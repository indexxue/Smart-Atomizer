#include "boot_slot.h"
#include "stm32f1xx.h"

#define STM32F1_PAGE_SIZE  0x800U

/*
 * Flash erase/program must execute from SRAM (STM32F1 cannot fetch from flash
 * while flash is busy). ARM Compiler 5 + C99 does not accept __ramfunc, so
 * place helpers in section BootSlotRamFunc (see MDK-ARM/scatter .sct files).
 */
#if defined(__CC_ARM) || (defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050))
#pragma push
#pragma arm section code = "BootSlotRamFunc"
#endif

static uint32_t flash_page_base(uint32_t addr)
{
    return addr & ~(STM32F1_PAGE_SIZE - 1U);
}

static void flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static int flash_wait_busy(uint32_t timeout)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
        if (timeout-- == 0U)
        {
            return -1;
        }
    }
    return 0;
}

static int flash_clear_status(void)
{
    uint32_t sr = FLASH->SR;

    if ((sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) != 0U)
    {
        FLASH->SR = sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP);
        return -1;
    }

    if ((sr & FLASH_SR_EOP) != 0U)
    {
        FLASH->SR = FLASH_SR_EOP;
    }

    return 0;
}

static int flash_erase_one_page(uint32_t page_addr)
{
    if (flash_wait_busy(0x00100000U) != 0)
    {
        return -1;
    }

    flash_unlock();

    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = page_addr;
    FLASH->CR |= FLASH_CR_STRT;

    if (flash_wait_busy(0x00100000U) != 0)
    {
        FLASH->CR &= ~FLASH_CR_PER;
        flash_lock();
        return -1;
    }

    FLASH->CR &= ~FLASH_CR_PER;

    if (flash_clear_status() != 0)
    {
        flash_lock();
        return -1;
    }

    flash_lock();
    return 0;
}

static int flash_program_halfword(uint32_t addr, uint16_t data)
{
    if (flash_wait_busy(0x00100000U) != 0)
    {
        return -1;
    }

    flash_unlock();

    FLASH->CR |= FLASH_CR_PG;
    *(__IO uint16_t *)addr = data;

    if (flash_wait_busy(0x00100000U) != 0)
    {
        FLASH->CR &= ~FLASH_CR_PG;
        flash_lock();
        return -1;
    }

    FLASH->CR &= ~FLASH_CR_PG;

    if (flash_clear_status() != 0)
    {
        flash_lock();
        return -1;
    }

    flash_lock();
    return 0;
}

static bool boot_slot_program_u32(uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)BOOT_SLOT_FLAG_ADDR;
    uint32_t addr = BOOT_SLOT_FLAG_ADDR;
    uint16_t hw0 = (uint16_t)(val & 0xFFFFU);
    uint16_t hw1 = (uint16_t)((val >> 16) & 0xFFFFU);

    if (*p == val)
    {
        return true;
    }

    if (flash_program_halfword(addr + 0U, hw0) != 0 ||
        flash_program_halfword(addr + 2U, hw1) != 0)
    {
        if (flash_erase_one_page(flash_page_base(addr)) != 0)
        {
            return false;
        }
        if (flash_program_halfword(addr + 0U, hw0) != 0 ||
            flash_program_halfword(addr + 2U, hw1) != 0)
        {
            return false;
        }
    }

    return (*p == val);
}

static bool boot_slot_request_app_a_ram(void)
{
    return (flash_erase_one_page(flash_page_base(BOOT_SLOT_FLAG_ADDR)) == 0);
}

static bool boot_slot_request_app_b_ram(void)
{
    return boot_slot_program_u32(BOOT_SLOT_FLAG_APP_B);
}

static bool boot_slot_request_factory_ram(void)
{
    return boot_slot_program_u32(BOOT_SLOT_FLAG_FACTORY);
}

void boot_slot_system_reset(void)
{
    __disable_irq();
    NVIC_SystemReset();
    IWDG->KR = 0xCCCCU;
    for (;;)
    {
    }
}

#if defined(__CC_ARM) || (defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050))
#pragma pop
#endif

bool boot_slot_request_factory(void)
{
    return boot_slot_request_factory_ram();
}

bool boot_slot_request_app_b(void)
{
    return boot_slot_request_app_b_ram();
}

bool boot_slot_request_app_a(void)
{
    return boot_slot_request_app_a_ram();
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
    bool ok = boot_slot_running_from_b() ? boot_slot_request_app_a_ram() : boot_slot_request_app_b_ram();
    if (!ok)
    {
        return false;
    }
    boot_slot_system_reset();
    return false;
}
