# Loadable Module System — Combo Behavior Module

**Date:** 2026-04-11 (Revised 2026-04-21)
**Branch:** feat/combo-modules
**Status:** Implemented (see Outstanding Issues)

## Overview

Add a loadable module system to the Keychron Q3 Max firmware. A module is a pre-linked blob of ARM Thumb code compiled on the host against the firmware's symbol map (`.map` file). The firmware stores modules in flash, executes them in-place (XIP), and dispatches to them through a fixed hook table.

The first module type targets **combo behavior customization**: the EEPROM combo data definitions remain as-is, but a loaded module can override QMK combo callbacks (`combo_should_trigger`, `get_combo_term`, `process_combo_event`) to add advanced behaviors such as per-layer filtering, per-combo timing, and state-dependent triggering.

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Who links? | Host | Keeps firmware lean; host has full toolchain + `.map` file |
| Execution location | Flash (XIP) | Saves 3 KB RAM; STM32F4 supports execute-in-place from internal flash |
| Multiple modules? | Yes | Independent modules coexist, each claiming different hooks |
| Hook conflicts | Reject | Loading fails if another module already claims a hook; explicit, no silent breakage |
| Module storage | Sectors 2 & 3 | Isolates modules from EEPROM (S1) and Main Code (S4/S5); provides 32 KB space |
| Boot behavior | Auto-activate from flash | Modules persist across power cycles; safe-mode boot skips activation |

## MCU Constraints (STM32F401xC)

- **Flash layout**: 256 KB total.
- **Sector 0 (16 KB)**: Mandatory for vectors and startup.
- **Sector 1 (16 KB)**: Dedicated to emulated EEPROM.
- **Sectors 2 & 3 (32 KB)**: Dedicated to Loadable Modules.
- **Sectors 4 & 5 (192 KB)**: Main firmware body.
- **Erase granularity**: Sector-based (16 KB). Erasing S2 or S3 leaves the other sector and the EEPROM untouched, but **within a sector all 4 slots share the same erase unit** — loading any slot erases all its siblings. See *Outstanding Issues* below.
- **HAL Layer**: Flash operations use the ChibiOS HAL (`flashProgram`, `flashStartEraseSector`, `flashWaitErase`).

## Flash Layout

```
Sector 0 (16 KB):  0x08000000 -> Firmware Startup/Vectors
Sector 1 (16 KB):  0x08004000 -> EEPROM Emulation Cache
Sector 2 (16 KB):  0x08008000 -> Module Slots 0-3 (4 KB each)
Sector 3 (16 KB):  0x0800C000 -> Module Slots 4-7 (4 KB each)
Sector 4+5 (192 KB): 0x08010000 -> Main Firmware Body (.text, .rodata)
```

There are 8 module slots of 4 KB each. This provides significant space for complex logic and multiple functions per module, a substantial increase over the legacy `dynld` system.

## Module Binary Format

The host compiles a C source file into a position-dependent Thumb binary targeted at the slot's fixed flash address.

### Module Header (32 bytes, at start of slot)
```c
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 0x4D4F444C ("MODL")
    uint16_t version;        // module format version (1)
    uint16_t flags;          // reserved for future use (e.g. explicit enable/disable); must be 0
    uint32_t code_size;      // total size of module binary (header + code)
    uint32_t hook_bitmap;    // bitmask of hooks this module provides
    uint32_t hook_table_off; // offset from slot start to hook function pointer table
    uint32_t init_off;       // offset from slot start to init function (0 = none)
    uint32_t deinit_off;     // offset from slot start to deinit function (0 = none)
    uint32_t crc32;          // CRC-32/ISO-HDLC over [0, code_size) with this field zeroed
} module_header_t;
```

The `crc32` field is computed by the host with `zlib.crc32` (CRC-32/ISO-HDLC:
polynomial `0xEDB88320`, init `0xFFFFFFFF`, reflected, final XOR `0xFFFFFFFF`)
over the entire binary with the `crc32` field itself treated as four zero
bytes during the computation. The firmware recomputes it with the same
convention and rejects the module if the values disagree. See
`validate_module_crc()` in `module_loader.c` and the matching host code in
`ModuleBuild._assemble()` / `ModuleTab._prepare_binary_for_load()`.

### Hook Function Pointer Table
Located at `slot_base + hook_table_off`. A fixed-size array of `MODULE_HOOK_MAX` (16) `uint32_t` entries. Each entry is a **slot-relative byte offset** from the slot base to the hook's function body, not an absolute address. The firmware converts offsets to absolute addresses at load time (`func = slot_addr + hook_table[i]`), so the same module binary is valid in any slot.

A zero entry means "not provided" for indices whose bit is set in `hook_bitmap`; entries whose hook_bitmap bit is clear are ignored. Lifecycle hooks (`init`, `deinit`) are NOT stored in the hook table — they live in the header's `init_off` / `deinit_off` fields.

