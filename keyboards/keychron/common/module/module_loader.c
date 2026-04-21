/*
    Module Loader Core - Implementation
    Handles module validation, hook registration, and boot-time activation.
*/

#include <string.h>
#include "module_loader.h"
#include "module_flash.h"

/* Global Hook Table.
   Every entry must start with module_id = 0xFF (unclaimed sentinel).
   Using a GCC range designator ensures all MODULE_HOOK_MAX entries are
   initialized — the previous {{NULL, 0xFF}} form only set element 0 and
   left the rest with module_id = 0, so release_hook() called with
   module_id = 0 could have matched an unclaimed slot. */
static module_hook_entry_t g_module_hooks[MODULE_HOOK_MAX] = {
    [0 ... MODULE_HOOK_MAX - 1] = {NULL, 0xFF},
};

/* Helper: lifecycle hooks are represented by header offsets, not dispatch table claims */
static bool is_lifecycle_hook(uint32_t hook_index) {
    return (hook_index == MODULE_HOOK_INIT) || (hook_index == MODULE_HOOK_DEINIT);
}

/* Helper: Validate module header layout against provided binary length */
static bool validate_module_layout(const module_header_t* header, size_t available_len) {
    if (header->code_size < sizeof(module_header_t)) {
        return false;
    }
    if (header->code_size > MODULE_FLASH_SLOT_SIZE) {
        return false;
    }
    if (header->code_size > available_len) {
        return false;
    }

    if (header->hook_table_off < sizeof(module_header_t)) {
        return false;
    }
    if (((uint64_t)header->hook_table_off + (uint64_t)(MODULE_HOOK_MAX * sizeof(uint32_t))) > header->code_size) {
        return false;
    }

    if (header->init_off > 0) {
        if (header->init_off < sizeof(module_header_t) || header->init_off >= header->code_size) {
            return false;
        }
    }
    if (header->deinit_off > 0) {
        if (header->deinit_off < sizeof(module_header_t) || header->deinit_off >= header->code_size) {
            return false;
        }
    }

    return true;
}

/* Helper: Validate hook offsets for all dispatch hooks claimed in hook_bitmap */
static bool validate_dispatch_hook_offsets(const module_header_t* header, const uint32_t* hook_table) {
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if (header->hook_bitmap & (1U << i)) {
            uint32_t hook_off = hook_table[i];
            if (hook_off == 0) {
                return false;
            }
            if (hook_off >= header->code_size) {
                return false;
            }
        }
    }

    return true;
}

/* Helper: Read the module header from a slot */
static bool read_module_header(uint32_t slot_addr, module_header_t* header) {
    /* Read the header from flash */
    memcpy(header, (const void*)slot_addr, sizeof(module_header_t));
    return true;
}

/* Helper: Write the module header to a slot */
static bool write_module_header(uint32_t slot_addr, const module_header_t* header) {
    return module_flash_write(slot_addr, (uint8_t*)header, sizeof(module_header_t));
}

/* Helper: Invalidate a module by overwriting its header with zeros.
   Works without a sector erase because on STM32F4 NOR flash, 1->0
   transitions are always legal; any non-zero bit in the header becomes 0,
   which guarantees magic != MODULE_HEADER_MAGIC on the next boot scan. */
static bool invalidate_module(uint32_t slot_addr) {
    module_header_t header;
    memset(&header, 0, sizeof(header));
    return write_module_header(slot_addr, &header);
}

/* Helper: Check if a hook is already claimed */
static bool is_hook_claimed(uint32_t hook_index) {
    return (g_module_hooks[hook_index].func != NULL);
}

/* Helper: Claim a hook for a module */
static bool claim_hook(uint32_t hook_index, uint8_t module_id) {
    if (is_hook_claimed(hook_index)) {
        return false;
    }
    g_module_hooks[hook_index].func = NULL;  /* Will be set from flash later */
    g_module_hooks[hook_index].module_id = module_id;
    return true;
}

