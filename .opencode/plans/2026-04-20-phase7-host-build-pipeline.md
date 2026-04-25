# Phase 7: Host Build Pipeline — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete the firmware-to-host pipeline so a combo filter module can be compiled, uploaded, and activated on the Q3 Max keyboard.

**Architecture:** Modules linked at base 0 (slot-independent) with hook table entries as offsets; firmware adds `slot_addr`. External symbols (e.g., `layer_state`) resolved from firmware `.map` file at link time. Sector erase before write; sibling slots in the same sector are wiped (documented limitation).

**Tech Stack:** C (ARM Cortex-M4 Thumb), Python/PySide6 (QMKata host tool), arm-none-eabi-gcc toolchain, ChibiOS HAL flash driver

**Key constraint:** Modules must be pure functions + const data only (no module-local writable globals). Sufficient for combo hooks.

---

## Context

### Build command
```
make keychron/q3_max/ansi_encoder:keychron
```

### Relevant firmware files (in `/home/user/qmk/keychron_qmk_firmware/`)
- `keyboards/keychron/q3_max/qmkata_sysex_handler.c` — SysEx handler (SET/GET/DEL dispatch)
- `keyboards/keychron/common/module/module_loader.c` — Module validation, hook registration, boot scan
- `keyboards/keychron/common/module/module_loader.h` — Module header struct, hook indices, public API
- `keyboards/keychron/common/module/module_flash.c` — Low-level flash operations
- `keyboards/keychron/common/module/module_flash.h` — Flash constants, slot mapping
- `keyboards/keychron/common/module/module_dispatch.c` — Combo hook dispatchers
- `keyboards/keychron/qmkata/QMKata.h` — Command ID definitions

### Relevant host tool files (in `/home/user/qmk/qmk-tools/qmk/QMKata/`)
- `QMKataKeyboard.py` — Keyboard communication (command IDs, signals, SET/GET/DEL methods)
- `QMKata.py` — Main UI (tab registration)
- `GccToolchain.py` — Compilation and objcopy
- `GccMapfile.py` — .map file parser for symbol resolution
- `RGBDynLDAnimationTab.py` — Reference pattern for dynamic code loading UI
- `kb_scripts/kb_dynld_animation.py` — Reference pattern for scripted compile-and-load
- `keyboards/KeychronQ3Max.py` — Keyboard model with toolchain config

### Key constants
| Constant | Value | Notes |
|----------|-------|-------|
| `MODULE_HEADER_MAGIC` | `0x4D4F444C` | "MODL" in LE |
| `MODULE_HEADER_VERSION` | `1` | Current format version |
| `MODULE_FLASH_SLOT_SIZE` | `0x1000` (4096) | Max module binary size |
| `MODULE_FLASH_SLOT_COUNT` | `8` | Slots 0-7 |
| `MODULE_HOOK_MAX` | `16` | Hook table capacity |
| `QMKATA_ID_MODULE` | `14` | SysEx feature ID |
| Finalize sentinel | `0xFFFF` | offset value = commit to flash |
| Hook 0 | `combo_should_trigger` | `bool (uint16_t, combo_t*, uint16_t, keyrecord_t*)` |
| Hook 1 | `process_combo_event` | `void (uint16_t, bool)` |
| Hook 2 | `get_combo_term` | `uint16_t (uint16_t, combo_t*)` |

---

## Part A: Firmware Fixes

### Task A1: Wire SET/GET handlers into dispatch

**Files:**
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c:171-204`

**Step 1: Add SET dispatch line**

In the `if (cmd == QMKATA_CMD_SET)` block (after the LEADER line ~187), add:
```c
#if defined(MODULE_LOADER_ENABLE)
        if (id == QMKATA_ID_MODULE) _QMKATA_HANDLE_CMD_SET_FN(module)(cmd, seqnum, len, buf);
#endif
```

**Step 2: Add GET dispatch line**

In the `if (cmd == QMKATA_CMD_GET)` block (after the LEADER line ~203), add:
```c
#if defined(MODULE_LOADER_ENABLE)
        if (id == QMKATA_ID_MODULE) _QMKATA_HANDLE_CMD_GET_FN(module)(cmd, seqnum, len, buf);
