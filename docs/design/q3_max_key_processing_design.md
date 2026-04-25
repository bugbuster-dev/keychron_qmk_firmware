# Key Processing Design Document: Keychron Q3 Max (QMK Firmware)

**Document Version:** 1.0  
**Date:** 2026-04-15  
**Target Keyboard:** Keychron Q3 Max (ANSI Encoder variant)  
**Firmware Base:** QMK Firmware  
**MCU:** STM32L432  
**Architecture:** ARM Cortex-M4

---

## 1. Executive Summary

This design document describes the key processing architecture for the Keychron Q3 Max keyboard running QMK firmware. The implementation uses a custom matrix scanning approach with HC595 shift registers for column selection, per-key debouncing with multiple algorithm options, and extensive custom keycode support for macOS/Windows compatibility.

**Key Features:**
- 87-key ANSI layout with dual encoders
- Custom debouncing system (6 algorithms)
- HC595-based column scanning
- Per-key RGB matrix (SNLED27351 driver)
- OS-specific keycode mapping (macOS/Windows)
- Layer support (4 base layers + FN layers)
- Factory test mode support
- Wireless/BT capability (hardware present)

---

## 2. System Architecture Overview

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    USB HID Report Layer                     │
├─────────────────────────────────────────────────────────────┤
│                  Key Processing Pipeline                    │
├─────────────┬─────────────┬─────────────┬───────────────────┤
│   Matrix    │  Debounce   │  Keycode    │   Action Layer   │
│   Scanner   │  Engine     │  Processor  │   & Modifiers    │
├─────────────┴─────────────┴─────────────┴───────────────────┤
│              Custom Keychron Processing                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────────┐  │
│  │ OS Toggle│ │ Key Combo│ │  Siri    │ │  GUI Toggle │  │
│  │ Handler  │ │ Handler  │ │ Handler  │ │  Handler    │  │
│  └──────────┘ └──────────┘ └──────────┘ └─────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                 Matrix Hardware Layer                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────────┐  │
│  │ Row Pins │ │Col Shift │ │ Diode    │ │   Encoder   │  │
│  │ (Input)  │ │ Register │ │ DIR:     │ │   Interrupt │  │
│  │          │ │ (HC595)  │ │ ROW2COL  │ │   (PAL)     │  │
│  └──────────┘ └──────────┘ └──────────┘ └─────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 File Structure

```
keyboards/keychron/q3/
├── q3.c                          # Main keyboard init
├── config.h                      # Shared config (HC595, encoders)
├── keycodes_custom.h             # Custom keycode definitions
├── rules.mk                      # Build rules
├── ansi_encoder/                # ANSI variant with encoder
│   ├── ansi_encoder.c           # RGB LED configuration
│   ├── config.h                 # Variant-specific config
│   ├── keymaps/
│   │   ├── keychron/
│   │   │   ├── keymap.c        # Layer definitions
│   │   │   └── rules.mk
│   │   └── default/
│   └── rules.mk
├── iso_encoder/
├── jis_encoder/
└── [other variants]

keyboards/keychron/common/
├── keychron.h                   # Main header (includes all modules)
├── keychron_common.c/h          # Core key processing & OS handlers
├── keychron_task.c/h            # Task scheduler & periodic tasks
├── keychron_raw_hid.c/h         # Raw HID communication
├── matrix.c                     # Matrix scanning (HC595)
├── debounce/
│   ├── keychron_debounce.c/h   # Debounce interface
│   ├── sym_defer_g.c           # Symmetric defer global
│   ├── sym_defer_pk.c          # Symmetric defer per-key
│   ├── sym_defer_pr.c          # Symmetric defer per-row
│   ├── sym_eager_pk.c          # Symmetric eager per-key
│   ├── sym_eager_pr.c          # Symmetric eager per-row
│   ├── asym_eager_defer_pk.c   # Asymmetric eager defer per-key
│   └── none.c                  # No debounce
├── backlit_indicator.c/h        # LED indicator patterns
├── eeconfig_kb.h               # EEPROM layout (KB-specific)
└── [other common modules]
```

---

## 3. Key Processing Flow

### 3.1 Complete Data Flow

```
1. Hardware Interrupt (Timer) → matrix_scan()
   │
   ├─→ matrix_init_custom() [once at startup]
   │   └─→ Initialize HC595 shift registers
   │   └─→ Set row pins as input (with pull-up)
   │   └─→ Unselect all columns
   │
   └─→ matrix_scan_custom() [periodic, ~1kHz]
       │
       ├─→ For each column (0 to MATRIX_COLS-1):
       │   ├─→ select_col(col)
       │   │   └─→ HC595_output(pattern) via shift register
       │   ├─→ HC595_delay(200 cycles)  // settle time
       │   ├─→ For each row (0 to MATRIX_ROWS-1):
       │   │   └─→ readMatrixPin(row_pin[row])
       │   │       └─→ If LOW (0): set bit in matrix[row]
       │   └─→ unselect_col(col)
       │       └─→ HC595_delay(200 cycles)
       │
       └─→ Return true if matrix changed

2. Raw Matrix → debounce(raw_matrix, cooked_matrix, changed)
   │
   ├─→ debounce_init(num_rows) [once]
   │   └─→ Load debounce type from EEPROM
   │   └─→ Initialize selected debounce algorithm
   │
   └─→ debounce(raw[], cooked[], num_rows, changed)
       │
       ├─→ If changed: feed new raw state to algorithm
       │
       └─→ Algorithm tracks per-key state:
           ├─→ State machine: IDLE → PRESSED → RELEASED
           ├─→ Counter-based timing (debounce_time ms)
           ├─→ On stable state change:
           │   └─→ Update cooked_matrix
           │
           └─→ Return true if cooked changed

3. Cooked Matrix → matrix_scan() → action_tapping()
   │
   ├─→ matrix_scan() calls:
   │   ├─→ matrix_scan_custom() → raw matrix
   │   ├─→ debounce() → cooked matrix
   │   └─→ If cooked changed:
   │       └─→ For each key (row, col):
   │           ├─→ keycode = keymap[layer][row][col]
   │           ├─→ keyevent_t event = {
   │           │     .keycode = keycode,
   │           │     .pressed = (cooked bit set),
   │           │     .time = timer_read()
   │           │   }
   │           └─→ action_tapping(event)
   │
   └─→ action_tapping():
       ├─→ Handle modifier stacking
       ├─→ Handle tap/hold distinction (via tap_code)
       ├─→ Call process_record_user(keycode, record)
       │   └─→ User keymap overrides
       └─→ Call process_record_keychron_common()
           └─→ Custom Keychron handlers (see Section 6)

4. Keycode Processing → USB HID Report
   │
   ├─→ If keycode is modifier:
   │   └─→ modifier_state |= (1<<mod_bit)
   │
   ├─→ If keycode is regular key:
   │   └─→ Add to keypress list (max 6 for NKRO)
   │
   ├─→ If keycode is system key (power, wake, etc.):
   │   └─→ Send via USB system control endpoint
   │
   ├─→ If keycode is mouse key:
   │   └─→ Send via USB mouse endpoint
   │
   └─→ If keycode is consumer control (volume, media):
       └─→ Send via USB consumer control endpoint

5. Periodic Tasks (keychron_task()) - runs every 10-30ms
   │
   ├─→ Siri timeout (500ms)
   ├─→ OS toggle hold detection (if configured)
   ├─→ OS select combo timeout (3000ms)
   ├─→ WinLock hold detection (if configured)
   ├─→ Profile selection (analog matrix)
   ├─→ Wireless task scheduling
   ├─→ Factory test monitoring
   ├─→ Retail demo timeout
   ├─→ LED indicator updates (backlit_indicator_task)
   └─→ RGB matrix animations (if enabled)
```

