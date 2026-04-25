# Combo EEPROM Persistence Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add EEPROM-backed persistent storage for 16 combo definitions on Keychron Q3 Max, surviving resets and supporting runtime modification.

**Architecture:** Reserve 289 bytes at the end of the 2048-byte EEPROM by reducing `DYNAMIC_KEYMAP_LAYER_COUNT` from 8 to 6. Store combo definitions as flat `combo_def_t` structs (no pointers). Maintain a RAM mirror (`combo_keys_ram`) for trigger keys since QMK's `combo_t` requires `const uint16_t *`. Load from EEPROM at boot via `keyboard_post_init_user`; save defaults on first boot.

**Tech Stack:** QMK Firmware (C), STM32F401 (ChibiOS), wear-leveling EEPROM driver

---

## EEPROM Layout (2048 bytes, 6 layers)

All addresses are computed from QMK macros. Values below are for verification.

| Region | Start | End | Size | Derivation |
|--------|-------|-----|------|------------|
| Core EECONFIG | 0x0000 | 0x0024 | 37 B | `EECONFIG_BASE_SIZE` |
| Keychron KB Data | 0x0025 | 0x0029 | 5 B | `EECONFIG_KB_DATA_SIZE` (1 lang + 4 wireless) |
| VIA Magic | 0x002A | 0x002C | 3 B | `VIA_EEPROM_MAGIC_ADDR` |
| VIA Layout Options | 0x002D | 0x002D | 1 B | `VIA_EEPROM_LAYOUT_OPTIONS_SIZE` |
| Dynamic Keymap | 0x002E | 0x04F5 | 1224 B | 6 layers x 6 rows x 17 cols x 2 bytes |
| Encoder Map | 0x04F6 | 0x050D | 24 B | 6 layers x 1 encoder x 2 dirs x 2 bytes |
| Macros | 0x050E | 0x05DE | 465 B | `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` |
| **Combo Storage** | **0x05DF** | **0x07FF** | **289 B** | 1 magic + 16 x 18 bytes |

**Static assert check:** Macro space = 465 bytes >= 100 bytes minimum.

### Combo EEPROM Block Layout (289 bytes)

| Offset | Size | Content |
|--------|------|---------|
| 0 | 1 B | Magic byte (`0xCB`) |
| 1 | 18 B | Combo 0: `combo_def_t` |
| 19 | 18 B | Combo 1: `combo_def_t` |
| ... | ... | ... |
| 271 | 18 B | Combo 15: `combo_def_t` |

```c
typedef struct {
    uint16_t keycodes[8];  // trigger keys, terminated by COMBO_END (0)
    uint16_t result;       // result keycode
} combo_def_t;             // 18 bytes
```

## Important Notes

- **EEPROM reset required after flashing.** Changing `DYNAMIC_KEYMAP_LAYER_COUNT` shifts all EEPROM addresses. Use VIA "Reset" or bootloader EEPROM clear on first flash.
- `PROGMEM` and `pgm_read_word` are no-ops on ARM/STM32 (`platforms/progmem.h`). Combo keys in RAM work identically to const PROGMEM arrays.
- `eeprom_read_byte()` **returns** the value (does NOT write to a pointer).
- `eeprom_update_*` functions only write if the value differs, reducing flash wear.

---

### Task 1: Reserve EEPROM Space in config.h

**Files:**
- Modify: `keyboards/keychron/q3_max/config.h:87` (change layer count, add max addr)

**Step 1: Change layer count and reserve EEPROM**

Replace line 87:
```c
#define DYNAMIC_KEYMAP_LAYER_COUNT  8
```

With:
```c
#define DYNAMIC_KEYMAP_LAYER_COUNT  6

// Reserve 289 bytes at end of EEPROM for combo persistence
// (1 magic byte + 16 combos x 18 bytes each)
// TOTAL_EEPROM_BYTE_COUNT = 2048 (WEAR_LEVELING_LOGICAL_SIZE in info.json)
#define COMBO_EEPROM_BLOCK_SIZE  (1 + (16 * 18))
#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR  (TOTAL_EEPROM_BYTE_COUNT - 1 - COMBO_EEPROM_BLOCK_SIZE)
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/config.h
git commit -m "feat: reduce layers to 6, reserve EEPROM for combo storage"
```

---

### Task 2: Rewrite Combo Section in keymap.c

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c:90-110`

**Step 1: Replace entire combo section (lines 90-110)**

Replace lines 90-110 with:
```c
////////////////////////////////////////////////////////////////////////////////
// COMBO EEPROM PERSISTENCE
////////////////////////////////////////////////////////////////////////////////
#ifdef COMBO_ENABLE
#include "eeprom.h"

#define COMBO_MAX_KEYS   8
#define COMBO_MAX_COMBOS 16

// EEPROM addresses: combo block sits at the end of EEPROM, after macro space
#define COMBO_EEPROM_MAGIC_ADDR  (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1)
#define COMBO_EEPROM_DATA_ADDR   (COMBO_EEPROM_MAGIC_ADDR + 1)
#define COMBO_EEPROM_MAGIC_VALUE 0xCB

