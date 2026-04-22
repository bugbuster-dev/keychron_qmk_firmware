/*
    Module Flash Infrastructure - Header
    Provides low-level flash access for loadable modules in Sectors 2 & 3.
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Sector Base Addresses */
#define MODULE_FLASH_S2_BASE 0x08008000
#define MODULE_FLASH_S3_BASE 0x0800C000

/* Slot Configuration */
#define MODULE_FLASH_SLOT_SIZE 0x1000       /* 4 KB per slot */
#define MODULE_FLASH_SECTOR_SIZE 0x4000     /* 16 KB per sector */
#define MODULE_FLASH_SLOT_COUNT 8

/* Slot Address Mapping */
#define MODULE_FLASH_SLOT_0_ADDR MODULE_FLASH_S2_BASE
#define MODULE_FLASH_SLOT_1_ADDR (MODULE_FLASH_S2_BASE + MODULE_FLASH_SLOT_SIZE)
#define MODULE_FLASH_SLOT_2_ADDR (MODULE_FLASH_S2_BASE + (MODULE_FLASH_SLOT_SIZE * 2))
#define MODULE_FLASH_SLOT_3_ADDR (MODULE_FLASH_S2_BASE + (MODULE_FLASH_SLOT_SIZE * 3))
#define MODULE_FLASH_SLOT_4_ADDR MODULE_FLASH_S3_BASE
#define MODULE_FLASH_SLOT_5_ADDR (MODULE_FLASH_S3_BASE + MODULE_FLASH_SLOT_SIZE)
#define MODULE_FLASH_SLOT_6_ADDR (MODULE_FLASH_S3_BASE + (MODULE_FLASH_SLOT_SIZE * 2))
#define MODULE_FLASH_SLOT_7_ADDR (MODULE_FLASH_S3_BASE + (MODULE_FLASH_SLOT_SIZE * 3))

/* Get the flash address for a given slot ID */
#define MODULE_FLASH_GET_SLOT_ADDR(slot_id) \
    ((slot_id) < 4 ? (MODULE_FLASH_S2_BASE + ((slot_id) * MODULE_FLASH_SLOT_SIZE)) : \
                     (MODULE_FLASH_S3_BASE + (((slot_id) - 4) * MODULE_FLASH_SLOT_SIZE)))

/* Get the sector base for a given slot ID */
#define MODULE_FLASH_GET_SLOT_SECTOR(slot_id) \
    ((slot_id) < 4 ? MODULE_FLASH_S2_BASE : MODULE_FLASH_S3_BASE)

/* Function Prototypes */

/**
 * @brief Write data to a specific flash address.
 * @param address The absolute flash address to write to.
 * @param data Pointer to the data buffer.
 * @param len Length of data in bytes.
 * @return true if the write was successful, false otherwise.
 */
bool module_flash_write(uint32_t address, uint8_t* data, size_t len);

/**
 * @brief Apply ABS32 relocations to a RAM image in place, then program
 *        the resulting bytes into a module slot.
 *
 * The module image is linked at ORIGIN=0 (see qmk-tools/qmk/QMKata/module_linker.ld)
 * so every literal-pool entry that refers to an in-module address is
 * emitted as the raw link-time offset. At load time we rebase each such
 * entry by adding @p slot_addr (the absolute XIP address of the target
 * slot) to the 32-bit word at each reloc table entry's offset. The
 * reloc table itself is a packed array of uint32_t patch offsets stored
 * at @p reloc_off bytes into @p buf and has @p reloc_count entries.
 *
 * Patching happens on the caller's RAM buffer — flash cannot be patched
 * after programming because STM32F4 flash bits are erase-once (1->0 only).
 * After successful patching, the entire buffer (including the now-stale
 * reloc table, which is kept for post-load debugging via XIP read) is
 * programmed to flash in one call to module_flash_write().
 *
 * On any validation failure this function logs an xprintf diagnostic
 * identifying @p slot_id and returns false without touching flash.
 *
 * @note The caller MUST NOT reuse @p buf after this function returns.
 *       Relocations are applied in place and are not idempotent — a
 *       second call with the same buffer would double-add @p slot_addr.
 *       On failure (either bad reloc entry or flash write failure), @p buf
 *       is left in an indeterminate state (possibly partially patched);
 *       the caller must obtain fresh bytes from the host before retrying.
 *       The current upload protocol (qmkata_sysex_handler::module_chunk_buf)
 *       satisfies this contract by re-staging on retry.
 *
 * @note @p reloc_off must be 4-byte aligned in addition to the structural
 *       bounds checked upstream in module_load(); alignment is verified
 *       on every entry individually before the 32-bit load.
 *
 * @param slot_id The slot ID (for diagnostic logging only).
 * @param slot_addr Absolute XIP base address of the target slot.
 * @param buf Pointer to the full module image (header + code + reloc table).
 *            Mutated in place with relocation fix-ups.
 * @param len Total image length in bytes (must be 4-byte aligned).
 * @param reloc_off Byte offset of the reloc table within @p buf, or 0
 *                  if the module has no relocations. Must be 4-aligned.
 * @param reloc_count Number of 4-byte entries in the reloc table, or 0
 *                    if the module has no relocations.
 * @return true if patching and flash write both succeeded.
 */
bool module_flash_write_with_relocs(uint8_t slot_id,
                                    uint32_t slot_addr,
                                    uint8_t* buf,
                                    size_t len,
                                    uint32_t reloc_off,
                                    uint32_t reloc_count);

/**
 * @brief Erase a specific sector (S2 or S3).
 * @param sector_base The base address of the sector to erase (S2_BASE or S3_BASE).
 * @return true if the erase was successful, false otherwise.
 */
bool module_flash_erase_sector(uint32_t sector_base);

/**
 * @brief Fast blank-check on a sector by reading only its first word.
 *
 * Returns true if the first 4 bytes of the sector are 0xFFFFFFFF. This is
 * a boot-path optimization to skip header parsing when a sector has never
 * been programmed; it is NOT a rigorous "all bytes are 0xFF" test. A
 * partially-written sector (e.g. power loss between erase and program)
 * may still report as empty here, so callers must still validate each
 * slot's header before trusting its contents.
 *
 * @param sector_base The base address of the sector to check.
 * @return true if the first word is erased (0xFFFFFFFF), false otherwise.
 */
bool module_flash_is_sector_empty(uint32_t sector_base);
