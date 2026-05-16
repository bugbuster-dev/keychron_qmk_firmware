/*
    Module SRAM Infrastructure - Implementation

    Static buffer in .bss. If MODULE_SRAM_TOTAL_SIZE exceeds remaining
    SRAM, the linker fails with "region 'ram0' overflowed by N bytes".
    That is the intended budget-enforcement mechanism — do not paper
    over it by switching to heap allocation.

    Cortex-M4 default memory map (ARMv7-M ARM B3.1) marks 0x20000000
    region as Normal/Non-shareable/Executable. ChibiOS does not configure
    MPU on the Q3 Max, so execution from this buffer works without any
    MPU programming. The Cortex-M4 has no I-cache, so DSB/ISB after
    memcpy is the only barrier needed before jumping into loaded code.
*/

#include <string.h>
#include "module_sram.h"
#include "module_loader.h"  /* for MODULE_HEADER_VERSION sanity (none today) */

/* Compile-time sanity checks. */
_Static_assert(MODULE_SRAM_TOTAL_SIZE > 0,
               "MODULE_SRAM_TOTAL_SIZE must be > 0");
_Static_assert(MODULE_SRAM_TOTAL_SIZE % MODULE_SRAM_SLOT_SIZE == 0,
               "MODULE_SRAM_TOTAL_SIZE must be a multiple of MODULE_SRAM_SLOT_SIZE");
_Static_assert(MODULE_SRAM_SLOT_COUNT >= 1 && MODULE_SRAM_SLOT_COUNT <= 8,
               "Unreasonable SRAM slot count");

/* The buffer. 8-byte aligned so any access pattern (including 64-bit
   loads if the module ever uses them) is well-defined. Placed in .bss
   so it doesn't bloat the firmware image. */
__attribute__((aligned(8)))
static uint8_t g_module_sram[MODULE_SRAM_TOTAL_SIZE];

/* Per-slot loaded flag. */
static bool g_slot_loaded[MODULE_SRAM_SLOT_COUNT] = {0};

uint32_t module_sram_slot_addr(uint8_t slot_id) {
    if (!module_sram_is_sram_slot(slot_id)) {
        return 0;
    }
    uint32_t off = (uint32_t)(slot_id - MODULE_SRAM_SLOT_BASE_ID) * MODULE_SRAM_SLOT_SIZE;
    return (uint32_t)&g_module_sram[off];
}

void module_sram_clear(uint8_t slot_id) {
    if (!module_sram_is_sram_slot(slot_id)) {
        return;
    }
    uint32_t off = (uint32_t)(slot_id - MODULE_SRAM_SLOT_BASE_ID) * MODULE_SRAM_SLOT_SIZE;
    memset(&g_module_sram[off], 0xFF, MODULE_SRAM_SLOT_SIZE);
    g_slot_loaded[slot_id - MODULE_SRAM_SLOT_BASE_ID] = false;
}

bool module_sram_write(uint32_t address, const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return false;
    }
    uint32_t buf_base = (uint32_t)&g_module_sram[0];
    uint32_t buf_end = buf_base + MODULE_SRAM_TOTAL_SIZE;
    if (address < buf_base || address >= buf_end || (address + len) > buf_end) {
        return false;
    }
    memcpy((void*)address, data, len);

    /* Data Synchronization Barrier + Instruction Synchronization Barrier.
       Ensures the memcpy is observable before subsequent instruction
       fetches from the same region. No-op cost on Cortex-M4 when there's
       no pending memory traffic, but cheap insurance. */
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    return true;
}

bool module_sram_slot_is_loaded(uint8_t slot_id) {
    if (!module_sram_is_sram_slot(slot_id)) {
        return false;
    }
    return g_slot_loaded[slot_id - MODULE_SRAM_SLOT_BASE_ID];
}

void module_sram_slot_set_loaded(uint8_t slot_id, bool loaded) {
    if (!module_sram_is_sram_slot(slot_id)) {
        return;
    }
    g_slot_loaded[slot_id - MODULE_SRAM_SLOT_BASE_ID] = loaded;
}