// Flat struct for EEPROM storage (no pointers)
typedef struct {
    uint16_t keycodes[COMBO_MAX_KEYS]; // trigger keys, COMBO_END terminated
    uint16_t result;                   // result keycode
} combo_def_t;                         // 18 bytes

// RAM mirror for trigger keys (combo_t requires const uint16_t*)
static uint16_t combo_keys_ram[COMBO_MAX_COMBOS][COMBO_MAX_KEYS + 1] = {
    [0] = {KC_A, KC_B, COMBO_END},
    [1] = {KC_LEFT, KC_RIGHT, COMBO_END},
    // Remaining entries are zero-initialized (all COMBO_END)
};

// Safe empty keys for static initialization (before init function runs)
const uint16_t PROGMEM combo_empty_keys[] = {COMBO_END};

// combo_t array required by QMK. Initialized with safe empty keys;
// combo_eeprom_init() sets correct pointers and keycodes before any
// combo processing occurs.
combo_t key_combos[COMBO_MAX_COMBOS] = {
    [0 ... COMBO_MAX_COMBOS - 1] = COMBO(combo_empty_keys, KC_NO),
};

// --- EEPROM load/save (per-combo to minimize stack usage: 18 bytes) ---

static void load_combos_from_eeprom(void) {
    combo_def_t def;
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        eeprom_read_block(&def,
                          (const void *)(uintptr_t)(COMBO_EEPROM_DATA_ADDR + i * sizeof(combo_def_t)),
                          sizeof(combo_def_t));
        for (int j = 0; j < COMBO_MAX_KEYS; j++) {
            combo_keys_ram[i][j] = def.keycodes[j];
        }
        combo_keys_ram[i][COMBO_MAX_KEYS] = COMBO_END;
        key_combos[i].keycode = def.result;
    }
}

static void save_combos_to_eeprom(void) {
    eeprom_update_byte((uint8_t *)(uintptr_t)COMBO_EEPROM_MAGIC_ADDR, COMBO_EEPROM_MAGIC_VALUE);

    combo_def_t def;
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        for (int j = 0; j < COMBO_MAX_KEYS; j++) {
            def.keycodes[j] = combo_keys_ram[i][j];
        }
        def.result = key_combos[i].keycode;
        eeprom_update_block(&def,
                            (void *)(uintptr_t)(COMBO_EEPROM_DATA_ADDR + i * sizeof(combo_def_t)),
                            sizeof(combo_def_t));
    }
}

// --- Public init function (called from keyboard_post_init_user) ---

void combo_eeprom_init(void) {
    // Point all combos to their RAM mirror slots
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        key_combos[i].keys = combo_keys_ram[i];
    }

    // Set default result keycodes for pre-defined combos
    key_combos[0].keycode = KC_ESC;
    key_combos[1].keycode = LCTL(KC_Z);

    // Load from EEPROM if valid magic, otherwise save defaults
    if (eeprom_read_byte((const uint8_t *)(uintptr_t)COMBO_EEPROM_MAGIC_ADDR) == COMBO_EEPROM_MAGIC_VALUE) {
        load_combos_from_eeprom();
    } else {
        save_combos_to_eeprom();
    }
}

// --- Runtime API ---

void combo_set(uint8_t index, const uint16_t *keys, uint16_t result) {
    if (index >= COMBO_MAX_COMBOS) return;

    int i = 0;
    while (keys[i] != COMBO_END && i < COMBO_MAX_KEYS) {
        combo_keys_ram[index][i] = keys[i];
        i++;
    }
    // Fill remaining slots with COMBO_END
    while (i <= COMBO_MAX_KEYS) {
        combo_keys_ram[index][i++] = COMBO_END;
    }
    key_combos[index].keycode = result;
    save_combos_to_eeprom();
}

void combo_clear(uint8_t index) {
    if (index >= COMBO_MAX_COMBOS) return;

    for (int i = 0; i <= COMBO_MAX_KEYS; i++) {
        combo_keys_ram[index][i] = COMBO_END;
    }
    key_combos[index].keycode = KC_NO;
    save_combos_to_eeprom();
}

void combo_reset_to_defaults(void) {
    // Clear all slots
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        for (int j = 0; j <= COMBO_MAX_KEYS; j++) {
            combo_keys_ram[i][j] = COMBO_END;
        }
        key_combos[i].keycode = KC_NO;
    }

    // Re-apply defaults
    combo_keys_ram[0][0] = KC_A;
    combo_keys_ram[0][1] = KC_B;
    combo_keys_ram[0][2] = COMBO_END;
    key_combos[0].keycode = KC_ESC;

    combo_keys_ram[1][0] = KC_LEFT;
    combo_keys_ram[1][1] = KC_RIGHT;
    combo_keys_ram[1][2] = COMBO_END;
    key_combos[1].keycode = LCTL(KC_Z);

    save_combos_to_eeprom();
}

