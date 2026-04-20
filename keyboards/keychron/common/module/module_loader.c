/*
    Module Loader Core - Implementation
    Handles module validation, hook registration, and boot-time activation.
*/

#include <string.h>
#include "module_loader.h"
#include "module_flash.h"

/* Global Hook Table */
static module_hook_entry_t g_module_hooks[MODULE_HOOK_MAX] = {{NULL, 0xFF}};

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

/* Helper: Invalidate a module by clearing its magic */
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

    /* Check for hook conflicts */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if ((hdr->hook_bitmap & (1 << i)) && is_hook_claimed(i)) {
            return false;
        }
    }

    uint32_t slot_addr = MODULE_FLASH_GET_SLOT_ADDR(slot_id);

    /* Erase sector (unload sibling modules first) */
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
        if (hdr->hook_bitmap & (1 << i)) {
            if (!claim_hook(i, slot_id)) {
                for (uint32_t j = 0; j < i; j++) {
                    if (hdr->hook_bitmap & (1 << j)) release_hook(j, slot_id);
                }
                return false;
            }
        }
    }

    /* Read hook table from flash (entries are offsets, add slot_addr) */
    if (hdr->hook_table_off > 0 && hdr->hook_table_off < len) {
        void** hook_table = (void**)(slot_addr + hdr->hook_table_off);
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (hdr->hook_bitmap & (1 << i)) {
                g_module_hooks[i].func = (void*)(slot_addr + (uint32_t)hook_table[i]);
            }
        }
    }

    /* Call init function if present */
    if (hdr->init_off > 0 && hdr->init_off < len) {
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

    /* Check if module is enabled */
    if (!(header.flags & (1 << 0))) {
        return false;
    }

    /* Call deinit function if present */
    if (header.deinit_off > 0 && header.deinit_off < header.code_size) {
        void (*deinit_fn)(void) = (void (*)(void))(slot_addr + header.deinit_off);
        deinit_fn();
    }

    /* Release all hooks claimed by this module */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (header.hook_bitmap & (1 << i)) {
            release_hook(i, slot_id);
        }
    }

    /* Invalidate the module */
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

        /* Check if enabled */
        if (!(header.flags & (1 << 0))) {
            continue;
        }

        /* Validate hook bitmap - check for conflicts */
        bool conflict = false;
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if ((header.hook_bitmap & (1 << i)) && is_hook_claimed(i)) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            /* Deactivate the module due to conflicts */
            invalidate_module(slot_addr);
            continue;
        }

        /* Claim all hooks */
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (header.hook_bitmap & (1 << i)) {
                claim_hook(i, slot_id);
            }
        }

        /* Read the hook table from flash and populate g_module_hooks */
        if (header.hook_table_off > 0 && header.hook_table_off < header.code_size) {
            void** hook_table = (void**)(slot_addr + header.hook_table_off);
            for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
                if (header.hook_bitmap & (1 << i)) {
                    g_module_hooks[i].func = (void*)(slot_addr + (uint32_t)hook_table[i]);
                }
            }
        }

        /* Call init function if present */
        if (header.init_off > 0 && header.init_off < header.code_size) {
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