#endif
```

**Step 3: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 4: Commit**
```
git add keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "fix: wire module SET/GET handlers into sysex dispatch"
```

---

### Task A2: Change hook table entries to offset-based

**Files:**
- Modify: `keyboards/keychron/common/module/module_loader.c:110-118, 227-234`

**Step 1: Update module_load() hook table reading (line ~115)**

```c
// Before:
g_module_hooks[i].func = hook_table[i];
// After:
g_module_hooks[i].func = (void*)(slot_addr + (uint32_t)hook_table[i]);
```

**Step 2: Update module_boot_scan() hook table reading (line ~232)**

Same change:
```c
// Before:
g_module_hooks[i].func = hook_table[i];
// After:
g_module_hooks[i].func = (void*)(slot_addr + (uint32_t)hook_table[i]);
```

**Step 3: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 4: Commit**
```
git add keyboards/keychron/common/module/module_loader.c
git commit -m "feat: use offset-based hook table entries for slot-independent modules"
```

---

### Task A3: Add sector erase before write in module_load

**Files:**
- Modify: `keyboards/keychron/common/module/module_loader.c:55-71`

**Step 1: Restructure module_load() to validate in RAM before erasing**

The new flow:
1. Validate header in RAM (data buffer) — magic, version
2. Check for hook conflicts
3. Unload sibling modules in the same sector
4. Erase the sector
5. Write module data to flash
6. Claim hooks and read hook table
7. Call init if present

```c
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

    /* Write module data to flash */
    if (!module_flash_write(slot_addr, (uint8_t*)data, len)) return false;

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
```

Note: The `flags` field with enabled bit is written as part of the initial data (host sets `flags=0x0001`). No post-write flag modification needed.

**Step 2: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 3: Commit**
```
git add keyboards/keychron/common/module/module_loader.c
git commit -m "feat: add sector erase before module write with sibling unload"
```

Note: Tasks A2 and A4 are subsumed by this rewrite. If executing A3, skip A2 and A4.

---

### Task A4: (Subsumed by A3) Fix flags writing

If A3 was implemented as a full rewrite of module_load(), this task is already done. The host tool sets `flags=0x0001` in the header and the firmware writes it as-is. No post-write modification.

---

### Task A5: Add chunk ACK to module SET handler

**Files:**
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c:1007-1059`

**Step 1: Add ACK responses**

After successful chunk processing (memcpy to buffer), add:
```c
    /* Send ACK */
    uint8_t resp[3] = { seqnum, QMKATA_ID_MODULE, 0 };
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
```

After finalize (module_load call), replace return with:
```c
    if (offset == 0xFFFF) {
        bool success = false;
        if (module_loading_slot != 0xFF) {
            success = module_load(module_loading_slot, module_chunk_buf, module_loading_offset);
            DBG_USR(qmkata, "module:load %s\n", success ? "OK" : "FAIL");
            module_loading_slot = 0xFF;
            module_loading_offset = 0;
        }
        uint8_t resp[3] = { seqnum, QMKATA_ID_MODULE, success ? 0 : 1 };
        qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
        return;
    }
```

On overflow error:
```c
    if (module_loading_offset + data_len > sizeof(module_chunk_buf)) {
        DBG_USR(qmkata, "module:set overflow\n");
        module_loading_slot = 0xFF;
        uint8_t resp[3] = { seqnum, QMKATA_ID_MODULE, 2 }; /* 2 = overflow */
        qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
        return;
    }
```

**Step 2: Fix module_load data length**

Note: Currently passes `sizeof(module_chunk_buf)` (4096) to module_load. Should pass `module_loading_offset` (actual data length) instead. The `module_flash_write` requires 4-byte alignment, so pad: `size_t write_len = (module_loading_offset + 3) & ~3;`

**Step 3: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 4: Commit**
```
git add keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "feat: add chunk ACK and error responses to module SET handler"
```

---

### Task A6: Add DEL handler + dispatch