### How the Host Builds a Module
1. Compile with `arm-none-eabi-gcc` (Cortex-M4, Thumb). No unwind tables, no exceptions, no writable sections (enforced by host checks).
2. Link with a fixed-layout script: header at offset 0, hook table at offset 32, code starting at offset 96. `.text` origin is 0 (slot-relative) — the firmware adds the slot base at runtime.
3. Resolve external symbols (QMK kernel calls) from the firmware's `.map` file before linking.
4. Output flat binary and prepend the `module_header_t` with computed offsets and hook bitmap.

**Host Implementation Notes:**
- **Endianness**: All binary data is written in **little-endian** format (ARM native).
- **Alignment**: The binary is padded to a multiple of 4 bytes; the firmware rejects unaligned writes (STM32F4 word-mode `flashProgram` requirement).
- **No writable sections**: Modules run XIP from flash, so `.data` and `.bss` must be empty. Host objdump check fails the build if any module defines writable globals.
- **Flash reset state**: Erased flash reads as `0xFF`. The firmware's `module_flash_is_sector_empty()` only checks the first word of a sector as a fast-path heuristic; the per-slot header magic/version is the authoritative validity check.

## Hook Table (Firmware Side)

The firmware maintains a global hook table:
```c
#define MODULE_HOOK_MAX 16
typedef struct {
    void*   func;       // function pointer (NULL = not hooked)
    uint8_t module_id;  // which module slot owns this hook (0xFF = none)
} module_hook_entry_t;
static module_hook_entry_t g_module_hooks[MODULE_HOOK_MAX] = {
    [0 ... MODULE_HOOK_MAX - 1] = {NULL, 0xFF},
};
```
Every entry must start with `module_id = 0xFF` — a plain `{0}` initializer is unsafe because it leaves `module_id = 0x00`, which would cause `release_hook(slot=0, ...)` to match unclaimed entries.

### Defined Hooks
| Index | Symbol | QMK callback |
|-------|--------|--------------|
| 0 | `MODULE_HOOK_COMBO_SHOULD_TRIGGER` | `combo_should_trigger()` |
| 1 | `MODULE_HOOK_PROCESS_COMBO_EVENT` | `process_combo_event()` |
| 2 | `MODULE_HOOK_GET_COMBO_TERM` | `get_combo_term()` |
| 3 | `MODULE_HOOK_INIT` | Lifecycle (header `init_off`) |
| 4 | `MODULE_HOOK_DEINIT` | Lifecycle (header `deinit_off`) |
| 5 | `MODULE_HOOK_GET_COMBO_MUST_HOLD` | `get_combo_must_hold()` |
| 6 | `MODULE_HOOK_GET_COMBO_MUST_TAP` | `get_combo_must_tap()` |
| 7 | `MODULE_HOOK_GET_COMBO_MUST_PRESS_IN_ORDER` | `get_combo_must_press_in_order()` |
| 8 | `MODULE_HOOK_PROCESS_COMBO_KEY_RELEASE` | `process_combo_key_release()` |
| 9 | `MODULE_HOOK_PROCESS_COMBO_KEY_REPRESS` | `process_combo_key_repress()` |
| 10 | `MODULE_HOOK_COMBO_REF_FROM_LAYER` | `combo_ref_from_layer()` |
| 11–15 | *(reserved)* | |

Indices 3 and 4 (`INIT`, `DEINIT`) exist for bitmap accounting but are never used as dispatch-table entries — the firmware detects them via `is_lifecycle_hook()` and skips them in the claim/release loops.

## Module Lifecycle