### 3.2 Timing Diagram

```
Time ──────────────────────────────────────────────────────────────>
      |    T1    |    T2    |    T3    |    T4    |    T5    |
Matrix ──┐         │          │          │          │          │
Scan     ─────────┘          │          │          │          │
            (1ms)             │          │          │          │
                              ▼          │          │          │
Debounce ──────────────────────┼─────────┘          │          │
  (5ms)                        │ (state machine)    │          │
                               ▼                    │          │
Key Event ───────────────────────────────────────────┼──────────┘
  (immediate)                    (on stable change)  │
                                            ▼      │
USB Report ────────────────────────────────────────────────┘
  (every matrix_scan if changed)
```

---

## 4. Matrix Scanning Implementation

### 4.1 Hardware Configuration

**File:** `keyboards/keychron/common/matrix.c`

**Pin Configuration:**
- **Rows:** `MATRIX_ROW_PINS` (array of `pin_t`, size `MATRIX_ROWS`)
  - Configured as input with pull-up
  - Read via `gpio_read_pin()`
- **Columns:** `MATRIX_COL_PINS` (array of `pin_t`, size `MATRIX_COLS`)
  - Split into two ranges:
    - **Direct GPIO columns:** Pins 0-7 (configurable via `HC595_START_INDEX`)
    - **Shift register columns:** Pins 8-15 (via HC595)
  - Direct columns: Output push-pull, LOW to select
  - Shift register columns: Controlled via 3 pins:
    - `HC595_DS` (data)
    - `HC595_SHCP` (shift clock)
    - `HC595_STCP` (storage clock)

**Diode Direction:** `ROW2COL` (from row to column)
- Row pin = input with pull-up
- Column pin = output LOW to select
- Key pressed = row pin reads LOW (diode conducts)

### 4.2 Matrix Scanning Algorithm

**Function:** `matrix_scan_custom(matrix_row_t current_matrix[])`

```c
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_row_t curr_matrix[MATRIX_ROWS] = {0};
    matrix_row_t row_shifter = MATRIX_ROW_SHIFTER;  // Typically 1

    for (uint8_t current_col = 0; current_col < MATRIX_COLS; current_col++, row_shifter <<= 1) {
        matrix_read_rows_on_col(curr_matrix, current_col, row_shifter);
    }

    bool changed = memcmp(current_matrix, curr_matrix, sizeof(curr_matrix)) != 0;
    if (changed) memcpy(current_matrix, curr_matrix, sizeof(curr_matrix));
    return changed;
}
```

**Function:** `matrix_read_rows_on_col(matrix_row_t current_matrix[], uint8_t current_col, matrix_row_t row_shifter)`

```c
static void matrix_read_rows_on_col(matrix_row_t current_matrix[], uint8_t current_col, matrix_row_t row_shifter) {
    select_col(current_col);           // Set column LOW (direct or via HC595)
    HC595_delay(200);                 // Settling time (~2µs at 72MHz)

    for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
        if (readMatrixPin(row_pins[row_index]) == 0) {
            current_matrix[row_index] |= row_shifter;    // Key pressed
        } else {
            current_matrix[row_index] &= ~row_shifter;   // Key released
        }
    }

    unselect_col(current_col);         // Set column HIGH/Z
    HC595_delay(200);                 // Allow rows to float high
}
```

**HC595 Shift Register Output:**

```c
static void HC595_output(SIZE_T data, bool bit_flag) {
    ATOMIC_BLOCK_FORCE_ON {
        for (uint8_t i = 0; i < (HC595_END_INDEX - HC595_START_INDEX + 1); i++) {
            if (data & 0x1) {
                gpio_write_pin_high(HC595_DS);
            } else {
                gpio_write_pin_low(HC595_DS);
            }
            gpio_write_pin_high(HC595_SHCP);  // Rising edge
            HC595_delay(n);
            gpio_write_pin_low(HC595_SHCP);
            HC595_delay(n);
            if (bit_flag) break;               // Only output 1 bit if requested
            data = data >> 1;
        }
        gpio_write_pin_high(HC595_STCP);      // Latch to output
        HC595_delay(n);
        gpio_write_pin_low(HC595_STCP);
        HC595_delay(n);
    }
}
```

### 4.3 Column Selection Logic

```c
static void select_col(uint8_t col) {
    if (col < HC595_START_INDEX || col > HC595_END_INDEX) {
        // Direct GPIO column
        gpio_set_pin_output_push_pull_writeLow(col_pins[col]);
    } else {
        // Shift register column
        if (col == HC595_START_INDEX) {
            HC595_output(0x00, true);  // Select first column (all others high)
            if (col < HC595_OFFSET_INDEX) {
                HC595_output(0x01, true);  // Additional offset
            }
        }
    }
}
```

---

## 5. Debounce Implementation

### 5.1 Debounce Interface

**File:** `keyboards/keychron/common/debounce/keychron_debounce.h`

```c
typedef struct {
    void (*debounce_init)(uint8_t num_rows);
    bool (*debounce)(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
    void (*debounce_free)(void);
} debounce_t;

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
void debounce_init(uint8_t num_rows);
void debounce_config_reset(void);
void debounce_free(void);
void debounce_rx(uint8_t *data, uint8_t size);  // For configuration via Raw HID
```