**Files:**
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`
- Modify: `keyboards/keychron/qmkata/QMKata.h` (if DEL macros missing)

**Step 1: Check if DEL macros exist in QMKata.h**

Look for `_QMKATA_HANDLE_CMD_DEL` macro. If missing, add alongside the existing SET/GET macros:
```c
#define _QMKATA_HANDLE_CMD_DEL(name) \
    static void _qmkata_handle_cmd_del_##name(uint8_t cmd, uint8_t seqnum, uint8_t len, uint8_t* buf)
#define _QMKATA_HANDLE_CMD_DEL_FN(name) _qmkata_handle_cmd_del_##name
```

**Step 2: Add DEL dispatch block in qmkata_sysex_handler()**

After the GET block:
```c
    if (cmd == QMKATA_CMD_DEL) {
#if defined(MODULE_LOADER_ENABLE)
        if (id == QMKATA_ID_MODULE) _QMKATA_HANDLE_CMD_DEL_FN(module)(cmd, seqnum, len, buf);
#endif
    }
```

**Step 3: Implement DEL handler**

```c
#if defined(MODULE_LOADER_ENABLE)
_QMKATA_HANDLE_CMD_DEL(module) {
    if (len < 1) return;
    uint8_t slot_id = buf[0];

    bool success = false;
    if (slot_id < MODULE_FLASH_SLOT_COUNT) {
        success = module_unload(slot_id);
        DBG_USR(qmkata, "module:del slot=%u %s\n", slot_id, success ? "OK" : "FAIL");
    }

    uint8_t resp[3] = { seqnum, QMKATA_ID_MODULE, success ? 0 : 1 };
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
}
#endif
```

**Step 4: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 5: Commit**
```
git add keyboards/keychron/q3_max/qmkata_sysex_handler.c keyboards/keychron/qmkata/QMKata.h
git commit -m "feat: add module DEL handler for SysEx module unloading"
```

---

### Task A7: Complete GET handler with real flags and hook_bitmap

**Files:**
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c:1062-1108`

**Step 1: Read actual flags and hook_bitmap for single-slot GET**

Replace the placeholder section (lines ~1093-1107):
```c
    /* Read full header for detailed info */
    uint16_t flags = 0;
    uint32_t hook_bitmap = 0;
    if (magic == MODULE_HEADER_MAGIC) {
        module_header_t hdr;
        memcpy(&hdr, (const void*)slot_addr, sizeof(hdr));
        flags = hdr.flags;
        hook_bitmap = hdr.hook_bitmap;
    }

    uint8_t resp[13];
    resp[0] = seqnum;
    resp[1] = QMKATA_ID_MODULE;
    resp[2] = slot_id;
    resp[3]  = (magic >> 0) & 0xFF;
    resp[4]  = (magic >> 8) & 0xFF;
    resp[5]  = (magic >> 16) & 0xFF;
    resp[6]  = (magic >> 24) & 0xFF;
    resp[7]  = (flags >> 0) & 0xFF;
    resp[8]  = (flags >> 8) & 0xFF;
    resp[9]  = (hook_bitmap >> 0) & 0xFF;
    resp[10] = (hook_bitmap >> 8) & 0xFF;
    resp[11] = (hook_bitmap >> 16) & 0xFF;
    resp[12] = (hook_bitmap >> 24) & 0xFF;
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
```

**Step 2: Build and verify**

```
make keychron/q3_max/ansi_encoder:keychron
```

**Step 3: Commit**
```
git add keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "fix: return actual flags and hook_bitmap in module GET response"
```

---

## Part B: Host Tool — Protocol Layer

All host tool files are in `/home/user/qmk/qmk-tools/qmk/QMKata/`.

### Task B1: Add ID_MODULE and signal to QMKataKeyboard

**Files:**
- Modify: `QMKataKeyboard.py`

**Step 1: Add ID_MODULE to QMKataKeybCmd_v0_3**

Find `class QMKataKeybCmd_v0_3` and add:
```python
    ID_MODULE = 14
```

**Step 2: Add signal**

In QMKataKeyboard signal declarations:
```python
    signal_module_status = Signal(object)
```

**Step 3: Commit**
```
git add QMKataKeyboard.py
git commit -m "feat: add ID_MODULE command and signal to QMKataKeyboard"
```

---

### Task B2: Add module response handler

**Files:**
- Modify: `QMKataKeyboard.py` (in `sysex_response_handler` method)

