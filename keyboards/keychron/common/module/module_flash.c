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
#include "print.h"

/* Flash instance - using the ChibiOS EFL driver */
static EFlashDriver *efl = &EFLD1;
static BaseFlash *flash = (BaseFlash *)&EFLD1;

/* ChibiOS EFL API takes offsets relative to FLASH_BASE, not absolute
 * addresses. Module code works in absolute addresses (XIP view) so we
 * convert at the boundary. FLASH_BASE on STM32 is 0x08000000. */
#define MODULE_FLASH_ABS_TO_OFFSET(abs) ((flash_offset_t)((abs) - FLASH_BASE))

/**
 * @brief Normalize the flash controller state before calling eflStart().
 *
 * The DFU bootloader leaves the STM32F4 flash controller unlocked
 * (CR.LOCK=0) and may leave stale operation bits or error flags set.
 * ChibiOS's eflStart() assumes CR is locked and performs the KEY1/KEY2
 * unlock sequence unconditionally. Per RM0368 §3.5.1, writing the key
 * sequence to an already-unlocked register "locks up FLASH_CR until
 * next reset" — a hardware wedge that NACKs subsequent CR writes on
 * the AHB bus, stalling XIP instruction fetch and hanging the CPU.
 *
 * This helper:
 *   1. Waits for any in-flight bootloader operation (BSY=0).
 *   2. Clears sticky error flags in SR.
 *   3. Forces CR.LOCK=1 so eflStart()'s unlock sequence runs against
 *      a known-locked controller.
 *
 * Safe to call when CR is already locked (the CR write is ignored).
 */
static void prepare_flash_controller(void) {
    while (FLASH->SR & FLASH_SR_BSY) { }
    FLASH->SR = 0x0000FFFFU;
    FLASH->CR |= FLASH_CR_LOCK;
}

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

    /* Validate alignment. STM32F4 flashProgram() in word mode (used below)
     * requires both the target address and the source buffer to be 4-byte
     * aligned — unaligned accesses cause a FLASH_ERROR_HW from the HAL. */
    if ((address & 0x03) != 0 || ((uintptr_t)data & 0x03) != 0) {
        return false;
    }

    /* Validate length is a multiple of 4 bytes */
    if (len % 4 != 0) {
        return false;
    }

    prepare_flash_controller();

    /* Initialize the EFL driver */
    if (eflStart(efl, NULL) != HAL_RET_SUCCESS) {
        return false;
    }

    /* Write the entire block in one call. The ChibiOS EFL driver programs
     * in flash lines (32 bytes on STM32F4) — it fills each line buffer
     * with 0xFF, copies the requested bytes, then programs the whole line.
     * Calling it repeatedly with small n overwrites previous lines with
     * 0xFF. A single call lets the driver handle line alignment internally. */
    flash_error_t status = flashProgram(flash, MODULE_FLASH_ABS_TO_OFFSET(address), len, data);
    if (status != FLASH_NO_ERROR) {
        eflStop(efl);
        return false;
    }

    eflStop(efl);
    return true;
}