### Loading a Module
1. Host sends `QMKATA_CMD_SET` chunks with `QMKATA_ID_MODULE` (`slot_id`, `offset`, data). Chunks stage into a RAM buffer (`module_chunk_buf`, one slot's worth) in the SysEx handler.
2. On finalize chunk (`offset == 0xFFFF`), the handler calls `module_load(slot_id, buf, len)`.
3. `module_load()`:
   - Validates the header in RAM (magic, version, layout, hook-offset bounds) before touching flash.
   - Verifies the header's `crc32` against the RAM payload so a corrupted transmission is rejected before any erase is performed.
   - Rejects if any requested hook is already claimed (first-come-first-served conflict resolution, no silent override).
   - Calls `module_unload()` on every **sibling slot in the same sector** to release their hooks and run their `deinit` — but the sibling binaries are then lost to the sector erase (see *Outstanding Issues*).
   - Erases the sector, programs the new module into its slot, claims hooks, populates the dispatch table (adding `slot_addr` to each stored offset), and invokes the module's `init` if present.

### Unloading a Module
1. Host sends `QMKATA_CMD_DEL` with `QMKATA_ID_MODULE` and `slot_id`.
2. Firmware reads the header; if magic/version do not match a valid module, the unload is a silent no-op success (idempotent — important because `module_load()` uses this path for sibling cleanup on blank slots too).
3. Otherwise: calls `deinit()` if present → releases every hook claimed by this slot → overwrites the on-flash header with zeros (no sector erase needed; STM32F4 NOR flash always allows `1→0` transitions).

### Boot (Auto-Activation)
On `keyboard_post_init_user()`, the firmware scans all 8 slots. Each slot is validated (magic, version, layout, hook bounds, CRC-32 against flash contents) and, if none of its hooks conflict with an earlier module, its hooks are claimed and its `init` is called. The CRC check is what makes power-loss-mid-write detectable: a header that committed to flash before the following code region finished writing will no longer match the CRC the host signed it with, and the module is skipped instead of executed.

**The boot scan is strictly read-only**: a module that fails validation or loses a hook conflict is skipped, never erased. Writing to flash during boot risks corrupting siblings (sector-granular erase) and would leave the keyboard unbootable on power loss mid-write. Inert (unclaimed) modules are cleaned up on the next explicit `module_load`/`module_unload` from the host.

**Safe Mode**: Holding `KC_DEL` at boot (when `keyboard_post_init_user` runs) skips module activation entirely, letting the user unload a buggy module that would otherwise crash the firmware or hijack input. The check drives ~10 matrix scans across the debounce window before reading, because `matrix_scan()` has not yet run when `keyboard_post_init_user` is called.

### Updating a Module
There is currently no safe update path. Replacing a module requires erasing its sector, which destroys every sibling module in that sector — the host must re-upload all siblings afterwards. See *Outstanding Issues*.

## SysEx Protocol

- **`QMKATA_ID_MODULE = 14`**
- **SET (Load)**: `buf[0] = slot_id (0-7)`, `buf[1..2] = offset (uint16_t LE)`, `buf[3..] = data`.
- **DEL (Unload)**: `buf[0] = slot_id (0-7)`.
- **GET (Query)**: `buf[0] = slot_id (0-7)` or `0xFF` for summary.

## Firmware File Map

| File | Purpose |
|------|---------|
| `keyboards/keychron/common/module/module_loader.h` | Header, Hook Indices, Public API |
| `keyboards/keychron/common/module/module_loader.c` | Loader logic, validation, boot scan |
| `keyboards/keychron/common/module/module_loader.mk` | Build integration |
| `keyboards/keychron/common/module/module_flash.h` | S2/S3 address constants |
| `keyboards/keychron/common/module/module_flash.c` | Low-level flash program/erase |

## RAM Budget
| Item | Cost | Note |
|------|------|------|
| `g_module_hooks` | 96 B | 16 entries $\times$ 6 bytes |
| Module RAM pool | 256 B | Optional pool for mutable module state |
| **Total** | **~352 B** | Significant savings over legacy `dynld_func_buf` (3 KB) |

## Build Sequence
- [x] **Phase 1**: Linker Hardening (`stm32f4xx_common.ld` updates).
- [x] **Phase 2**: Flash infrastructure (`module_flash.c/h`).
- [x] **Phase 3**: Module loader core (`module_loader.c/h`).
- [x] **Phase 4**: Combo hook dispatchers (`combo_should_trigger`, etc).
- [x] **Phase 5**: Build integration (`module_loader.mk`, `rules.mk`).
- [x] **Phase 6**: Safe-mode boot (KC_DEL hold).
- [x] **Phase 7**: Host build pipeline (GCC → Map → Bin → SysEx).
- [x] **Phase 8**: QMKata UI "modules" tab.
- [x] **Phase 9**: End-to-end test with Layer Filter module.

## Outstanding Issues

1. **Sibling data loss on load (P0).** Because 4 slots share one 16 KB sector and flash erase is sector-granular, loading any module erases the binaries of the three siblings in its sector. `module_load()` calls `module_unload()` on each sibling first so their hooks/deinit are handled cleanly, but the flash contents are lost and must be re-uploaded by the host. A proper fix reads siblings into RAM before erase and writes them back afterward (needs ~12 KB RAM scratch worst case, or a per-sibling read/write tango). The code contains a `TODO` marker at the erase call site.

### Resolved

- **Dead "enabled bit" check.** The host always set `flags & 0x01` and no code path ever cleared it, so gating activation on it was dead code. Removed; `flags` is now documented as reserved and must be zero.
- **No content hash validation.** A CRC-32/ISO-HDLC over the full module binary is now stored in the header (last 4 bytes). The host computes it with `zlib.crc32`; the firmware recomputes it at load time (against the RAM buffer, before erase) and at boot time (against the flash XIP address) and skips any module whose bytes no longer match. This detects power-loss mid-write, bit rot, and corruption left behind by interrupted erases.

## Key Differences from Legacy dynld System
| Aspect | Legacy dynld | Module Loader (Hardened) |
|--------|--------------|-------------------------|
| Storage | RAM buffer (3 KB) | Flash S2/S3 (32 KB) |
| Persistence | Volatile | Persistent (survives power cycle) |
| Linking | Position Independent | Host-linked against `.map`, slot-relative offsets |
| Capacity | 1 KB per function | 4 KB per module (multiple hooks) |
| Safety | None | Magic/version validation, layout bounds checks, hook-offset bounds checks, CRC-32 content check, hook conflict rejection, safe-mode boot, read-only boot scan |
