# Combo EEPROM Persistence Implementation Plan

> **For Implementer:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task.

**Goal:** Add EEPROM-backed persistent storage for combo definitions on Keychron Q3 Max, allowing combo configurations to survive resets and be potentially modified at runtime.

**Architecture:** Store combo definitions in a custom EEPROM region (0x0300-0x03FF) using a flat `combo_def_t` struct. Maintain a RAM mirror for trigger keys since QMK's `combo_t` requires pointers. Load from EEPROM at boot with fallback to PROGMEM defaults if EEPROM is empty.

**Tech Stack:** QMK Firmware, STM32 EEPROM driver, C

---

## Background

### Current State
- Combos are hardcoded in `keymap.c` using `combo_t` struct with trigger keys in RAM array
- `combo_t` requires `uint16_t *` pointer to trigger keys (cannot store pointers in EEPROM)
- No persistence - combos reset to defaults on every reboot

### EEPROM Layout (1024 bytes total)
| Region | Address | Size | Description |
|--------|---------|------|-------------|
| Core EECONFIG | 0x0000 | 37 bytes | QMK core settings |
| Keychron KB Data | 0x0025 | 526 bytes | Language, RGB, Wireless |
| VIA Magic | 0x0235 | 3 bytes | VIA build date |
| VIA Layout Options | 0x0238 | 1 byte | Layout options |
| Dynamic Keymap | 0x0239 | ~256 bytes | Keymap data (partial) |
| **Combo Storage** | **0x0300** | **288 bytes** | **16 combos (NEW)** |

### Combo Definition Size
```c
typedef struct {
    uint16_t keycodes[8];  // 16 bytes (8 keycodes × 2 bytes, last is COMBO_END)
    uint16_t result;       // 2 bytes (result keycode)
} combo_def_t;            // Total: 18 bytes per combo
```

16 combos × 18 bytes = 288 bytes (0x0300 to 0x03FF)

---

### Task 1: Add Combo EEPROM Struct and Constants

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c:93-96`

**Step 1: Add EEPROM constants and struct after COMBO_MAX_COMBOS define**

Insert after line 96:

```c
// EEPROM storage for combos
#define COMBO_EEPROM_ADDR         0x0300
#define COMBO_EEPROM_SIZE         (COMBO_MAX_COMBOS * sizeof(combo_def_t))
#define COMBO_EEPROM_MAGIC        0xAB

typedef struct {
    uint16_t keycodes[COMBO_MAX_KEYS];
    uint16_t result;
} combo_def_t;
```

**Step 2: Add RAM mirror for trigger keys**

Replace lines 97-103:

```c
// Empty combo for initialization
const uint16_t PROGMEM combo_empty_keys[] = {COMBO_END};

// RAM mirror for combo trigger keys (pointers required by combo_t)
static uint16_t combo_keys_ram[COMBO_MAX_COMBOS][COMBO_MAX_KEYS + 1];
```

**Step 3: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: add combo EEPROM struct and constants"
```

---

### Task 2: Implement EEPROM Load Function

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

**Step 1: Add load function before combo_t array**

Insert before line 105 (before `combo_t key_combos[]`):

```c
// Load combos from EEPROM into RAM mirror
static void load_combos_from_eeprom(void) {
    uint8_t magic;
    combo_def_t defs[COMBO_MAX_COMBOS];
    
    // Check magic byte
    eeprom_read_byte((uint8_t *)(COMBO_EEPROM_ADDR), &magic);
    if (magic != COMBO_EEPROM_MAGIC) {
        // Invalid EEPROM, use defaults
        return;
    }
    
    // Read all combo definitions
    eeprom_read_block(defs, (void *)(COMBO_EEPROM_ADDR + 1), COMBO_EEPROM_SIZE - 1);
    
    // Copy to RAM mirror
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        for (int j = 0; j < COMBO_MAX_KEYS; j++) {
            combo_keys_ram[i][j] = defs[i].keycodes[j];
        }
        combo_keys_ram[i][COMBO_MAX_KEYS] = COMBO_END;
    }
}
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: implement load_combos_from_eeprom function"
```

---

### Task 3: Implement EEPROM Save Function

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

**Step 1: Add save function after load function**

Insert after the `load_combos_from_eeprom` function:

```c
// Save combos from RAM mirror to EEPROM
static void save_combos_to_eeprom(void) {
    combo_def_t defs[COMBO_MAX_COMBOS];
    uint8_t magic = COMBO_EEPROM_MAGIC;
    
    // Copy from RAM mirror to temp struct
    for (int i = 0; i < COMBO_MAX_COMBOS; i++) {
        for (int j = 0; j < COMBO_MAX_KEYS; j++) {
            defs[i].keycodes[j] = combo_keys_ram[i][j];
        }
        defs[i].result = key_combos[i].keycode;
    }
    
    // Write magic byte
    eeprom_update_byte((uint8_t *)(COMBO_EEPROM_ADDR), magic);
    
    // Write all combo definitions
    eeprom_update_block(defs, (void *)(COMBO_EEPROM_ADDR + 1), COMBO_EEPROM_SIZE - 1);
}
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: implement save_combos_to_eeprom function"
```