bool module_flash_erase_sector(uint32_t sector_base) {
    /* Validate sector */
    if (!is_valid_sector(sector_base)) {
        return false;
    }

    /* Determine the sector number. flashGetOffsetSector expects an
     * offset from FLASH_BASE (0x08000000). Passing the absolute XIP
     * address silently fails the internal range check and the function
     * returns sector 0 — which would erase the vector table and brick
     * the MCU on the next interrupt. */
    flash_sector_t sector_num = flashGetOffsetSector(flash, MODULE_FLASH_ABS_TO_OFFSET(sector_base));
    if (sector_num == UINT32_MAX) {
        return false;
    }

    prepare_flash_controller();

    /* Initialize the EFL driver */
    if (eflStart(efl, NULL) != HAL_RET_SUCCESS) {
        return false;
    }

    /* Start the erase operation. FLASH_BUSY_ERASING here indicates another
     * erase is in progress for this device, not an error — flashWaitErase()
     * below will block until the sector is actually done. */
    flash_error_t status = flashStartEraseSector(flash, sector_num);
    if (status != FLASH_NO_ERROR && status != FLASH_BUSY_ERASING) {
        eflStop(efl);
        return false;
    }

    /* Wait for the erase to complete. Only FLASH_NO_ERROR is a valid success
     * result here: flashWaitErase() must not return until the controller is
     * idle, so FLASH_BUSY_ERASING at this point means the wait was aborted
     * or timed out and the sector is in an indeterminate (possibly partially
     * erased) state. Returning true in that case would cause the caller's
     * subsequent flashProgram() to write into a sector whose bits are not
     * guaranteed to be 1, producing silent corruption. */
    status = flashWaitErase(flash);
    if (status != FLASH_NO_ERROR) {
        eflStop(efl);
        return false;
    }

    eflStop(efl);
    return true;
}

bool module_flash_write_with_relocs(uint8_t slot_id,
                                    uint32_t slot_addr,
                                    uint8_t* buf,
                                    size_t len,
                                    uint32_t reloc_off,
                                    uint32_t reloc_count) {
    /* Structural consistency between the two fields has already been
     * enforced by module_load() (they must be both-zero or both-nonzero,
     * reloc_off must be 4-aligned and sit past the header+hook table, and
     * the table must fit within code_size). Here we only walk the entries,
     * bound-check each patch site against the buffer we are about to
     * program, and apply the fix-up in place.
     *
     * Each entry is a 4-byte offset within the image at which a 32-bit
     * word currently holds a link-time address (ORIGIN=0 based) that must
     * be rebased to the absolute slot address by adding slot_addr. Patch
     * sites must be 4-aligned and must not straddle the reloc table
     * itself (we refuse to patch the table that describes the patches). */
    if (reloc_count > 0) {
        const uint32_t *table = (const uint32_t *)(buf + reloc_off);
        for (uint32_t i = 0; i < reloc_count; i++) {
            uint32_t patch_off = table[i];
            if ((patch_off & 3u) != 0 || patch_off + 4u > reloc_off) {
                xprintf("mod load slot=%u rejected: bad reloc[%lu] off=0x%lx (reloc_off=0x%lx)\n",
                        (unsigned)slot_id, (unsigned long)i,
                        (unsigned long)patch_off, (unsigned long)reloc_off);
                return false;
            }
            if (patch_off + 4u > len) {
                /* Should already be caught by patch_off < reloc_off and
                 * reloc_off < code_size <= len, but defend against a
                 * caller that passed a truncated buffer. */
                xprintf("mod load slot=%u rejected: reloc[%lu] off=0x%lx past buffer (len=%u)\n",
                        (unsigned)slot_id, (unsigned long)i,
                        (unsigned long)patch_off, (unsigned)len);
                return false;
            }
            uint32_t *word = (uint32_t *)(buf + patch_off);
            *word += slot_addr;
        }
    }

    if (!module_flash_write(slot_addr, buf, len)) {
        xprintf("mod load slot=%u rejected: flash write failed at 0x%lx len=%u\n",
                (unsigned)slot_id, (unsigned long)slot_addr, (unsigned)len);
        return false;
    }
    return true;
}

bool module_flash_is_sector_empty(uint32_t sector_base) {
    /* Validate sector */
    if (!is_valid_sector(sector_base)) {
        return false;
    }

    /* Quick blank-check: read only the first word. This is a fast-path
     * optimization used by the boot scanner — if the sector-base word is
     * 0xFFFFFFFF the sector has almost certainly never been written, so we
     * can skip header parsing for all 4 slots in it. A partially-written
     * sector (power loss mid-program) may still return true here, but the
     * per-slot magic/version check downstream will reject any slot whose
     * header is not fully valid, so false positives are safe. */
    uint32_t first_word = *(volatile uint32_t *)sector_base;

    return (first_word == 0xFFFFFFFF);
}