/* Helper: Release a hook claimed by a module */
static void release_hook(uint32_t hook_index, uint8_t module_id) {
    if (g_module_hooks[hook_index].module_id == module_id) {
        g_module_hooks[hook_index].func = NULL;
        g_module_hooks[hook_index].module_id = 0xFF;
    }
}

bool module_load(uint8_t slot_id, const uint8_t* data, size_t len) {
    if (slot_id >= MODULE_FLASH_SLOT_COUNT) return false;
    if (len < sizeof(module_header_t)) return false;

    /* Validate header in RAM before touching flash */
    const module_header_t* hdr = (const module_header_t*)data;
    if (hdr->magic != MODULE_HEADER_MAGIC) return false;
    if (hdr->version != MODULE_HEADER_VERSION) return false;
    if (!validate_module_layout(hdr, len)) return false;

    const uint32_t* hook_table_data = (const uint32_t*)(data + hdr->hook_table_off);
    if (!validate_dispatch_hook_offsets(hdr, hook_table_data)) return false;

    /* Check for hook conflicts */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if ((hdr->hook_bitmap & (1U << i)) && is_hook_claimed(i)) {
            return false;
        }
    }

    uint32_t slot_addr = MODULE_FLASH_GET_SLOT_ADDR(slot_id);

    /* Destructive step: flash erase on STM32F4 is sector-granular, not
       slot-granular. Four 4 KB slots share one 16 KB sector, so erasing
       this sector wipes every sibling module that currently lives in it.
       We call module_unload() on each sibling first so its hooks are
       released and its deinit runs, but the sibling binaries themselves
       will be lost and must be re-uploaded after this call returns.
       TODO: save siblings to RAM and restore them post-erase. */
    uint32_t sector_base = MODULE_FLASH_GET_SLOT_SECTOR(slot_id);
    for (uint8_t s = 0; s < MODULE_FLASH_SLOT_COUNT; s++) {
        if (s != slot_id && MODULE_FLASH_GET_SLOT_SECTOR(s) == sector_base) {
            module_unload(s);
        }
    }
    if (!module_flash_erase_sector(sector_base)) return false;

    /* Write module data to flash (pad to 4-byte alignment) */
    size_t write_len = (len + 3) & ~3;
    if (!module_flash_write(slot_addr, (uint8_t*)data, write_len)) return false;

    /* Claim hooks */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if (hdr->hook_bitmap & (1U << i)) {
            if (!claim_hook(i, slot_id)) {
                for (uint32_t j = 0; j < i; j++) {
                    if (!is_lifecycle_hook(j) && (hdr->hook_bitmap & (1U << j))) {
                        release_hook(j, slot_id);
                    }
                }
                return false;
            }
        }
    }

    /* Populate dispatch hooks from validated RAM hook table (entries are slot-base offsets) */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if (hdr->hook_bitmap & (1U << i)) {
            g_module_hooks[i].func = (void*)(slot_addr + hook_table_data[i]);
        }
    }

    /* Call init function if present */
    if (hdr->init_off > 0) {
        void (*init_fn)(void) = (void (*)(void))(slot_addr + hdr->init_off);
        init_fn();
    }

    return true;
}

