/*
    Module Loader Core - Header
    Defines the module header structure, hook indices, and public API.
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "module_flash.h"

/* Module Header Constants */
#define MODULE_HEADER_MAGIC 0x4D4F444C  /* "MODL" */
/* Version 5: added send_string to kbsm_env_t for multi-character
   string output (autotext module). v4 modules continue to work — the
   new field is at the end of the struct and old modules never reference it. */
#define MODULE_HEADER_VERSION 5

/* Value a module's init function must return for the loader to consider
   the init call successful. Any other return value is logged as a
   warning; the module stays loaded (hooks are already claimed and flash
   is already written by the time init runs) but the mismatch is
   evidence something is wrong in the module's boot path or the
   load/dispatch mechanism itself. Modules should include module_api.h
   (host) or this header (firmware) to get the canonical value. */
#define MODULE_INIT_MAGIC 0x600DBEEFu

/* init / deinit ABI: init takes a kbsm_env_t* (NULL for legacy
   non-kbsm modules); deinit takes no arguments. Both return uint32_t.
   Init must return MODULE_INIT_MAGIC; deinit's return value is logged
   but not checked.

   Modules that don't need any callbacks (e.g. existing combo modules)
   ignore the env argument. Behavior modules store it in module-local
   state so their handlers can call kbsm_register, tap_code16, etc.

   Module code does not receive its load address. R_ARM_ABS32
   relocations are applied host-side during upload (see qmk-tools
   ModuleBuild.apply_relocations_and_crc), rebasing literal-pool
   entries to the target slot's absolute XIP address before the bytes
   are flashed. Module code therefore references its own .rodata
   through plain C without arithmetic on a load-address parameter.
   The load address remains available to the firmware (via slot_addr)
   for logging and bounds validation, but passing it into the module
   would only invite the now-broken "module_base + (uintptr_t)sym" PIC
   pattern to double-relocate an already-rebased address. */
struct kbsm_env;
typedef uint32_t (*module_init_fn_t)(struct kbsm_env *env);
typedef uint32_t (*module_deinit_fn_t)(void);

/* Hook Indices.

   Hook constants are grouped by feature category via an infix tag
   (`COMBO`, `KEY`, `TAPDANCE`, `LEADER`) so the namespace stays
   self-documenting as it grows. Lifecycle hooks (`INIT`/`DEINIT`)
   are universal and use the unqualified `MODULE_HOOK_` prefix.

   The namespace itself is flat: a single global hook table indexed
   by these values. Each hook has a single owning module — multiple
   modules claiming the same hook is rejected at load time
   (one-hook-one-owner; see multi-module-plan.md). MODULE_HOOK_MAX is
   the size of the on-flash hook table and bounds the bitmap; it must
   be a multiple of 8 so the table size aligns to 4-byte entries. */

/* Combo hooks */
#define MODULE_COMBO_HOOK_SHOULD_TRIGGER          0
#define MODULE_COMBO_HOOK_PROCESS_EVENT           1
#define MODULE_COMBO_HOOK_GET_TERM                2
/* Lifecycle hooks (universal) */
#define MODULE_HOOK_INIT                          3
#define MODULE_HOOK_DEINIT                        4
/* Combo hooks (continued) */
#define MODULE_COMBO_HOOK_GET_MUST_HOLD           5
#define MODULE_COMBO_HOOK_GET_MUST_TAP            6
#define MODULE_COMBO_HOOK_GET_MUST_PRESS_IN_ORDER 7
#define MODULE_COMBO_HOOK_PROCESS_KEY_RELEASE     8
#define MODULE_COMBO_HOOK_PROCESS_KEY_REPRESS     9
#define MODULE_COMBO_HOOK_REF_FROM_LAYER          10
/* Key processing hooks.
   PRE_PROCESS_RECORD overrides QMK's pre_process_record_user (uncontested
   weak symbol). PROCESS_RECORD uses cooperative dispatch — keymaps call
   module_dispatch_process_record() explicitly; see module_dispatch.h. */
#define MODULE_KEY_HOOK_PRE_PROCESS_RECORD        11
#define MODULE_KEY_HOOK_PROCESS_RECORD            18
#define MODULE_KEY_HOOK_LAYER_STATE_SET           17
/* Tap dance hooks — reserved, not currently dispatched */
#define MODULE_TAPDANCE_HOOK_ON_EACH_TAP          12
#define MODULE_TAPDANCE_HOOK_ON_DANCE_FINISHED    13
#define MODULE_TAPDANCE_HOOK_ON_RESET             14
/* Leader hooks — reserved, not currently dispatched */
#define MODULE_LEADER_HOOK_START                  15
#define MODULE_LEADER_HOOK_END                    16
/* Lifecycle hooks (universal) — periodic tick and graceful shutdown */
#define MODULE_HOOK_HOUSEKEEPING                  19
#define MODULE_HOOK_SHUTDOWN                      20