**Debounce Types (enum):**
```c
enum {
    DEBOUNCE_SYM_DEFER_GLOBAL,      // Symmetric defer (global timer)
    DEBOUNCE_SYM_DEFER_PER_ROW,     // Symmetric defer (per-row timer)
    DEBOUNCE_SYM_DEFER_PER_KEY,     // Symmetric defer (per-key timer)
    DEBOUNCE_SYM_EAGER_PER_ROW,     // Symmetric eager (per-row)
    DEBOUNCE_SYM_EAGER_PER_KEY,     // Symmetric eager (per-key) [DEFAULT]
    DEBOUNCE_ASYM_EAGER_DEFER_PER_KEY,  // Asymmetric eager defer
    DEBOUNCE_NONE,                  // No debounce
    DEBOUNCE_MAX,
};
```

### 5.2 Debounce Configuration

- **Default:** `DEBOUNCE_SYM_EAGER_PER_KEY`
- **Debounce Time:** `DEBOUNCE` (default 5ms, configurable 1-255ms via EEPROM)
- **Storage:** EEPROM offset `OFFSET_DEBOUNCE` (type + time)
- **Runtime Change:** Via Raw HID interface or VIA (if supported)

### 5.3 Algorithm Details

**Symmetric Eager Per-Key (Default):**

```c
// Each key has:
typedef struct {
    matrix_row_t current;      // Current debounced state
    matrix_row_t previous;     // Previous state
    uint16_t    press_time;    // Time when pressed (if pressed)
    uint16_t    release_time;  // Time when released (if released)
} per_key_state_t;

// On raw matrix change:
// 1. Detect changed keys (raw XOR previous)
// 2. For each changed key:
//    - If raw bit = 1 (pressed):
//        * Start timer (press_time = timer_read())
//        * Wait DEBOUNCE ms before setting cooked bit
//    - If raw bit = 0 (released):
//        * Start timer (release_time = timer_read())
//        * Wait DEBOUNCE ms before clearing cooked bit
// 3. Return true if any cooked bits changed after debounce delay
```

**Asymmetric Eager Defer Per-Key:**

```c
// Different debounce times for press vs release
// Typically: press_time = DEBOUNCE, release_time = DEBOUNCE * 2
// Useful for keys with bounce issues on release
```

### 5.4 Debounce Selection Logic

```c
void debounce_set(uint8_t new_debounce_type, uint8_t time, bool force) {
    if (new_debounce_type == debounce_type && time == debounce_time && !force) return;

    debounce_free();  // Clean up old algorithm

    debounce_type = new_debounce_type;
    debounce_time = time;
    if (debounce_time == 0) new_debounce_type = DEBOUNCE_NONE;

    switch (new_debounce_type) {
        case DEBOUNCE_SYM_DEFER_GLOBAL:
            debounce_func.debounce_init = sym_defer_g_debounce_init;
            debounce_func.debounce      = sym_defer_g_debounce;
            debounce_func.debounce_free = sym_defer_g_debounce_free;
            break;
        case DEBOUNCE_SYM_EAGER_PER_KEY:
            debounce_func.debounce_init = sym_eager_pk_debounce_init;
            debounce_func.debounce      = sym_eager_pk_debounce;
            debounce_func.debounce_free = sym_eager_pk_debounce_free;
            break;
        // ... other cases
    }

    if (debounce_func.debounce_init) debounce_func.debounce_init(MATRIX_ROWS);
}
```

---

## 6. Custom Keychron Processing

### 6.1 Key Processing Entry Point

**File:** `keyboards/keychron/common/keychron_common.c`

```c
bool process_record_keychron_common(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        // Custom Keychron keycodes
        case KC_MCTRL:  // Mac Mission Control
            if (record->event.pressed) {
                register_code(KC_MISSION_CONTROL);
            } else {
                unregister_code(KC_MISSION_CONTROL);
            }
            return false;  // Skip further processing

        case KC_LNPAD:  // Mac Launchpad
            if (record->event.pressed) {
                register_code(KC_LAUNCHPAD);
            } else {
                unregister_code(KC_LAUNCHPAD);
            }
            return false;

        case KC_LOPTN:  // Mac Option (left)
        case KC_ROPTN:  // Mac Option (right)
        case KC_LCMMD:  // Mac Command (left)
        case KC_RCMMD:  // Mac Command (right)
            if (record->event.pressed) {
                register_code(mac_keycode[keycode - KC_LOPTN]);
            } else {
                unregister_code(mac_keycode[keycode - KC_LOPTN]);
            }
            return false;

        case KC_SIRI:  // Mac Siri (hold for 500ms)
            if (record->event.pressed) {
                if (!siri_active) {
                    siri_active = true;
                    register_code(KC_LCMD);
                    register_code(KC_SPACE);
                }
                siri_timer = timer_read32();
            }
            return false;

        // Key combos (OS-specific shortcuts)
        case KC_TASK:   // Win Task View / Mac Mission Control
        case KC_FILE:   // Win File Explorer / Mac Finder
        case KC_SNAP:   // Win Snip & Sketch / Mac Screenshot
        case KC_CTANA:  // Win Cortana
        case KC_WLCK:   // Win Lock Screen (hold)
        case KC_MLCK:   // Mac Lock Screen (hold)
            if (record->event.pressed) {
                for (uint8_t i = 0; i < key_comb_list[keycode - KC_TASK].len; i++) {
                    register_code(key_comb_list[keycode - KC_TASK].keycode[i]);
                }
            } else {
                for (uint8_t i = 0; i < key_comb_list[keycode - KC_TASK].len; i++) {
                    unregister_code(key_comb_list[keycode - KC_TASK].keycode[i]);
                }
            }
            return false;

        // LED matrix controls
        #ifdef LED_MATRIX_ENABLE
        case BL_SPI:  // Increase LED speed
            if (record->event.pressed) {
                led_matrix_increase_speed();
            }
            break;
        case BL_SPD:  // Decrease LED speed
            if (record->event.pressed) {
                led_matrix_decrease_speed();
            }
            break;
        #endif

        // OS Toggle (hold to switch between Mac/Win layers)
        #ifdef KEYCOMBO_OS_TOGGLE_ENABLE
        case OS_TOGGL:
            #ifdef KEYCOMBO_OS_TOGGLE_HOLD_TIME
            if (record->event.pressed) {
                os_toggle_timer = timer_read32();
            } else {
                os_toggle_timer = 0;
            }
            #else
            if (record->event.pressed) {
                os_toggle();
            }
            #endif
            return false;
        #endif

        // OS Select (combo: hold 3s to switch)
        #ifdef KEYCOMBO_OS_SELECT_ENABLE
        case OS_WIN:
        case OS_MAC:
            if (record->event.pressed) {
                if (os_selected_keycode == 0) {
                    os_selected_keycode = keycode;
                    os_keycombo_timer = timer_read32();
                }
            } else {
                if (os_selected_keycode == keycode) {
                    os_selected_keycode = 0;
                    os_keycombo_timer = 0;
                }
            }
            break;
        #endif

        // GUI Toggle (Windows key enable/disable)
        #ifdef WIN_LOCK_PIN || defined(WINLOCK_HOLD_LIST)
        case GU_TOGG:
            #ifdef WIN_LOCK_HOLD_TIME
            if (record->event.pressed) {
                winlock_timer = timer_read32();
            } else {
                winlock_timer = 0;
            }
            #else
            if (record->event.pressed) gui_toggle();
            #endif
            return false;
        #endif

        default:
            return true;  // Process normally
    }
    return true;
}
```

