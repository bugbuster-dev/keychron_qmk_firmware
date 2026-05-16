/*
    Module SRAM Infrastructure - Header

    Provides a fixed, executable SRAM region for modules that need to be
    hot-loaded without flash wear (e.g. pipeline state-machine features
    during development). Modules loaded into SRAM are volatile — lost on
    reset — but otherwise behave identically to flash modules: same
    module_header_t, same hook registration, same init/deinit lifecycle.

    The SRAM region is a static buffer declared in module_sram.c. If the
    buffer doesn't fit in the remaining SRAM after .data/.bss/.heap, the
    LINK FAILS with "region 'ram0' overflowed" — by design, so the
    budget is enforced at build time rather than masked as runtime OOM.

    Slot ID space:
      0-7   flash slots (Sectors 2 & 3, 4KB each)
      8+    SRAM slots  (this module, MODULE_SRAM_SLOT_SIZE each)
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Total SRAM reserved for modules. Override per-keymap via
   OPT_DEFS += -DMODULE_SRAM_TOTAL_SIZE=N. Default 4 KB = one slot. */
#ifndef MODULE_SRAM_TOTAL_SIZE
#    define MODULE_SRAM_TOTAL_SIZE 0x1000
#endif

/* Per-slot size. Match flash slot size so module binaries are portable
   between flash and SRAM without rebuild (the host only needs a different
   slot_addr for relocations). */
#ifndef MODULE_SRAM_SLOT_SIZE
#    define MODULE_SRAM_SLOT_SIZE 0x1000
#endif

#define MODULE_SRAM_SLOT_COUNT (MODULE_SRAM_TOTAL_SIZE / MODULE_SRAM_SLOT_SIZE)

/* Global slot ID space: flash slots are 0..MODULE_SRAM_SLOT_BASE_ID-1,
   SRAM slots start at MODULE_SRAM_SLOT_BASE_ID. */
#define MODULE_SRAM_SLOT_BASE_ID 8

/* True if slot_id refers to an SRAM slot. */
static inline bool module_sram_is_sram_slot(uint8_t slot_id) {
    return slot_id >= MODULE_SRAM_SLOT_BASE_ID &&
           slot_id < (MODULE_SRAM_SLOT_BASE_ID + MODULE_SRAM_SLOT_COUNT);
}

/* Get the absolute address of an SRAM slot. Returns NULL for invalid IDs. */
uint32_t module_sram_slot_addr(uint8_t slot_id);

/* Clear an SRAM slot (memset 0xFF to mimic erased flash). */
void module_sram_clear(uint8_t slot_id);

/* Write data to a slot. Returns false on bounds error. Emits DSB/ISB
   after copy so subsequent instruction fetches see the new bytes. */
bool module_sram_write(uint32_t address, const uint8_t* data, size_t len);

/* True if the slot has been loaded (i.e. cleared and written, not raw). */
bool module_sram_slot_is_loaded(uint8_t slot_id);

/* Mark a slot as loaded / unloaded. Called by module_loader. */
void module_sram_slot_set_loaded(uint8_t slot_id, bool loaded);