**Step 1: Add ID_MODULE case**

Find the response handler. Add case for `QMKataKeybCmd.ID_MODULE`. Pattern: check `buf[0]` for feature ID, parse remaining bytes based on type.

For chunk ACKs and DEL responses (3-byte responses), set `sysex_response_seq[seqnum]`.

For GET responses (longer), emit `signal_module_status` with parsed data dict.

Follow the exact response parsing pattern used by `ID_DYNLD_FUNCTION` and `ID_COMBO`.

**Step 2: Commit**
```
git add QMKataKeyboard.py
git commit -m "feat: add module response handler to QMKataKeyboard"
```

---

### Task B3: Add module SET method (chunked upload)

**Files:**
- Modify: `QMKataKeyboard.py`

**Step 1: Implement keyb_set_module(slot_id, buf)**

Follow `keyb_set_dynld_function` pattern. Header = `[ID_MODULE, slot_id, offset_lo, offset_hi]` (4 bytes). Finalize with offset=0xFFFF. Use `send_sysex_wait` for each chunk.

**Step 2: Commit**
```
git add QMKataKeyboard.py
git commit -m "feat: add keyb_set_module chunked upload method"
```

---

### Task B4: Add module GET and DEL methods

**Files:**
- Modify: `QMKataKeyboard.py`

**Step 1: Implement keyb_get_module_summary(), keyb_get_module(slot_id), keyb_del_module(slot_id)**

Follow existing GET/DEL patterns (e.g., `keyb_get_combo`).

**Step 2: Commit**
```
git add QMKataKeyboard.py
git commit -m "feat: add module GET and DEL methods"
```

---

## Part C: Host Tool — Module Build System

### Task C1: Create module linker script

**Files:**
- Create: `QMKata/module_linker.ld`

```ld
/* Module linker script - links at base 0 for slot-independent modules */
MEMORY {
    MODULE (rx) : ORIGIN = 0, LENGTH = 0x1000
}
SECTIONS {
    .module_header : {
        . = 32;  /* Reserve space for 32-byte header (host fills post-link) */
    } > MODULE
    .hook_table : ALIGN(4) {
        KEEP(*(.hook_table))
        . = ALIGN(4);
    } > MODULE
    .text : ALIGN(4) {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > MODULE
    /DISCARD/ : {
        *(.data*)
        *(.bss*)
        *(.comment)
        *(.ARM.attributes)
        *(.ARM.exidx*)
    }
}
```

**Step 1: Commit**
```
git add module_linker.ld
git commit -m "feat: add module linker script for slot-independent modules"
```

---

### Task C2: Create module API header

**Files:**
- Create: `QMKata/module_api.h`

Minimal C header with:
- Hook index defines
- `MODULE_HOOK_TABLE` section attribute macro
- Minimal type stubs for `combo_t`, `keyrecord_t` (just enough for function signatures)
- No dependency on QMK headers

```c
#ifndef MODULE_API_H
#define MODULE_API_H

#include <stdint.h>
#include <stdbool.h>

#define MODULE_HOOK_COMBO_SHOULD_TRIGGER  0
#define MODULE_HOOK_PROCESS_COMBO_EVENT   1
#define MODULE_HOOK_GET_COMBO_TERM        2
#define MODULE_HOOK_MAX                   16

#define MODULE_HOOK_TABLE __attribute__((section(".hook_table"), used))

/* Minimal stubs - match QMK struct layout for accessed fields only */
typedef struct { const uint16_t *keys; uint16_t keycode; } combo_t;
typedef struct { struct { uint16_t key; } event; } keyrecord_t;

#ifndef COMBO_TERM
#define COMBO_TERM 50
#endif

#endif
```

**Step 1: Commit**
```
git add module_api.h
git commit -m "feat: add module API header for module C source compilation"
```

---

### Task C3: Add link() method to GccToolchain

**Files:**
- Modify: `QMKata/GccToolchain.py`

Add `link(object_files, linker_script, output_file, extra_ld_files=None)` method.
Uses `arm-none-eabi-gcc -nostdlib -nostartfiles -T <script> [-T <syms>] -o <out> <in.o>`.

