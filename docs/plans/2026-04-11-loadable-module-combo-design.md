# Loadable Module System — Combo Behavior Module

**Date:** 2026-04-11 (Revised 2026-04-20)
**Branch:** dynamic_combo_tapdance_leader
**Status:** Finalized Architecture

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
- **Erase granularity**: Sector-based. Erasing S2 or S3 only affects modules in that specific sector, leaving others and the EEPROM untouched.

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
    uint16_t flags;          // bit 0: enabled, bits 1-15: reserved
    uint32_t code_size;      // total size of module binary (header + code)
    uint32_t hook_bitmap;    // bitmask of hooks this module provides
    uint32_t hook_table_off; // offset from slot start to hook function pointer table
    uint32_t init_off;       // offset from slot start to init function (0 = none)
    uint32_t deinit_off;     // offset from slot start to deinit function (0 = none)
    uint32_t reserved;       // padding / future use
} module_header_t;
```

### Hook Function Pointer Table
Located at `slot_base + hook_table_off`. An array of `void*` entries, one per bit set in `hook_bitmap`. The host writes absolute flash addresses.

### How the Host Builds a Module
1. Compile with `arm-none-eabi-gcc` (Cortex-M4, Thumb).
2. Use a linker script setting `.text` origin to the specific slot address (e.g., `0x08008000` for Slot 0).
3. Resolve symbols from the firmware's `.map` file.
4. Output flat binary and prepend the `module_header_t`.

## Hook Table (Firmware Side)

The firmware maintains a global hook table:
```c
#define MODULE_HOOK_MAX 16
typedef struct {
    void*   func;       // function pointer (NULL = not hooked)
    uint8_t module_id;  // which module slot owns this hook (0xFF = none)
} module_hook_entry_t;
static module_hook_entry_t g_module_hooks[MODULE_HOOK_MAX] = {0};
```

### Initial Hook Set (Combo Behavior)
| Index | Name | Signature | QMK Callback |
|-------|------|-----------|-------------|
| 0 | `combo_should_trigger` | `bool (*)(uint16_t, combo_t*, uint16_t, keyrecord_t*)` | `combo_should_trigger()` |
| 1 | `process_combo_event` | `void (*)(uint16_t, bool)` | `process_combo_event()` |
| 2 | `get_combo_term` | `uint16_t (*)(uint16_t, combo_t*)` | `get_combo_term()` |
| 3 | `init` | `void (*)(void)` | Lifecycle Init |
| 4 | `deinit` | `void (*)(void)` | Lifecycle Deinit |

## Module Lifecycle

### Loading a Module
1. Host sends `QMKATA_CMD_SET` with `QMKATA_ID_MODULE` (Slot, Offset, Data).
2. Firmware writes chunks directly to flash.
3. Upon finalization (`offset == 0xFFFF`):
   - Validates header magic and size.
   - Checks `hook_bitmap` for conflicts (Reject if hook already occupied).
   - Populates `g_module_hooks[]` from the flash table.
   - Calls `init()` if present and sets `flags.enabled`.

### Unloading a Module
1. Host sends `QMKATA_CMD_DEL` with `QMKATA_ID_MODULE` and `slot_id`.
2. Firmware calls `deinit()` $\rightarrow$ clears `g_module_hooks[]` $\rightarrow$ invalidates magic in flash.

### Boot (Auto-Activation)
On `keyboard_post_init_user()`, the firmware scans all 8 slots. Valid and enabled modules are registered in the hook table and their `init()` functions are called.
**Safe Mode**: Holding ESC during power-on skips this scan to prevent boot-loops from buggy modules.

### Updating a Module
To update an existing module, the specific sector (S2 or S3) containing the slot must be erased.
1. Host sends a "prepare update" command for the target slot.
2. Firmware calls `deinit()` on all modules in that sector $\rightarrow$ saves other valid modules in that sector to RAM $\rightarrow$ erases sector $\rightarrow$ restores other modules to flash.
3. Host performs normal chunked `SET` load.

## SysEx Protocol

- **`QMKATA_ID_MODULE = 14`**
- **SET (Load)**: `buf[0] = slot_id (0-7)`, `buf[1..2] = offset`, `buf[3..] = data`.
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
- [ ] **Phase 1**: Linker Hardening (`stm32f4xx_common.ld` updates).
- [ ] **Phase 2**: Flash infrastructure (`module_flash.c/h`).
- [ ] **Phase 3**: Module loader core (`module_loader.c/h`).
- [ ] **Phase 4**: Combo hook dispatchers (`combo_should_trigger`, etc).
- [ ] **Phase 5**: Build integration (`module_loader.mk`, `rules.mk`).
- [ ] **Phase 6**: Safe-mode boot (ESC hold).
- [ ] **Phase 7**: Host build pipeline ( GCC $\rightarrow$ Map $\rightarrow$ Bin $\rightarrow$ SysEx).
- [ ] **Phase 8**: QMKata UI "modules" tab.
- [ ] **Phase 9**: End-to-end test with Layer Filter module.

## Key Differences from Legacy dynld System
| Aspect | Legacy dynld | Module Loader (Hardened) |
|--------|--------------|-------------------------|
| Storage | RAM buffer (3 KB) | Flash S2/S3 (32 KB) |
| Persistence | Volatile | Persistent (survives power cycle) |
| Linking | Position Independent | Host-linked against `.map` |
| Capacity | 1 KB per function | 4 KB per module (multiple hooks) |
| Safety | None | Magic validation, hook conflict check, Safe Mode |