### 6.2 Key Combinations (OS-Specific Mappings)

**Key Combination Table:**

```c
static key_combination_t key_comb_list[] = {
    {2, {KC_LWIN, KC_TAB}},              // Task View (Win) / Mission Control (Mac)
    {2, {KC_LWIN, KC_E}},                // File Explorer (Win) / Finder (Mac)
    {3, {KC_LSFT, KC_LCMD, KC_4}},       // Screenshot (Mac)
    {2, {KC_LWIN, KC_C}},                // Not defined (reserved)
    #ifdef WIN_LOCK_SCREEN_ENABLE
    {2, {KC_LWIN, KC_L}},                // Lock Screen (Win)
    #endif
    #ifdef MAC_LOCK_SCREEN_ENABLE
    {3, {KC_LCTL, KC_LCMD, KC_Q}},       // Lock Screen (Mac)
    #endif
};
```

**Note:** The keycode index maps to `KC_TASK` (0), `KC_FILE` (1), `KC_SNAP` (2), `KC_CTANA` (3), `KC_WLCK` (4), `KC_MLCK` (5).

### 6.3 Periodic Task Handler

**File:** `keyboards/keychron/common/keychron_task.c`

```c
void keychron_task(void) {
    // Siri timeout: release keys after 500ms
    if (siri_active && timer_elapsed32(siri_timer) > 500) {
        unregister_code(KC_LCMD);
        unregister_code(KC_SPACE);
        siri_active = false;
        siri_timer = 0;
    }

    // Profile selection (analog matrix)
    #ifdef ANALOG_MATRIX
    process_profile_select_combo();
    #endif

    // WinLock hold detection (3s to toggle GUI)
    #if defined(WIN_LOCK_HOLD_TIME)
    if (winlock_timer) {
        if (keymap_config.no_gui) {
            winlock_timer = 0;
            gui_toggle();
        } else if (timer_elapsed32(winlock_timer) > WIN_LOCK_HOLD_TIME) {
            winlock_timer = 0;
            gui_toggle();
        }
    }
    #endif

    // OS Select combo timeout (3s hold to switch)
    #if defined(KEYCOMBO_OS_SELECT_ENABLE) && defined(MAC_BASE_LAYER) && defined(WIN_BASE_LAYER)
    if (os_keycombo_timer && timer_elapsed32(os_keycombo_timer) > 3000) {
        if (os_selected_keycode == OS_WIN) {
            if (get_highest_layer(default_layer_state) == MAC_BASE_LAYER) {
                default_layer_set(1UL << WIN_BASE_LAYER);
                eeprom_update_byte(EECONFIG_DEFAULT_LAYER, 1UL << WIN_BASE_LAYER);
            }
        } else {
            if (get_highest_layer(default_layer_state) == WIN_BASE_LAYER) {
                default_layer_set(1UL << MAC_BASE_LAYER);
                eeprom_update_byte(EECONFIG_DEFAULT_LAYER, 1UL << MAC_BASE_LAYER);
            }
        }
        RGB color = {.r = 255, .g = 0, .b = 0};
        backlight_indicator_start(250, 250, 3, color);
        os_keycombo_timer = 0;
    }
    #endif

    // OS Toggle hold detection
    #if defined(KEYCOMBO_OS_TOGGLE_ENABLE) && defined(KEYCOMBO_OS_TOGGLE_HOLD_TIME)
    if (os_toggle_timer && timer_elapsed32(os_toggle_timer) > KEYCOMBO_OS_TOGGLE_HOLD_TIME) {
        os_toggle();
        os_toggle_timer = 0;
    }
    #endif

    // Other tasks (wireless, factory test, retail demo, etc.)
    #if defined(LK_WIRELESS_ENABLE) || defined(KC_BLUETOOTH_ENABLE)
    wireless_tasks();
    #endif
    #ifdef FACTORY_TEST_ENABLE
    factory_test_task();
    #endif
    #ifdef RETAIL_DEMO_ENABLE
    retail_demo_task();
    #endif

    // LED indicator update
    backlit_indicator_task();

    // RGB matrix animation (if enabled)
    #ifdef RGB_MATRIX_ENABLE
    rgb_matrix_task();
    #endif
}
```

---

## 7. Custom Keycode Definitions

**File:** `keyboards/keychron/common/keychron_common.h`