bool module_unload(uint8_t slot_id) {
    /* Validate slot ID */
    if (slot_id >= MODULE_FLASH_SLOT_COUNT) {
        return false;
    }

    uint32_t slot_addr = MODULE_FLASH_GET_SLOT_ADDR(slot_id);

    /* Read the header */
    module_header_t header;
    if (!read_module_header(slot_addr, &header)) {
        return false;
    }

    /* If the slot does not hold a currently-valid module, treat the unload
       as a no-op success. This makes unload idempotent and, more importantly,
       makes it safe to call from module_load()'s sibling-cleanup loop on
       blank or stale slots. We deliberately do NOT read deinit_off,
       hook_bitmap, or code_size when magic/version are wrong — those fields
       may contain arbitrary bytes (erased 0xFF, zeroed by a prior
       invalidate, or mid-write garbage after a power loss). */
    if (header.magic != MODULE_HEADER_MAGIC || header.version != MODULE_HEADER_VERSION) {
        return true;
    }

    /* Call deinit function if present. Bounds-check deinit_off against the
       header's own code_size before using it as a jump target; the layout
       validator ran at load time but flash contents cannot be trusted
       unconditionally at runtime. */
    if (header.deinit_off > 0 && header.deinit_off >= sizeof(module_header_t) && header.deinit_off < header.code_size) {
        void (*deinit_fn)(void) = (void (*)(void))(slot_addr + header.deinit_off);
        deinit_fn();
    }

    /* Release all hooks claimed by this module. release_hook() is a no-op
       on hooks not actually owned by slot_id, so a stale hook_bitmap with
       extra bits set cannot cause us to steal another module's hook. */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if (header.hook_bitmap & (1U << i)) {
            release_hook(i, slot_id);
        }
    }

    /* Invalidate the module header on flash so the next boot skips it. */
    return invalidate_module(slot_addr);
}

void module_boot_scan(void) {
    for (uint8_t slot_id = 0; slot_id < MODULE_FLASH_SLOT_COUNT; slot_id++) {
        uint32_t slot_addr = MODULE_FLASH_GET_SLOT_ADDR(slot_id);

        /* Check if the sector is empty */
        uint32_t sector_base = MODULE_FLASH_GET_SLOT_SECTOR(slot_id);
        if (module_flash_is_sector_empty(sector_base)) {
            continue;
        }

        /* Read the header */
        module_header_t header;
        if (!read_module_header(slot_addr, &header)) {
            continue;
        }

        /* Validate magic and version */
        if (header.magic != MODULE_HEADER_MAGIC) {
            continue;
        }
        if (header.version != MODULE_HEADER_VERSION) {
            continue;
        }

        /* Boot scan is read-only: failing modules are skipped, not erased.
           Writing to flash during boot risks corrupting sibling modules that
           share the sector (flash erase is sector-granular, not slot-granular)
           and leaves the keyboard unbootable if power is lost mid-write.
           A skipped module never has its hooks claimed, so it is inert;
           an explicit module_unload or module_load re-write will clear it. */
        if (!validate_module_layout(&header, MODULE_FLASH_SLOT_SIZE)) {
            continue;
        }

        const uint32_t* hook_table = (const uint32_t*)(slot_addr + header.hook_table_off);
        if (!validate_dispatch_hook_offsets(&header, hook_table)) {
            continue;
        }

        /* Check if enabled */
        if (!(header.flags & (1 << 0))) {
            continue;
        }

        /* Validate hook bitmap - check for conflicts */
        bool conflict = false;
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (is_lifecycle_hook(i)) {
                continue;
            }

            if ((header.hook_bitmap & (1U << i)) && is_hook_claimed(i)) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            /* Skip this module (see read-only boot scan rationale above).
               The earlier module that claimed the hook wins; this one is
               inert until the user explicitly unloads the winner or
               rewrites this slot. */
            continue;
        }

        /* Claim all hooks */
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (is_lifecycle_hook(i)) {
                continue;
            }

            if (header.hook_bitmap & (1U << i)) {
                claim_hook(i, slot_id);
            }
        }

        /* Read the validated hook table from flash and populate dispatch hooks */
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (is_lifecycle_hook(i)) {
                continue;
            }

            if (header.hook_bitmap & (1U << i)) {
                g_module_hooks[i].func = (void*)(slot_addr + hook_table[i]);
            }
        }

        /* Call init function if present */
        if (header.init_off > 0) {
            void (*init_fn)(void) = (void (*)(void))(slot_addr + header.init_off);
            init_fn();
        }
    }
}

module_hook_entry_t* module_get_hook_table(void) {
    return g_module_hooks;
}

uint8_t module_get_hook_count(void) {
    return MODULE_HOOK_MAX;
}