#endif // COMBO_ENABLE
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: implement combo EEPROM persistence with load/save/init/API"
```

---

### Task 3: Hook combo_eeprom_init into keyboard_post_init_user

**Files:**
- Modify: `keyboards/keychron/q3_max/q3_max_user.c:19,24-32`

**Step 1: Add weak declaration after QMKATA includes (after line 22)**

After line 22 (the `#endif` closing the QMKATA includes), add:
```c
#ifdef COMBO_ENABLE
__attribute__((weak)) void combo_eeprom_init(void) {}
#endif
```

**Step 2: Replace keyboard_post_init_user (lines 24-32)**

Replace the entire function with:
```c
void keyboard_post_init_user(void) {
#ifdef QMKATA_ENABLE
#ifdef DEVEL_BUILD
    //debug_config.enable = 1;
    //debug_config_user.qmkata = 1;
#endif
    qmkata_init("Keychron QMKata");
#endif
#ifdef COMBO_ENABLE
    combo_eeprom_init();
#endif
}
```

The `__attribute__((weak))` declaration provides a no-op fallback for keymaps that don't define `combo_eeprom_init()`. When our via keymap provides the real implementation, the linker uses it.

**Step 3: Commit**

```bash
git add keyboards/keychron/q3_max/q3_max_user.c
git commit -m "feat: call combo_eeprom_init from keyboard_post_init_user"
```

---

### Task 4: Build Firmware

**Step 1: Build**

```bash
make keychron/q3_max/ansi_encoder:via
```

Expected: Compiles without errors or warnings.

**Step 2: Check RAM usage**

Look at the build output for memory usage. The `arm-none-eabi-size` output should show:
- `.data + .bss` should still be close to 65,536 (no significant increase -- combo_keys_ram replaces the existing `combo_keys` array, and combo_def_t is stack-only)
- `.text` should be similar (~84KB)

```bash
arm-none-eabi-size .build/keychron_q3_max_ansi_encoder_via.elf
```

**Step 3: Verify EEPROM layout**

The build should NOT fail with the `_Static_assert` in `dynamic_keymap.c:92`. If it does, the EEPROM math is wrong -- re-check the addresses.

**Step 4: Commit (if any fixes were needed)**

---

### Task 5: Test on Hardware (Manual)

1. **Flash firmware** (use QMK Toolbox or `make flash`)
2. **Reset EEPROM** after flashing (required because layer count changed):
   - VIA: Settings > Reset EEPROM
   - Or: hold Bootmagic key during plug-in
3. **Test default combos:**
   - Press A + B simultaneously -> should produce ESC
   - Press Left + Right simultaneously -> should produce Ctrl+Z
4. **Test persistence:**
   - Unplug and re-plug keyboard
   - Default combos should still work (loaded from EEPROM)
5. **Test VIA:** Verify VIA connects and can read/write keymaps on layers 0-5

---

## Bug Fixes from Previous Plan Review

| # | Bug | Fix |
|---|-----|-----|
| 1 | EEPROM address 0x0300-0x03FF (256 bytes) can't fit 288 bytes | Use `DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` to reserve 289 bytes at end of EEPROM; reduce layers 8->6 for space |
| 2 | `eeprom_read_byte((uint8_t*)addr, &magic)` wrong API | Fixed: `magic = eeprom_read_byte((const uint8_t*)(uintptr_t)addr)` -- returns value |
| 3 | Load function never updates `key_combos[i].keycode` | Fixed: `key_combos[i].keycode = def.result` in load loop |
| 4 | Load function never updates `.keys` pointers for combos 2-15 | Fixed: `combo_eeprom_init()` sets `key_combos[i].keys = combo_keys_ram[i]` for all 16 |
| 5 | `COMBO_EEPROM_SIZE - 1` wrong for block size | Fixed: use `sizeof(combo_def_t)` per combo, no aggregate buffer |
| 6 | `.flags = COMBO_FLAG_NONE` doesn't exist in `combo_t` | Fixed: use `COMBO()` macro for static init; direct field assignment at runtime |
| 7 | 288 bytes on stack in load/save | Fixed: per-combo read/write loop -- only 18 bytes on stack |

## Files Modified

| File | Change |
|------|--------|
| `keyboards/keychron/q3_max/config.h` | `DYNAMIC_KEYMAP_LAYER_COUNT` 8->6, add `DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` |
| `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c` | Complete combo EEPROM implementation |
| `keyboards/keychron/q3_max/q3_max_user.c` | Call `combo_eeprom_init()` from `keyboard_post_init_user` |

## Key Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `load_combos_from_eeprom()` | keymap.c (static) | Read all combo defs from EEPROM into RAM mirror + key_combos |
| `save_combos_to_eeprom()` | keymap.c (static) | Write magic + all combo defs from RAM to EEPROM |
| `combo_eeprom_init()` | keymap.c (extern) | Set up pointers, apply defaults, load/save on boot |
| `combo_set()` | keymap.c (extern) | Set combo trigger keys + result, auto-save |
| `combo_clear()` | keymap.c (extern) | Clear a combo slot, auto-save |
| `combo_reset_to_defaults()` | keymap.c (extern) | Reset all combos to compile-time defaults, auto-save |