```c
#ifndef CUSTOM_KEYCODES_ENABLE
enum {
    KC_LOPTN = QK_KB_0,    // Mac Option Left
    KC_ROPTN,              // Mac Option Right
    KC_LCMMD,              // Mac Command Left
    KC_RCMMD,              // Mac Command Right
    KC_MAC_MISSION_CONTROL, // Mac Mission Control
    KC_MAC_LAUNCHPAD,      // Mac Launchpad
    KC_WIN_TASK_VIEW,      // Win Task View
    KC_WIN_FILE_EXPLORER,  // Win File Explorer
    KC_MAC_SCREEN_SHOT,    // Mac Screenshot
    KC_WIN_CORTANA,        // Win Cortana
    #ifdef WIN_LOCK_SCREEN_ENABLE
    KC_WIN_LOCK_SCREEN,    // Win Lock Screen
    __KC_WIN_LOCK_SCREEN_NEXT,
    #else
    __KC_WIN_LOCK_SCREEN_NEXT = KC_WIN_CORTANA + 1,
    #endif
    #ifdef MAC_LOCK_SCREEN_ENABLE
    KC_MAC_LOCK_SCREEN = __KC_WIN_LOCK_SCREEN_NEXT,
    __KC_MAC_LOCK_SCREEN_NEXT,
    #else
    __KC_MAC_LOCK_SCREEN_NEXT = __KC_WIN_LOCK_SCREEN_NEXT,
    #endif
    KC_MAC_SIRI = __KC_MAC_LOCK_SCREEN_NEXT,
    #if defined(LK_WIRELESS_ENABLE) || defined(KC_BLUETOOTH_ENABLE)
    BT_HST1,               // Bluetooth Host 1
    BT_HST2,               // Bluetooth Host 2
    BT_HST3,               // Bluetooth Host 3
    #if (defined(P24G_MODE_SELECT_PIN) || defined(KEYCOMBO_CONN_SWITCH_ENABLE)) && !defined(KC_BLUETOOTH_ENABLE)
    P2P4G,                 // 2.4GHz mode
    __P2P4G_NEXT,
    #else
    __P2P4G_NEXT = BT_HST3 + 1,
    #endif
    #if defined(KEYCOMBO_CONN_SWITCH_ENABLE)
    TP_USB = __P2P4G_NEXT, // USB transport
    __TP_USB_NEXT,
    #else
    __TP_USB_NEXT = __P2P4G_NEXT,
    #endif
    BAT_LVL = __TP_USB_NEXT, // Battery level indicator
    __BAT_LVL_NEXT = BAT_LVL + 1,
    #else
    BT_HST1 = KC_NO,
    BT_HST2 = KC_NO,
    BT_HST3 = KC_NO,
    P2P4G   = KC_NO,
    #if defined(KEYCOMBO_CONN_SWITCH_ENABLE)
    TP_USB  = KC_NO,
    #endif
    BAT_LVL = KC_NO,
    __BAT_LVL_NEXT = KC_MAC_SIRI + 1,
    #endif
    #ifdef KEYCOMBO_OS_TOGGLE_ENABLE
    OS_TOGGL = __BAT_LVL_NEXT, // OS toggle
    __OS_TOGGL_NEXT,
    #else
    __OS_TOGGL_NEXT = __BAT_LVL_NEXT,
    #endif
    #ifdef KEYCOMBO_OS_SELECT_ENABLE
    OS_WIN = __OS_TOGGL_NEXT,   // Select Windows
    OS_MAC,                     // Select macOS
    __OS_MAC_NEXT,
    #else
    __OS_MAC_NEXT = __BAT_LVL_NEXT,
    #endif
    #ifdef ANALOG_MATRIX
    PROF1 = __OS_MAC_NEXT,      // Profile 1
    PROF2,                      // Profile 2
    PROF3,                      // Profile 3
    __PROF3_NEXT,
    #else
    __PROF3_NEXT = __OS_MAC_NEXT,
    #endif
    #ifdef LED_MATRIX_ENABLE
    BL_SPI = __PROF3_NEXT,      // LED speed increase
    BL_SPD,                     // LED speed decrease
    __BL_SPD_NEXT,
    #else
    __BL_SPD_NEXT = __PROF3_NEXT,
    #endif
    NEW_SAFE_RANGE = __BL_SPD_NEXT,
};
#endif
```

**Keycode Range:** `QK_KB_0` (0x0340) to `QK_KB_31` (0x035F) reserved for Keychron custom keycodes.

---

## 8. Data Structures

### 8.1 Matrix Representation

```c
// matrix.h
#if (MATRIX_COLS <= 8)
typedef uint8_t matrix_row_t;
#elif (MATRIX_COLS <= 16)
typedef uint16_t matrix_row_t;
#elif (MATRIX_COLS <= 32)
typedef uint32_t matrix_row_t;
#else
#error "MATRIX_COLS invalid"
#endif

// Global matrix state
extern matrix_row_t matrix[MATRIX_ROWS];
```

**Layout:** Each `matrix_row_t` is a bitmask where bit N represents column N in that row.
- For Q3 Max (likely 6 rows × 17 cols): `matrix_row_t` = `uint32_t`

### 8.2 Key Event Record

```c
// action.h
typedef struct {
    uint16_t  keycode;      // QMK keycode
    uint8_t   flags;        // See below
    uint8_t   reserved;     // For future use
    uint16_t  time;         // Timer value at event (for tap/hold)
} keyrecord_t;

// Flags (from action_tapping.h)
enum {
    TAP_HOLD = 1 << 0,      // Key is in tap-hold detection period
    TAPPED   = 1 << 1,      // Key was tapped (released before hold threshold)
    HELD     = 1 << 2,      // Key is held (exceeded hold threshold)
    RELEASED = 1 << 3,      // Key was released
};
```

### 8.3 Debounce State (Per-Key Example)

```c
// In sym_eager_pk.c
typedef struct {
    uint16_t press_time;     // Timer when pressed
    uint16_t release_time;   // Timer when released
    bool     pressed;        // Current debounced state
} per_key_debounce_t;

static per_key_debounce_t per_key_state[MATRIX_ROWS][MATRIX_COLS];
```

### 8.4 Layer State

```c
// keymap_common.h
typedef uint32_t layer_state_t;  // Bitmask: bit N = layer N active

extern layer_state_t default_layer_state;  // Base layers (persisted to EEPROM)
extern layer_state_t layer_state;          // Active layers (computed)
```

---

## 9. Performance Considerations

### 9.1 Matrix Scan Rate

**Target:** 1000 Hz (1ms interval)
- Typical QMK uses 1kHz timer interrupt for matrix scanning
- For Q3 Max with 6 rows × 17 cols:
  - Direct columns: negligible delay
  - HC595 columns: 8 bits × ~10 cycles = 80 cycles per column
  - Total per scan: ~6 columns × 80 cycles = 480 cycles + overhead
  - At 72MHz: ~7µs per scan → negligible
- **Bottleneck:** Debounce algorithm, not matrix scan

### 9.2 Debounce Algorithm Selection

