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
#define MODULE_HEADER_VERSION 1

/* Value a module's init function must return for the loader to consider
   the init call successful. Any other return value is logged as a
   warning; the module stays loaded (hooks are already claimed and flash
   is already written by the time init runs) but the mismatch is
   evidence something is wrong in the module's boot path or the
   load/dispatch mechanism itself. Modules should include module_api.h
   (host) or this header (firmware) to get the canonical value. */
#define MODULE_INIT_MAGIC 0x600DBEEFu

/* init / deinit ABI: both take the module's Flash base address and return uint32_t.
   Init must return MODULE_INIT_MAGIC; deinit's return value is logged but not checked.
   module_base is passed to deinit for symmetry with init — modules have no writable
   .data/.bss, so any PIC string access in deinit must re-derive addresses from the
   base at call time rather than caching it from init. See module_loader.c call sites. */
typedef uint32_t (*module_init_fn_t)(uint32_t module_base);
typedef uint32_t (*module_deinit_fn_t)(uint32_t module_base);

/* Hook Indices */
#define MODULE_HOOK_COMBO_SHOULD_TRIGGER 0
#define MODULE_HOOK_PROCESS_COMBO_EVENT 1
#define MODULE_HOOK_GET_COMBO_TERM 2
#define MODULE_HOOK_INIT 3
#define MODULE_HOOK_DEINIT 4
#define MODULE_HOOK_GET_COMBO_MUST_HOLD 5
#define MODULE_HOOK_GET_COMBO_MUST_TAP 6
#define MODULE_HOOK_GET_COMBO_MUST_PRESS_IN_ORDER 7
#define MODULE_HOOK_PROCESS_COMBO_KEY_RELEASE 8
#define MODULE_HOOK_PROCESS_COMBO_KEY_REPRESS 9
#define MODULE_HOOK_COMBO_REF_FROM_LAYER 10
#define MODULE_HOOK_MAX 16

/* Module Header Structure (32 bytes).
   crc32 covers the bytes [0, code_size), with the 4 bytes of the
   crc32 field itself treated as zero during computation. Algorithm is
   CRC-32/ISO-HDLC (zlib-compatible): polynomial 0xEDB88320, init
   0xFFFFFFFF, input and output reflected, final XOR 0xFFFFFFFF. */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /* 0x4D4F444C ("MODL") */
    uint16_t version;        /* module format version (1) */
    uint16_t flags;          /* reserved for future use (e.g. explicit enable/disable); must be 0 */
    uint32_t code_size;      /* total size of module binary (header + code) */
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
 * @param slot_id The slot ID (0-7).
 * @param data Pointer to the module binary data (including header).
 * @param len Length of the data in bytes.
 * @return true if the module was loaded successfully, false otherwise.
 */
bool module_load(uint8_t slot_id, const uint8_t* data, size_t len);

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
