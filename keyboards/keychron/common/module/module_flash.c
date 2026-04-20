/*
    Module Flash Infrastructure - Implementation
    Provides low-level flash access for loadable modules in Sectors 2 & 3.
    Uses the ChibiOS HAL flash driver (EFLD1).
*/

#include <string.h>
#include "hal.h"
#include "hal_efl.h"
#include "hal_flash.h"
#include "module_flash.h"

/* Flash instance - using the ChibiOS EFL driver */
static EFlashDriver *efl = &EFLD1;
static BaseFlash *flash = (BaseFlash *)&EFLD1;

/**
 * @brief Validates that an address falls within the allowed module sectors.
 */
static bool is_valid_module_address(uint32_t address) {
    return (address >= MODULE_FLASH_S2_BASE && address < (MODULE_FLASH_S2_BASE + (MODULE_FLASH_SLOT_COUNT * MODULE_FLASH_SLOT_SIZE)));
}

/**
 * @brief Validates that a sector base is either S2 or S3.
 */
static bool is_valid_sector(uint32_t sector_base) {
    return (sector_base == MODULE_FLASH_S2_BASE || sector_base == MODULE_FLASH_S3_BASE);
}

bool module_flash_write(uint32_t address, uint8_t* data, size_t len) {
    /* Validate address range */
    if (!is_valid_module_address(address)) {
        return false;
    }

    /* Validate alignment - STM32F4 flash programming works best with 4-byte alignment */
    if ((address & 0x03) != 0 || ((uintptr_t)data & 0x03) != 0) {
        return false;
    }

    /* Validate length is a multiple of 4 bytes */
    if (len % 4 != 0) {
        return false;
    }

    /* Initialize the EFL driver */
    if (eflStart(efl, NULL) != HAL_RET_SUCCESS) {
        return false;
    }

    /* Write data in 4-byte chunks */
    for (size_t i = 0; i < len; i += 4) {
        flash_error_t status = flashProgram(flash, address + i, 4, data + i);
        if (status != FLASH_NO_ERROR) {
            eflStop(efl);
            return false;
        }
    }

    eflStop(efl);
    return true;
}

bool module_flash_erase_sector(uint32_t sector_base) {
    /* Validate sector */
    if (!is_valid_sector(sector_base)) {
        return false;
    }

    /* Determine the sector number */
    flash_sector_t sector_num = flashGetOffsetSector(flash, sector_base);
    if (sector_num == UINT32_MAX) {
        return false;
    }

    /* Initialize the EFL driver */
    if (eflStart(efl, NULL) != HAL_RET_SUCCESS) {
        return false;
    }

    /* Start the erase operation */
    flash_error_t status = flashStartEraseSector(flash, sector_num);
    if (status != FLASH_NO_ERROR && status != FLASH_BUSY_ERASING) {
        eflStop(efl);
        return false;
    }

    /* Wait for the erase to complete */
    status = flashWaitErase(flash);
    if (status != FLASH_NO_ERROR && status != FLASH_BUSY_ERASING) {
        eflStop(efl);
        return false;
    }

    eflStop(efl);
    return true;
}

bool module_flash_is_sector_empty(uint32_t sector_base) {
    /* Validate sector */
    if (!is_valid_sector(sector_base)) {
        return false;
    }

    /* Read the first 4 bytes of the sector */
    uint32_t first_word = *(volatile uint32_t *)sector_base;

    /* An erased sector contains all 0xFF bytes */
    return (first_word == 0xFFFFFFFF);
}