| Algorithm              | Memory  | CPU      | Latency | Notes                                  |
|------------------------|---------|----------|---------|----------------------------------------|
| `NONE`                 | 0 B     | 0        | 0 ms    | Debug only                            |
| `SYM_DEFER_GLOBAL`    | 2 B     | Low      | 5-20ms  | Single timer for all keys             |
| `SYM_DEFER_PER_ROW`   | 12 B    | Medium   | 5-20ms  | Timer per row (6 rows)                |
| `SYM_DEFER_PER_KEY`   | 102 B   | High     | 5-20ms  | Timer per key (102 keys)              |
| `SYM_EAGER_PER_ROW`   | 12 B    | Medium   | 0-5ms   | Per-row, processes immediately        |
| `SYM_EAGER_PER_KEY`   | 102 B   | High     | 0-5ms   | Best responsiveness, most memory     |
| `ASYM_EAGER_DEFER_PK` | 102 B   | High     | 5-10ms  | Different press/release debounce      |

**Recommendation for Q3 Max:** `SYM_EAGER_PER_KEY` (default) - best feel for mechanical keys.

### 9.3 Memory Usage

| Component               | Size (bytes) |
|-------------------------|--------------|
| Matrix (raw)            | 6 × 4 = 24   |
| Matrix (cooked)         | 6 × 4 = 24   |
| Debounce (per-key)      | 102 × 4 = 408|
| Keymap (4 layers × 102) | 408 × 4 = 1632|
| EEPROM (KB config)      | 32-64        |
| **Total RAM**           | ~2.1 KB      |
| **Flash (code)**        | ~15-25 KB    |

STM32L432 has 256KB Flash, 64KB RAM → plenty of room.

### 9.4 USB Report Rate

- NKRO (N-Key Rollover) enabled
- Report interval: 1000Hz (1ms) by default
- Maximum keys in report: 6 for boot protocol, unlimited for NKRO
- Q3 Max uses NKRO via `USB_POLLING_INTERVAL_MS = 1`

---

## 10. Error Handling & Edge Cases

### 10.1 Matrix Scanning Errors

**Scenario:** Row pin floating or shorted
- **Detection:** Inconsistent readings across scans
- **Mitigation:** 
  - Pull-up resistors on row pins (internal or external)
  - Diode direction ensures only pressed keys register
  - Debounce filters spurious transitions

**Scenario:** Column stuck LOW (short circuit)
- **Detection:** All keys in that column always pressed
- **Mitigation:** 
  - Unselect column after each scan (set to HIGH/input)
  - User can test via factory test mode

### 10.2 Debounce Failures

**Scenario:** Key chattering despite debounce
- **Cause:** Debounce time too short for mechanical switch
- **Mitigation:** Increase debounce time (up to 255ms) via EEPROM config
- **Alternative:** Switch to different algorithm (e.g., ASYM_EAGER_DEFER)

**Scenario:** Key "stuck" (release not detected)
- **Cause:** Debounce algorithm waiting for stable release
- **Mitigation:** 
  - Asymmetric algorithms can shorten release debounce
  - Factory test mode can override debounce for diagnostics

### 10.3 Keycode Processing Errors

**Scenario:** Unknown custom keycode
- **Handling:** `process_record_keychron_common()` returns `true` for default case → processed by QMK core
- **Result:** Key sent as-is (may be ignored by OS if unknown)

**Scenario:** Modifier stacking overflow (too many modifiers)
- **Handling:** QMK core limits to 6 modifiers + 6 regular keys per report
- **Mitigation:** `register_code()` checks for duplicates and capacity

**Scenario:** Layer overflow (more than 16 layers)
- **Handling:** `layer_state_t` is 32-bit, supports up to 32 layers
- **Limitation:** Keymap array size fixed at compile time

### 10.4 EEPROM Corruption

**Scenario:** Power loss during EEPROM write
- **Protection:** Wear leveling driver (embedded_flash)
- **Fallback:** Default settings loaded if EEPROM invalid
- **Recovery:** `eeconfig_init()` resets to defaults

### 10.5 USB Enumeration Failures

**Scenario:** USB descriptor mismatch
- **Handling:** Bootloader provides DFU mode for firmware recovery
- **User action:** Reset via physical button (if present) or magic key combo

---

## 11. Testing Strategy

### 11.1 Unit Tests

**Matrix Scanning:**
```c
TEST(matrix_scan_custom) {
    // Mock row pins, simulate key presses
    // Verify cooked matrix matches expected pattern
    // Test all columns (direct + HC595)
}

TEST(debounce_algorithm) {
    // Simulate noisy key press (bouncing)
    // Verify debounced output is clean
    // Test all algorithms
}
```

**Keycode Processing:**
```c
TEST(process_record_keychron_common) {
    // Test each custom keycode:
    // - KC_MCTRL: verify register/unregister called
    // - KC_SIRI: verify timeout logic
    // - OS_TOGGL: verify layer XOR
    // - Key combos: verify multi-key sequences
}
```

### 11.2 Integration Tests

**Full Key Press/Release Cycle:**
1. Simulate matrix change (raw key press)
2. Run debounce (wait appropriate time)
3. Verify cooked matrix update
4. Check USB report generation
5. Simulate release, verify release report

**Layer Switching:**
1. Set default layer (persisted)
2. Press layer switch key (MO, TO)
3. Verify `layer_state` updates
4. Verify keymap lookup uses new layer
5. Release key, verify layer reverts (for MO)

**OS Toggle Combo:**
1. Hold `OS_TOGGL` for hold time
2. Verify `default_layer_state` XOR toggles Mac/Win
3. Verify EEPROM update
4. Verify LED indicator pattern

### 11.3 Hardware Tests

**Factory Test Mode:**
- Enable via DIP switch or key combo (FN+1+2)
- Test all keys: matrix scan reads correctly
- Test encoders: rotation generates correct keycodes
- Test RGB matrix: all LEDs light in sequence
- Test USB: enumerate, send reports, verify on host

**Stress Tests:**
- Rollover: press all keys simultaneously, verify NKRO
- Ghosting: test key combos that typically ghost (no diodes)
- Rapid typing: 10k keypresses, monitor for missed events
- Temperature: test at 0°C and 50°C (mechanical switch tolerance)

### 11.4 Performance Benchmarks

| Metric                  | Target                  | Measurement Method               |
|-------------------------|-------------------------|----------------------------------|
| Matrix scan time        | < 100µs                 | GPIO toggling + oscilloscope    |
| Debounce latency        | 0-5ms (eager) / 5-20ms  | Timer capture on key press      |
| USB report latency      | < 1ms                   | Logic analyzer on D+/- lines    |
| CPU utilization         | < 50% at 1kHz scan      | Cycle counter in matrix_scan    |
| Memory footprint        | < 64KB RAM, < 256KB Flash| Size of compiled binary         |