/* Behavior machine hooks — for modules that plug into the kbsm
   orchestrator (quantum/kbsm.c). A behavior module exports a single
   kbsm_t* via GET_MACHINE, then calls env->kbsm_register on
   it in its init function. Unload calls env->kbsm_unregister. */
#define MODULE_KBSM_HOOK_GET_MACHINE          21

#define MODULE_HOOK_MAX                           32

/* Module Header Structure (32 bytes).
   crc32 covers the bytes [0, code_size), with the 4 bytes of the
   crc32 field itself treated as zero during computation. Algorithm is
   CRC-32/ISO-HDLC (zlib-compatible): polynomial 0xEDB88320, init
   0xFFFFFFFF, input and output reflected, final XOR 0xFFFFFFFF. */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /* 0x4D4F444C ("MODL") */
    uint16_t version;        /* module format version (2) */
    uint16_t flags;          /* reserved for future use (e.g. explicit enable/disable); must be 0 */
    uint32_t code_size;      /* total size of module binary (header + hook table + code) */
    uint32_t hook_bitmap;    /* bitmask of hooks this module provides */
    uint32_t hook_table_off; /* offset from slot start to hook function pointer table */
    uint32_t init_off;       /* offset from slot start to init function (0 = none) */
    uint32_t deinit_off;     /* offset from slot start to deinit function (0 = none) */
    uint32_t crc32;          /* CRC-32/ISO-HDLC over [0, code_size) with this field zeroed */
} module_header_t;

_Static_assert(sizeof(module_header_t) == 32, "module_header_t must be 32 bytes");

/* Hook Entry Structure */
typedef struct {
    void*   func;       /* function pointer (NULL = not hooked) */
    uint8_t module_id;  /* which module slot owns this hook (0xFF = none) */
} module_hook_entry_t;

/* Public API */

/**
 * @brief Load a module into a specific slot.
 *
 * The caller's buffer is read verbatim and programmed to flash without
 * modification. The host (qmk-tools ModuleBuild.apply_relocations_and_crc)
 * has already rebased ABS32 literal-pool entries to the target slot's
 * absolute XIP address and embedded the final CRC over the post-reloc
 * bytes, so the header CRC matches what module_boot_scan reads back on
 * every cold boot.
 *
 * @param slot_id The slot ID (0-7 for flash, 8+ for SRAM if MODULE_SRAM_ENABLE).
 * @param data Pointer to the module binary data (including header).
 * @param len Length of the data in bytes.
 * @return true if the module was loaded successfully, false otherwise.
 *
 * Slot IDs 0-7 dispatch to flash. Slot IDs >= 8 require MODULE_SRAM_ENABLE
 * and dispatch to SRAM (volatile, lost on reset). The host applies
 * relocations against the SRAM slot address in the same way it does for
 * flash slots.
 */
bool module_load(uint8_t slot_id, const uint8_t* data, size_t len);

/* Module load target. Internal — module_load() picks the target from
   slot_id, but the dispatcher is exposed for diagnostics. */
typedef enum {
    MODULE_TARGET_FLASH = 0,
    MODULE_TARGET_SRAM  = 1,
} module_target_t;

/**
 * @brief Mark a sector as erased to suppress redundant erases in module_load().
 * @param sector_base The sector base address (MODULE_FLASH_S2_BASE or S3_BASE).
 */
void module_loader_mark_sector_erased(uint32_t sector_base);

/**
 * @brief Clear the sector-erased flag. Called after sector-preserving reload
 * completes so the next independent load erases normally.
 */
void module_loader_clear_sector_erased(void);

/**
 * @brief Unload a module from a specific slot.
 * @param slot_id The slot ID (0-7).
 * @return true if the module was unloaded successfully, false otherwise.
 */
bool module_unload(uint8_t slot_id);

/**
 * @brief Scan all slots and activate valid modules during boot.
 */
void module_boot_scan(void);

/**
 * @brief Get the global hook table.
 * @return Pointer to the hook table array.
 */
module_hook_entry_t* module_get_hook_table(void);

/**
 * @brief Get the number of hooks in the table.
 * @return The number of hooks.
 */
uint8_t module_get_hook_count(void);