**Step 1: Commit**
```
git add GccToolchain.py
git commit -m "feat: add link() method to GccToolchain for module linking"
```

---

### Task C4: Create ModuleBuild class

**Files:**
- Create: `QMKata/ModuleBuild.py`

Orchestrates: compile → resolve symbols → link → elf2bin → generate header → produce final binary.

Key methods:
- `build(source_file)` → returns `{'binary': bytes, 'hook_bitmap': int, 'size': int, 'hooks': [str]}`
- `resolve_symbols(object_file)` → uses `arm-none-eabi-nm -u` + `GccMapfile` → temp `.ld` file

Uses module-specific compiler options (no `-fPIC`, add `-ffreestanding`, `-I` for module_api.h).

**Step 1: Commit**
```
git add ModuleBuild.py
git commit -m "feat: add ModuleBuild class for module compilation pipeline"
```

---

## Part D: Host Tool — ModuleTab UI

### Task D1: Create ModuleTab

**Files:**
- Create: `QMKata/ModuleTab.py`

PySide6 widget following `RGBDynLDAnimationTab` pattern:
- Slot status grid (8 rows: slot number, status, hook names)
- Source file input + browse button
- Build button
- Slot picker (ComboBox 0-7) + Load/Unload buttons
- Refresh button
- Log area (QTextEdit)

Signals: `signal_load_module(int, bytearray)`, `signal_unload_module(int)`, `signal_refresh_modules()`

**Step 1: Commit**
```
git add ModuleTab.py
git commit -m "feat: add ModuleTab UI for building and loading modules"
```

---

### Task D2: Wire ModuleTab into QMKata.py

**Files:**
- Modify: `QMKata/QMKata.py`

- Import ModuleTab
- Instantiate in `MainWindow.init_gui()`, add as tab
- Connect signals bidirectionally (tab ↔ keyboard)

**Step 1: Commit**
```
git add QMKata.py
git commit -m "feat: wire ModuleTab into QMKata main window"
```

---

## Part E: Scripted Workflow

### Task E1: Add module methods to KeybScriptEnv

**Files:**
- Modify: `QMKata/QMKataKeyboard.py`

Add `build_module(source_file)`, `load_module(slot_id, binary)`, `unload_module(slot_id)` to KeybScriptEnv.

**Step 1: Commit**
```
git add QMKataKeyboard.py
git commit -m "feat: add module build/load methods to KeybScriptEnv"
```

---

### Task E2: Create kb_module.py example script

**Files:**
- Create: `QMKata/kb_scripts/kb_module.py`

Simple script: `kb.build_module("module_examples/combo_layer_filter.c")` → `kb.load_module(0, result['binary'])`

**Step 1: Commit**
```
git add kb_scripts/kb_module.py
git commit -m "feat: add example kb_module.py script"
```

---

## Part F: End-to-End Test

### Task F1: Create combo layer filter test module

**Files:**
- Create: `QMKata/module_examples/combo_layer_filter.c`

```c
#include "module_api.h"
extern uint32_t layer_state;

bool combo_should_trigger(uint16_t combo_index, combo_t *combo,
                          uint16_t keycode, keyrecord_t *record) {
    if (layer_state & (1u << 2)) return false;  /* Disable combos on layer 2 */
    return true;
}

MODULE_HOOK_TABLE
const void* module_hook_table[16] = {
    [0] = (const void*)combo_should_trigger,
};
```

### Task F2: End-to-end verification

1. Build firmware: `make keychron/q3_max/ansi_encoder:keychron`
2. Launch QMKata, open "modules" tab
3. Build module → verify log output
4. Load to slot 0 → refresh → verify occupied
5. Test: combos work on layer 0, disabled on layer 2
6. Unload → verify empty, combos work everywhere

---

## Execution Order

```
A1 → A3 (subsumes A2+A4) → A5 → A6 → A7 → Build verify
         ↓ (parallel with)
B1 → B2 → B3 → B4
         ↓ (parallel with)
C1 → C2 → C3 → C4
         ↓
D1 → D2 → E1 → E2 → F1 → F2
```

Parts A, B, and C can be developed in parallel. Part D depends on B+C. Part F validates everything.