---

## 12. Implementation Roadmap

### Phase 1: Foundation (Week 1)
- [ ] Set up QMK build environment for STM32L432
- [ ] Create keyboard directory structure (`keyboards/keychron/q3/ansi_encoder/`)
- [ ] Implement `info.json` with hardware specs
- [ ] Create `rules.mk` with feature flags
- [ ] Implement `config.h` with pin definitions (HC595, rows, cols)
- [ ] Write `matrix.c` (HC595 scanning)
- [ ] Test matrix scanning with simple keymap (all keys → KC_NO)

### Phase 2: Key Processing (Week 2)
- [ ] Implement debounce system (select `SYM_EAGER_PER_KEY`)
- [ ] Create `keychron_common.h/c` (process_record)
- [ ] Define custom keycodes in `keycodes_custom.h`
- [ ] Implement keymap with 4 layers (Mac/Win base + FN)
- [ ] Test basic keypress → USB report flow
- [ ] Verify NKRO (n-key rollover)

### Phase 3: Custom Features (Week 3)
- [ ] Implement OS toggle (layer switch)
- [ ] Implement OS select combo (3s hold)
- [ ] Implement Siri key (500ms hold)
- [ ] Implement key combos (Task View, File Explorer, etc.)
- [ ] Implement GUI toggle (Win lock)
- [ ] Test all custom keycodes on host OS

### Phase 4: Encoder Support (Week 4)
- [ ] Enable encoder in `config.h` (`ENCODER_ENABLE`)
- [ ] Define encoder pins (A/B for each encoder)
- [ ] Implement encoder callback (`encoder_cb_init`)
- [ ] Map encoders to volume/scrolling per layer
- [ ] Test encoder rotation (CW/CCW)

### Phase 5: RGB Matrix (Week 5)
- [ ] Enable `RGB_MATRIX_ENABLE` and `KEYCHRON_RGB_ENABLE`
- [ ] Implement LED driver (`snled27351` in `ansi_encoder.c`)
- [ ] Define LED layout (per-key mapping to SNLED27351 channels)
- [ ] Implement default colors (per-key HSV)
- [ ] Implement region masks for effects
- [ ] Test all LEDs light correctly
- [ ] Implement basic animations (breathing, cycle)

### Phase 6: Task System (Week 6)
- [ ] Implement `keychron_task.c` (periodic tasks)
- [ ] Add LED indicator support (`backlit_indicator`)
- [ ] Add factory test mode (DIP switch or key combo)
- [ ] Add wireless task scheduling (if BT hardware)
- [ ] Add battery level indicator (if battery hardware)
- [ ] Test all tasks run without blocking matrix scan

### Phase 7: Optimization & Polish (Week 7)
- [ ] Tune debounce times (user-configurable via VIA)
- [ ] Optimize matrix scan rate (target 1kHz)
- [ ] Minimize flash/ram usage (check `size` output)
- [ ] Add power management (sleep mode, USB suspend)
- [ ] Implement EEPROM wear leveling
- [ ] Test power consumption (battery life if wireless)

### Phase 8: Validation & Documentation (Week 8)
- [ ] Run full hardware test suite (all keys, encoders, LEDs)
- [ ] Perform stress tests (rollover, rapid typing)
- [ ] Document keymap layout (README.md)
- [ ] Document build/flash instructions
- [ ] Create VIA JSON (if VIA support desired)
- [ ] Release firmware binaries (.bin, .hex)

---

## 13. Critical Implementation Details

### 13.1 Build Configuration

**`keyboards/keychron/q3/ansi_encoder/rules.mk`:**
```makefile
KEYCHRON_RGB_ENABLE = yes

SRC += keyboards/keychron/common/matrix.c
```

**`keyboards/keychron/common/keychron_common.mk`:**
```makefile
OPT_DEFS += -DFACTORY_TEST_ENABLE -DSTATE_NOTIFY_ENABLE

KEYCHRON_COMMON_DIR = $(TOP_DIR)/keyboards/keychron/common
SRC += \
    $(KEYCHRON_COMMON_DIR)/keychron_task.c \
    $(KEYCHRON_COMMON_DIR)/keychron_common.c \
    $(KEYCHRON_COMMON_DIR)/keychron_raw_hid.c \
    $(KEYCHRON_COMMON_DIR)/factory_test.c \
    $(KEYCHRON_COMMON_DIR)/backlit_indicator.c \
    $(KEYCHRON_COMMON_DIR)/eeconfig_kb.c \
    $(KEYCHRON_COMMON_DIR)/dfu_info.c \
    $(KEYCHRON_COMMON_DIR)/nkro.c \
    $(KEYCHRON_COMMON_DIR)/state_notify.c

VPATH += $(KEYCHRON_COMMON_DIR)

INFO_RULES_MK = $(shell $(QMK) --escape-quotes --output $(INTERMEDIATE_OUTPUT)/src/info_rules.mk \
    --keyboard $(KEYBOARD) --include $(KEYCHRON_COMMON_DIR)/info_rules.mk)

include $(INFO_RULES_MK)

ifeq ($(strip $(DEBOUNCE_TYPE)), custom)
include $(KEYCHRON_COMMON_DIR)/debounce/debounce.mk
endif
```

### 13.2 Pin Definitions (from `config.h`)

```c
// HC595 Shift Register Pins
#define HC595_STCP B0      // Storage clock (latch)
#define HC595_SHCP B1      // Shift clock
#define HC595_DS   A7      // Data serial

// Column mapping
#define HC595_START_INDEX 8    // First HC595-controlled column
#define HC595_END_INDEX   15   // Last HC595-controlled column
#define HC595_OFFSET_INDEX 0   // Offset for additional chips

// Encoder pins (if enabled)
#ifdef ENCODER_ENABLE
#define ENCODER_DEFAULT_POS 0x3
#endif

// RGB Matrix (I2C)
#ifdef RGB_MATRIX_ENABLE
#define SNLED27351_I2C_ADDRESS_1 SNLED27351_I2C_ADDRESS_VDDIO
#define SNLED27351_I2C_ADDRESS_2 SNLED27351_I2C_ADDRESS_GND
#define I2C1_TIMINGR_PRESC 0U
#define I2C1_TIMINGR_SCLDEL 3U
#define I2C1_TIMINGR_SDADEL 0U
#define I2C1_TIMINGR_SCLH 15U
#define I2C1_TIMINGR_SCLL 51U
#define SNLED27351_PHASE_CHANNEL SNLED27351_SCAN_PHASE_9_CHANNEL
#endif
```