---

### Task 4: Update Combo Array to Use RAM Mirror

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c:105-109`

**Step 1: Replace combo_t array initialization**

Replace lines 105-109:

```c
combo_t key_combos[COMBO_MAX_COMBOS] = {
    [0]        = {.keycodes = combo_keys_ram[0], .keycode = KC_ESC, .flags = COMBO_FLAG_NONE},
    [1]        = {.keycodes = combo_keys_ram[1], .keycode = LCTL(KC_Z), .flags = COMBO_FLAG_NONE},
    [2 ... COMBO_MAX_COMBOS-1] = {.keycodes = combo_empty_keys, .keycode = 0, .flags = COMBO_FLAG_NONE},
};
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: update combo array to use RAM mirror"
```

---

### Task 5: Add Keyboard Post-Init Hook

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

**Step 1: Add keyboard_post_init_user function**

Insert after the combo section (after line 110, before TAP DANCE section):

```c
// Initialize combos from EEPROM or defaults
void keyboard_post_init_user(void) {
    #ifdef COMBO_ENABLE
    // Initialize RAM mirror with defaults
    combo_keys_ram[0][0] = KC_A;
    combo_keys_ram[0][1] = KC_B;
    combo_keys_ram[0][2] = COMBO_END;
    
    combo_keys_ram[1][0] = KC_LEFT;
    combo_keys_ram[1][1] = KC_RIGHT;
    combo_keys_ram[1][2] = COMBO_END;
    
    // Load from EEPROM if valid
    load_combos_from_eeprom();
    #endif
}
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: add keyboard_post_init_user for combo initialization"
```

---

### Task 6: Add API Functions for Runtime Combo Modification (Optional)

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

**Step 1: Add public API functions**

Insert after `save_combos_to_eeprom` function:

```c
// Set a combo's trigger keys and result
void combo_set(uint8_t combo_index, const uint16_t *keys, uint16_t result) {
    if (combo_index >= COMBO_MAX_COMBOS) return;
    
    int i = 0;
    while (keys[i] != COMBO_END && i < COMBO_MAX_KEYS) {
        combo_keys_ram[combo_index][i] = keys[i];
        i++;
    }
    combo_keys_ram[combo_index][i] = COMBO_END;
    key_combos[combo_index].keycode = result;
    save_combos_to_eeprom();
}

// Clear a combo (set to empty)
void combo_clear(uint8_t combo_index) {
    if (combo_index >= COMBO_MAX_COMBOS) return;
    
    for (int i = 0; i <= COMBO_MAX_KEYS; i++) {
        combo_keys_ram[combo_index][i] = COMBO_END;
    }
    key_combos[combo_index].keycode = 0;
    save_combos_to_eeprom();
}

// Reset all combos to defaults
void combo_reset_to_defaults(void) {
    combo_keys_ram[0][0] = KC_A;
    combo_keys_ram[0][1] = KC_B;
    combo_keys_ram[0][2] = COMBO_END;
    combo_keys_ram[1][0] = KC_LEFT;
    combo_keys_ram[1][1] = KC_RIGHT;
    combo_keys_ram[1][2] = COMBO_END;
    
    for (int i = 2; i < COMBO_MAX_COMBOS; i++) {
        for (int j = 0; j <= COMBO_MAX_KEYS; j++) {
            combo_keys_ram[i][j] = COMBO_END;
        }
        key_combos[i].keycode = 0;
    }
    
    key_combos[0].keycode = KC_ESC;
    key_combos[1].keycode = LCTL(KC_Z);
    save_combos_to_eeprom();
}
```

**Step 2: Commit**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: add API functions for runtime combo modification"
```

---

### Task 7: Build and Test

**Step 1: Build the firmware**

```bash
make keychron/q3_max/ansi_encoder:via
```

Expected: Build completes without errors

**Step 2: Flash to keyboard**

```bash
make keychron/q3_max/ansi_encoder:via:flash
```

**Step 3: Test basic functionality**

1. Verify default combos work (A+B → ESC, Left+Right → Ctrl+Z)
2. Reset keyboard and verify combos persist
3. (Optional) Test combo modification API if integrated with VIA/TESS

---

## Summary

This implementation adds EEPROM-backed persistence for combo definitions:

1. **Storage**: 16 combo slots in EEPROM at address 0x0300 (288 bytes)
2. **RAM Mirror**: Trigger keys stored in RAM since `combo_t` requires pointers
3. **Load at Boot**: `keyboard_post_init_user()` loads from EEPROM or uses defaults
4. **API**: Functions to modify combos at runtime with automatic EEPROM save
5. **Safety**: Magic byte validation prevents corruption from invalid data

### Files Modified
- `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

### Key Functions
- `load_combos_from_eeprom()` - Load combo definitions from EEPROM
- `save_combos_to_eeprom()` - Save combo definitions to EEPROM
- `keyboard_post_init_user()` - Initialize combos at boot
- `combo_set()` - Set a combo's keys and result
- `combo_clear()` - Clear a combo
- `combo_reset_to_defaults()` - Reset all combos to defaults
