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
 * @brief Erase a specific sector (S2 or S3).
 * @param sector_base The base address of the sector to erase (S2_BASE or S3_BASE).
 * @return true if the erase was successful, false otherwise.
 */
bool module_flash_erase_sector(uint32_t sector_base);

/**
 * @brief Check if a sector is empty (all 0xFF).
 * @param sector_base The base address of the sector to check.
 * @return true if the sector is empty, false otherwise.
 */
bool module_flash_is_sector_empty(uint32_t sector_base);