**Matrix Pins (in `matrix.c`):**
```c
// These must be defined in keyboard's config.h or rules.mk
#define MATRIX_ROW_PINS { B10, B11, B12, B13, B14, B15 }  // Example: 6 rows
#define MATRIX_COL_PINS { A0, A1, A2, A3, A4, A5, A6, A8, \
                          B2, B3, B4, B5, B6, B7, B8, B9, C13 }  // 17 cols
```

### 13.3 Keymap Structure

```c
// LAYOUT_tkl_f13_ansi() - 87-key ANSI layout
// Row 0: Esc, F1-F12, Pause, PrSc, ...
// Row 1: `123, qwer, asdf, zxcv, Bksl
// Row 2: Tab, wasd, ...
// ...
// Row 5: Ctrl, GUI, Alt, Space, ...

enum layers {
    MAC_BASE,
    MAC_FN,
    WIN_BASE,
    WIN_FN,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_tkl_f13_ansi(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_PSCR, KC_SLCK, KC_BRK,  KC_,
        KC_TRNS, KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_INS,  KC_DEL,  KC_PMPS,
        KC_CAPS, KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS, KC_HOME, KC_END,  KC_PGDN,
        KC_TAB,  KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT, KC_ENT,  KC_PGUP, KC_PGDN, KC_,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT, KC_UP,   KC_,
        KC_LCTL, KC_LOPTN, KC_LCMD, KC_SPC,  KC_RCMD, KC_ROPTN, KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT, KC_,
    ),
    [MAC_FN] = LAYOUT_tkl_f13_ansi(
        _______, KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______, _______, _______, _______,
        _______, KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______, UG_VALD, UG_VALU, UG_NEXT,
        _______, UG_VALD, UG_VALU, UG_HUE__, UG_SAT__, UG_SPDU, _______, _______, _______, _______, _______, _______, _______, _______, UG_PREV, UG_NEXT, UG_VALU,
        _______, UG_PREV, UG_HUE__, UG_SAT__, UG_SPDU, _______, _______, _______, _______, _______, _______, _______, _______, _______, UG_VALD, UG_VALU, UG_HUE__,
        _______, UG_SAT__, UG_SPDU, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, UG_PREV, UG_HUE__, UG_SAT__,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, UG_SPDU, UG_VALD, UG_VALU,
    ),
    // WIN_BASE and WIN_FN similar but with Windows-specific keycodes
};
```

**Encoder Mapping:**
```c
#ifdef ENCODER_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [MAC_BASE] = { ENCODER_CCW_C(KC_VOLD), ENCODER_CW_C(KC_VOLU) },
    [MAC_FN]   = { ENCODER_CCW_C(UG_VALD),  ENCODER_CW_C(UG_VALU)  },
    [WIN_BASE] = { ENCODER_CCW_C(KC_VOLD), ENCODER_CW_C(KC_VOLU) },
    [WIN_FN]   = { ENCODER_CCW_C(UG_VALD),  ENCODER_CW_C(UG_VALU)  },
};
#endif
```

---

## 14. References

### QMK Firmware Core Documentation
- `quantum/quantum.h` - Main QMK API
- `quantum/matrix.h` - Matrix scanning interface
- `quantum/action.h` - Key event processing
- `quantum/keymap_common.h` - Layer and keymap utilities

### Keychron-Specific Modules
- `keyboards/keychron/common/keychron_common.c/h` - Custom key processing
- `keyboards/keychron/common/matrix.c` - HC595 matrix scanner
- `keyboards/keychron/common/debounce/keychron_debounce.c/h` - Debounce system
- `keyboards/keychron/common/keychron_task.c/h` - Periodic task scheduler

### Hardware References
- STM32L432 Datasheet (ARM Cortex-M4, 72MHz, 256KB Flash, 64KB RAM)
- SNLED27351 RGB LED Driver (I2C, 144 LEDs max)
- HC595 Shift Register (8-bit, SPI-compatible)
- Keychron Q3 Max schematics (if available)

---

## Appendix A: Configuration Checklist

Before building firmware, verify:

- [ ] `MATRIX_ROWS` and `MATRIX_COLS` defined correctly
- [ ] `MATRIX_ROW_PINS` array matches hardware wiring
- [ ] `MATRIX_COL_PINS` array matches hardware wiring
- [ ] `HC595_STCP`, `HC595_SHCP`, `HC595_DS` pins correct
- [ ] `HC595_START_INDEX` and `HC595_END_INDEX` match column mapping
- [ ] `DIODE_DIRECTION` set to `ROW2COL` (confirmed in `info.json`)
- [ ] `DEBOUNCE` type set (default 5 for `SYM_EAGER_PER_KEY`)
- [ ] Custom keycodes enabled (`CUSTOM_KEYCODES_ENABLE = yes`)
- [ ] Encoder pins defined if `ENCODER_ENABLE`
- [ ] RGB matrix pins (I2C) defined if `RGB_MATRIX_ENABLE`
- [ ] Layer definitions match physical keymap
- [ ] `info.json` correctly describes hardware features

---

## Appendix B: Debugging Tips

**Matrix Scan Issues:**
1. Enable `MATRIX_DEBUG` in `config.h`
2. Use `matrix_print()` to dump raw matrix state
3. Check row pins with multimeter (should be HIGH when no key pressed)
4. Check column pins toggle during scan (HC595 output)

**Debounce Issues:**
1. Increase `DEBOUNCE` value (e.g., to 10 or 20)
2. Switch to `ASYM_EAGER_DEFER_PER_KEY` for release-heavy bounce
3. Monitor `debounce_rx()` via Raw HID to change in real-time

**Keycode Issues:**
1. Enable `ACTION_DEBUG` in `config.h`
2. Check `process_record_user()` and `process_record_keychron_common()` logs
3. Verify keycode values in `keycodes.h` and `keycodes_custom.h`

**USB Issues:**
1. Check `USB_VID` and `USB_PID` in `info.json` (must be unique)
2. Verify USB descriptor (`usb_descriptor.c`) matches device type
3. Use `hid_listen` tool to monitor HID reports

---

**End of Document**
