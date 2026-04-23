/*
    Module Loader Core - Implementation
    Handles module validation, hook registration, and boot-time activation.
*/

#include <string.h>
#include "module_loader.h"
#include "module_flash.h"
#include "print.h"

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

/* Helper: Validate the CRC-32 stored in a module's header against its
   contents. Covers [0, code_size) with the 4 bytes of the crc32 field
   treated as zero. Algorithm is CRC-32/ISO-HDLC (zlib-compatible):
   polynomial 0xEDB88320, init 0xFFFFFFFF, reflected, final XOR 0xFFFFFFFF.

   Computed byte-at-a-time without a precomputed lookup table to save
   ~1 KB of flash; at roughly 10 cycles per bit on Cortex-M4 an entire
   4 KB module hashes in a few milliseconds, which is negligible on the
   one-shot module_load and module_boot_scan paths.

   base must point to a contiguous copy of the module starting at the
   header (RAM staging buffer at module_load time, or the flash XIP
   address of a slot at module_boot_scan time — both are byte-addressable
   contiguous memory on this platform).

   Prior validate_module_layout() already ensured:
     sizeof(module_header_t) <= code_size <= MODULE_FLASH_SLOT_SIZE
   so the reads below cannot run past the caller's buffer. */
static bool validate_module_crc(const uint8_t* base, const module_header_t* header) {
    const size_t crc_off = offsetof(module_header_t, crc32);
    const size_t code_size = header->code_size;

    uint32_t crc = 0xFFFFFFFFu;

    /* Bytes before the crc32 field. */
    for (size_t i = 0; i < crc_off; i++) {
        crc ^= base[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    /* Four zero bytes standing in for the crc32 field. */
    for (size_t i = 0; i < 4; i++) {
        /* crc ^= 0 is a no-op; only the shift/poly step runs. */
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    /* Bytes after the crc32 field, up to code_size. */
    for (size_t i = crc_off + 4; i < code_size; i++) {
        crc ^= base[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    crc ^= 0xFFFFFFFFu;

    return (crc == header->crc32);
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

/* Helper: Claim a hook for a module and install its dispatch pointer
   atomically (from the point of view of a dispatcher reader).

   The dispatcher in module_dispatch.c gates every call on
   `hooks[i].func != NULL`. If we stored `func` first and `module_id`
   second, a reader that raced between the two stores would see a valid
   function pointer with a stale `module_id`, which is harmless for
   dispatch but would let `release_hook(slot=0, ...)` match an entry not
   yet owned by slot 0.

   We therefore store `module_id` first and `func` last, separated by a
   compiler barrier so the compiler cannot reorder them. On Cortex-M4 the
   pointer store itself is a single 32-bit write; any reader that observes
   `func != NULL` is guaranteed to also observe the matching `module_id`.

   The previous split-phase design (claim hooks, then populate `.func`
   from a second loop after the flash write) had the opposite ordering:
   `module_id` was set while `func` stayed NULL, so `is_hook_claimed()`
   (which tests `func != NULL`) reported the entry as *unclaimed* — a
   subsequent conflict check or parallel claim would then silently steal
   it. Folding both stores into this helper fixes that class of bug. */
static bool claim_hook(uint32_t hook_index, uint8_t module_id, void* func) {
    if (func == NULL) {
        /* A NULL dispatch pointer would leave the hook visible as
           unclaimed to is_hook_claimed() and skipped by the dispatcher —
           effectively a silent no-op. Refuse so callers can report it. */
        return false;
    }
    if (is_hook_claimed(hook_index)) {
        return false;
    }
    g_module_hooks[hook_index].module_id = module_id;
    __asm__ volatile("" ::: "memory");  /* forbid reordering of the two stores */
    g_module_hooks[hook_index].func = func;
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
    if (hdr->version != MODULE_HEADER_VERSION) {
        xprintf("mod load slot=%u rejected: version %u != %u\n",
                (unsigned)slot_id, (unsigned)hdr->version,
                (unsigned)MODULE_HEADER_VERSION);
        return false;
    }
    if (!validate_module_layout(hdr, len)) return false;

    const uint32_t* hook_table_data = (const uint32_t*)(data + hdr->hook_table_off);
    if (!validate_dispatch_hook_offsets(hdr, hook_table_data)) return false;

    /* Reject corrupted payloads before we erase flash. validate_module_layout
       has already bounded code_size, so it is safe to CRC the RAM buffer
       at this point. */
    if (!validate_module_crc(data, hdr)) return false;

    uint32_t slot_addr = MODULE_FLASH_GET_SLOT_ADDR(slot_id);

    /* Destructive step: flash erase on STM32F4 is sector-granular, not
       slot-granular. Four 4 KB slots share one 16 KB sector, so erasing
       this sector wipes every sibling module that currently lives in it.
       We call module_unload() on each sibling first so its hooks are
       released and its deinit runs, but the sibling binaries themselves
       will be lost and must be re-uploaded after this call returns.
       TODO: save siblings to RAM and restore them post-erase.

       Sibling cleanup MUST happen before the hook-conflict check below:
       if a sibling currently claims a hook that the incoming module also
       wants, the naive pre-cleanup conflict test would reject the load
       even though the sibling is about to be erased anyway. Cleaning up
       first releases those hooks so the conflict check only fires on
       genuine cross-sector collisions. */
    uint32_t sector_base = MODULE_FLASH_GET_SLOT_SECTOR(slot_id);
    for (uint8_t s = 0; s < MODULE_FLASH_SLOT_COUNT; s++) {
        if (s != slot_id && MODULE_FLASH_GET_SLOT_SECTOR(s) == sector_base) {
            module_unload(s);
        }
    }

    /* Check for hook conflicts against whatever is still claimed after
       sibling cleanup (i.e. modules living in the *other* sector). */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if ((hdr->hook_bitmap & (1U << i)) && is_hook_claimed(i)) {
            return false;
        }
    }

    if (!module_flash_erase_sector(sector_base)) return false;

    /* Write module data to flash verbatim. The host has already applied
       ABS32 relocations (rebasing ORIGIN=0 literal-pool addresses to
       the target slot's absolute XIP address) and embedded the final
       CRC over the post-reloc bytes. Because we never mutate the data
       here, the CRC stored in the header matches the bytes on flash,
       which is what module_boot_scan validates on every cold boot.
       See qmk-tools ModuleBuild.apply_relocations_and_crc. */
    size_t write_len = (len + 3) & ~3;
    if (!module_flash_write(slot_addr, (uint8_t*)data, write_len)) {
        xprintf("mod load slot=%u rejected: flash write failed at 0x%lx len=%u\n",
                (unsigned)slot_id, (unsigned long)slot_addr, (unsigned)write_len);
        return false;
    }

    /* Claim hooks and install dispatch pointers in one pass. claim_hook()
       stores module_id before func (with a compiler barrier), so any
       dispatcher that observes func != NULL also sees the matching
       module_id. If any claim fails — which should not happen after the
       conflict check above, but is possible if another path mutated the
       table between the check and here — release everything this slot
       has grabbed so far and bail. The flash contents remain written;
       a subsequent module_unload(slot_id) will invalidate them. */
    for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
        if (is_lifecycle_hook(i)) {
            continue;
        }

        if (hdr->hook_bitmap & (1U << i)) {
            void* func = (void*)(slot_addr + hook_table_data[i]);
            if (!claim_hook(i, slot_id, func)) {
                for (uint32_t j = 0; j < i; j++) {
                    if (!is_lifecycle_hook(j) && (hdr->hook_bitmap & (1U << j))) {
                        release_hook(j, slot_id);
                    }
                }
                return false;
            }
        }
    }

    /* Call init function if present. ABI: returns uint32_t; the module
       must return MODULE_INIT_MAGIC to confirm the call reached module
       code and ran to completion. Mismatch is logged as a warning but
       does not fail the load — flash is already written and hooks are
       already claimed at this point, and rolling back would cascade
       into a spurious sibling-erase on retry. The trace line is the
       authoritative evidence that the hook-table / Thumb-bit / XIP path
       is working end-to-end. */
    if (hdr->init_off > 0) {
        module_init_fn_t init_fn = (module_init_fn_t)(slot_addr + hdr->init_off);
        xprintf("mod load slot=%u init_fn=0x%lx\n",
                (unsigned)slot_id, (unsigned long)(uintptr_t)init_fn);
        uint32_t rc = init_fn();
        if (rc == MODULE_INIT_MAGIC) {
            xprintf("mod load slot=%u init OK rc=0x%lx\n",
                    (unsigned)slot_id, (unsigned long)rc);
        } else {
            xprintf("mod load slot=%u init BAD rc=0x%lx (expected 0x%lx)\n",
                    (unsigned)slot_id, (unsigned long)rc,
                    (unsigned long)MODULE_INIT_MAGIC);
        }
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
       invalidate, or mid-write garbage after a power loss). Magic mismatch
       is the common "nothing here" case and stays silent; version mismatch
       means a module from an incompatible firmware build sits in the slot,
       which is worth surfacing in the console. */
    if (header.magic != MODULE_HEADER_MAGIC) {
        return true;
    }
    if (header.version != MODULE_HEADER_VERSION) {
        xprintf("mod unload slot=%u rejected: version %u != %u\n",
                (unsigned)slot_id, (unsigned)header.version,
                (unsigned)MODULE_HEADER_VERSION);
        return true;
    }

    /* Call deinit function if present. Bounds-check deinit_off against the
       header's own code_size before using it as a jump target; the layout
       validator ran at load time but flash contents cannot be trusted
       unconditionally at runtime. Return value is logged for diagnostic
       parity with init, but not checked — at this point hooks are about
       to be released and the module invalidated regardless. */
    if (header.deinit_off > 0 && header.deinit_off >= sizeof(module_header_t) && header.deinit_off < header.code_size) {
        module_deinit_fn_t deinit_fn = (module_deinit_fn_t)(slot_addr + header.deinit_off);
        uint32_t rc = deinit_fn();
        xprintf("mod unload slot=%u deinit rc=0x%lx\n",
                (unsigned)slot_id, (unsigned long)rc);
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
            xprintf("mod boot_scan slot=%u rejected: version %u != %u\n",
                    (unsigned)slot_id, (unsigned)header.version,
                    (unsigned)MODULE_HEADER_VERSION);
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

        /* Reject any module whose flash contents no longer match the CRC
           the host signed them with. This catches power-loss mid-write
           (header committed, code region partially written), flash bit
           rot, and the trailing state left by an interrupted erase that
           the FLASH_BUSY_ERASING fix can detect but not undo. The slot
           stays as-is — boot scan is read-only — so the user can retry
           a module_load or module_unload later. */
        if (!validate_module_crc((const uint8_t*)slot_addr, &header)) {
            continue;
        }

        /* header.flags is currently unused at runtime; bit 0 was originally
           an "enabled" flag but the host always sets it and there is no
           path to toggle it, so gating activation on it was dead code.
           The field is preserved in the header layout for a future
           explicit enable/disable mechanism (see module_loader.h). */

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

        /* Claim hooks and install dispatch pointers in one pass so no
           reader ever sees a claimed hook with a NULL func. See the
           comment on claim_hook() for the store-ordering rationale. */
        for (uint32_t i = 0; i < MODULE_HOOK_MAX; i++) {
            if (is_lifecycle_hook(i)) {
                continue;
            }

            if (header.hook_bitmap & (1U << i)) {
                void* func = (void*)(slot_addr + hook_table[i]);
                claim_hook(i, slot_id, func);
            }
        }

        /* Call init function if present. Same ABI + magic contract as
           module_load(); see comment there. Boot-scan mismatch is also
           non-fatal — the module is already in flash and its hooks are
           already claimed, and boot-scan is intentionally read-only. */
        if (header.init_off > 0) {
        module_init_fn_t init_fn = (module_init_fn_t)(slot_addr + header.init_off);
        xprintf("mod boot slot=%u init_fn=0x%lx\n",
                (unsigned)slot_id, (unsigned long)(uintptr_t)init_fn);
        uint32_t rc = init_fn();
        if (rc == MODULE_INIT_MAGIC) {
            xprintf("mod boot slot=%u init OK rc=0x%lx\n",
                        (unsigned)slot_id, (unsigned long)rc);
            } else {
                xprintf("mod boot slot=%u init BAD rc=0x%lx (expected 0x%lx)\n",
                        (unsigned)slot_id, (unsigned long)rc,
                        (unsigned long)MODULE_INIT_MAGIC);
            }
        }
    }
}

module_hook_entry_t* module_get_hook_table(void) {
    return g_module_hooks;
}

uint8_t module_get_hook_count(void) {
    return MODULE_HOOK_MAX;
}
