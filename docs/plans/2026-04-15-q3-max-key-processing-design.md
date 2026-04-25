<!-- markdownlint-disable-file -->

# QMK Key Processing Design: Keychron Q3 Max

**Date:** 2026-04-15  
**Author:** Architecture Analysis  
**Status:** Documentation  

---

## Abstract

This document describes the complete key processing pipeline in QMK firmware for the Keychron Q3 Max keyboard. It traces the journey from physical key press to HID output, covering hardware scanning, signal processing, action interpretation, and host communication.

---

## 1. System Overview

### 1.1 Hardware Specifications

| Component | Specification |
|-----------|---------------|
| Microcontroller | STM32F401 (ARM Cortex-M4) |
| Matrix Size | 6 rows × 17 columns (102 switches) |
| Diode Direction | ROW2COL |
| Column Driver | HC595 shift registers (dual) |
| RGB Driver | SNLED27351 (87 LEDs) |
| Wireless | LKBT51 Bluetooth + P24G 2.4GHz |
| Encoder | Rotary encoder (volume) |

### 1.2 Processing Pipeline Summary

```text
Physical Press → Matrix Scan → Debounce → Action Lookup → Keycode Processing → HID Report
```

---

## 2. Key Processing Architecture

### 2.1 High-Level Components

```
┌─────────────────────────────────────────────────────────────────┐
│                        Main Loop                                 │
│                     (keyboard_task)                              │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Matrix Scan  │→ │   Debounce   │→ │   Action Processing  │  │
│  │              │  │              │  │                      │  │
│  │ - HC595 col  │  │ - Edge detect│  │ - Layer resolution   │  │
│  │ - Row read   │  │ - State filter│ │ - Keycode mapping    │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│                                            │                    │
│                                            ▼                    │
│                                    ┌──────────────┐            │
│                                    │  HID Output  │            │
│                                    │  USB/Wireless│            │
│                                    └──────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Component Files

| Component | Primary File | Location |
|-----------|--------------|----------|
| Matrix Scanning | `matrix.c` | `keyboards/keychron/common/` |
| Matrix Core | `matrix.c` | `quantum/` |
| Action Processing | `action.c` | `quantum/` |
| Keyboard Main Loop | `keyboard.c` | `quantum/` |
| Keychron Common | `keychron_common.c` | `keyboards/keychron/common/` |
| Q3 Max Specific | `q3_max.c` | `keyboards/keychron/q3_max/` |

---

## 3. Matrix Scanning (Hardware Layer)

### 3.1 Hardware Configuration

**File:** `keyboards/keychron/common/matrix.c`

The Q3 Max uses ROW2COL diode direction with HC595 shift registers for column selection:

```c
// HC595 Control Pins (from config.h)
#define HC595_STCP  B2  // Storage register clock
#define HC595_SHCP  B3  // Shift register clock
#define HC595_DS    B4  // Data serial input
```

**Matrix Pin Assignment:**
- **Rows:** Direct GPIO pins (configured as input with pull-up)
- **Columns:** HC595 shift registers (17 columns across 2 chips)

### 3.2 Scan Algorithm

**Step 1: Column Selection** (Line 109-118, `matrix.c`)
```c
static void select_col(uint8_t col) {
    if (col < HC595_START_INDEX || col > HC595_END_INDEX) {
        // Direct GPIO column
        gpio_set_pin_output_push_pull_writeLow(col_pins[col]);
    } else {
        // HC595-controlled column
        HC595_output(0x00, true);  // Set specific bit
    }
}
```

**Step 2: Row Reading** (Line 164-182, `matrix.c`)
```c
static void matrix_read_rows_on_col(matrix_row_t current_matrix[], 
                                    uint8_t current_col, 
                                    matrix_row_t row_shifter) {
    select_col(current_col);
    HC595_delay(200);  // Settling time
    
    for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
        if (readMatrixPin(row_pins[row_index]) == 0) {
            // Key pressed (LOW signal)
            current_matrix[row_index] |= row_shifter;
        } else {
            // Key released (HIGH signal)
            current_matrix[row_index] &= ~row_shifter;
        }
    }
    
    unselect_col(current_col);
    HC595_delay(200);
}
```

**Step 3: Full Matrix Scan** (Line 203-216, `matrix.c`)
```c
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_row_t curr_matrix[MATRIX_ROWS] = {0};
    
    matrix_row_t row_shifter = MATRIX_ROW_SHIFTER;
    for (uint8_t current_col = 0; current_col < MATRIX_COLS; 
         current_col++, row_shifter <<= 1) {
        matrix_read_rows_on_col(curr_matrix, current_col, row_shifter);
    }
    
    bool changed = memcmp(current_matrix, curr_matrix, sizeof(curr_matrix)) != 0;
    if (changed) memcpy(current_matrix, curr_matrix, sizeof(curr_matrix));
    
    return changed;
}
```

### 3.3 Timing Characteristics

| Operation | Duration |
|-----------|----------|
| HC595 shift pulse | ~200 cycles |
| Column settling delay | 200 cycles |
| Full matrix scan | ~17 cols × (200 + row_read + 200) cycles |
| Estimated scan rate | ~1-2 kHz |

---

## 4. Debounce Processing

### 4.1 Debounce Module

**File:** `quantum/debounce/` (multiple implementations)

QMK uses a state-machine-based debounce system:

```c
// From quantum/matrix.c (line 339-342)
changed = debounce(raw_matrix, matrix, MATRIX_ROWS_PER_HAND, changed);
```

**Debounce States:**
1. **IDLE** - No key activity
2. **DEBOUNCE_PRESS** - Key detected, waiting for stable press
3. **DEBOUNCE_RELEASE** - Key released, waiting for stable release

### 4.2 Debounce Configuration

**File:** `keyboards/keychron/q3_max/config.h`

The Q3 Max inherits default QMK debounce settings (typically 5-10ms):

```c
// Default debounce timing (from quantum/debounce/sym_defer_pk.c)
#define DEBOUNCE_DELAY 5  // milliseconds
```

### 4.3 Raw vs. Debounced Matrix

```c
// From quantum/matrix.c (line 62-63)
extern matrix_row_t raw_matrix[MATRIX_ROWS];  // Raw scan values
extern matrix_row_t matrix[MATRIX_ROWS];      // Debounced values
```

---

## 5. Action Processing (Core Logic)

### 5.1 Main Processing Loop

**File:** `quantum/keyboard.c` (line 586-639)

```c
static bool matrix_task(void) {
    matrix_scan();  // Scan hardware matrix
    
    // Detect matrix changes
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        const matrix_row_t current_row = matrix_get_row(row);
        const matrix_row_t row_changes = current_row ^ matrix_previous[row];
        
        if (!row_changes || has_ghost_in_row(row, current_row)) {
            continue;
        }
        
        // Process each changed key
        matrix_row_t col_mask = 1;
        for (uint8_t col = 0; col < MATRIX_COLS; col++, col_mask <<= 1) {
            if (row_changes & col_mask) {
                const bool key_pressed = current_row & col_mask;
                
                if (should_process_keypress()) {
                    action_exec(MAKE_KEYEVENT(row, col, key_pressed));
                }
                
                switch_events(row, col, key_pressed);  // RGB matrix events
            }
        }
        
        matrix_previous[row] = current_row;
    }
}
```

### 5.2 Action Execution

**File:** `quantum/action.c` (line 77-149)

```c
void action_exec(keyevent_t event) {
    if (event.pressed) {
        clear_weak_mods();  // Clear pending weak modifiers
    }
    
    keyrecord_t record = {.event = event};
    
#ifndef NO_ACTION_TAPPING
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        action_tapping_process(record);  // Handle tap-dance, tap-hold
    }
#else
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        process_record(&record);  // Direct processing
    }
#endif
}
```

### 5.3 Keycode Lookup

**File:** `quantum/keymap_common.c`

```c
// Get keycode from keymap based on current layer state
uint16_t keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col) {
    return pgm_read_word(&keymaps[layer][row][col]);
}
```

### 5.4 Layer Resolution

**File:** `quantum/action_layer.c`

QMK supports up to 32 layers with nested layer states:

```c
// Layer state is a 32-bit bitmask
typedef uint32_t layer_state_t;

// Current active layers
extern layer_state_t layer_state;
```

**Layer Types:**
- **MOMENTARY** - Active while key held
- **TOGGLE** - Stays active until toggled off
- **DEFAULT** - Base layer when no others active
- **ONESHOT** - Active for next keypress only

---

## 6. Keycode Processing Pipeline

### 6.1 Process Keycode System

**Directory:** `quantum/process_keycode/`

QMK uses a modular keycode processor chain:

```
┌─────────────────────────────────────────────────────────────┐
│                    Process Keycode Chain                     │
├─────────────────────────────────────────────────────────────┤
│  1. process_record_user()  - User customization              │
│  2. process_record_kb()    - Keyboard customization          │
│  3. process_record_quantum() - Core QMK processing           │
│     ├─ process_combo()                                        │
│     ├─ process_tap_dance()                                    │
│     ├─ process_leader()                                       │
│     ├─ process_unicode()                                      │
│     ├─ process_macros()                                       │
│     ├─ process_default_layer()                                │
│     └─ ... (20+ modules)                                      │
│  4. do_code()          - Final HID generation                │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Q3 Max Custom Processing

**File:** `keyboards/keychron/q3_max/q3_max.c` (line 77-113)

The Q3 Max overrides modifier key handling for RDP compatibility:

```c
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // Send modifiers sequentially for RDP compatibility
        bool right = !!(code & QK_RMODS_MIN);
        if (code & QK_LCTL) register_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        uint8_t basic = code & 0xFF;
        if (basic) register_code(basic);
    } else {
        // Standard behavior for non-composite modifiers
        register_code(code);
    }
}
```

### 6.3 Keychron Common Features

**File:** `keyboards/keychron/common/keychron_common.c`

Common QMK features for all Keychron keyboards:

- Factory test mode
- Language switching
- RGB matrix integration
- Wireless mode handling

---

## 7. HID Output

### 7.1 USB HID Report

**File:** `tmk_core/protocol/usb_hid/`

Standard HID keyboard report format:

```c
typedef struct {
    uint8_t modifiers;      // 8 bits: Ctrl, Shift, Alt, GUI (left/right)
    uint8_t reserved;       // Reserved byte
    uint8_t keys[6];        // Up to 6 simultaneous keys (or 248 with NKRO)
} PACKED usb_keyboard_report_t;
```

### 7.2 Wireless Report

**File:** `keyboards/keychron/common/wireless/`

For wireless mode, reports are sent via:
- **Bluetooth** (LKBT51 module)
- **2.4GHz** (P24G module)

---

## 8. Data Structures

### 8.1 Key Event

```c
typedef struct {
    keypos_t key;    // Position {row, col}
    bool     pressed;
} keyevent_t;
```

### 8.2 Key Record

```c
typedef struct keyrecord_t {
    keyevent_t event;
    tap_t      tap;      // Tap state for tap-hold/tap-dance
    uint16_t   keycode;  // Resolved keycode
} keyrecord_t;
```

### 8.3 Action

```c
typedef struct {
    uint16_t code;  // Keycode or action code
    uint8_t  layer; // Target layer (for layer actions)
    uint8_t  flags; // Action flags
} action_t;
```

---

## 9. Timing and Performance

### 9.1 Latency Breakdown

| Stage | Typical Latency |
|-------|-----------------|
| Mechanical switch actuation | 5-8 ms |
| Matrix scan (half matrix) | 0.5 ms |
| Debounce | 5 ms |
| Action processing | < 0.1 ms |
| HID report transmission | 1 ms (USB), 4-8 ms (BT) |
| **Total (USB)** | **~12 ms** |
| **Total (BT)** | **~16 ms** |

### 9.2 Main Loop Frequency

```c
// From tmk_core/common/main.c
void main_task(void) {
    keyboard_task();  // Called as fast as possible
}
```

The main loop runs continuously at maximum CPU speed (~168 MHz for STM32F401).

---

## 10. Override Hooks

### 10.1 Keyboard-Level Hooks

```c
// In keyboard-specific .c file
void matrix_init_kb(void) {
    // Hardware-specific initialization
    matrix_init_user();
}

void matrix_scan_kb(void) {
    // Per-scan customization
    matrix_scan_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Keyboard-level keycode processing
    return process_record_user(keycode, record);
}
```

### 10.2 User-Level Hooks

```c
// In keymap .c file
void keyboard_post_init_user(void) {
    // User initialization after QMK setup
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // User keycode processing
    return true;  // Continue processing
}
```

---

## 11. Special Features

### 11.1 Rotary Encoder

**File:** `quantum/encoder/encoder.c`

The Q3 Max includes a rotary encoder for volume control:

```c
// Encoder state machine
typedef enum {
    ENCODER_STATE_RESET,
    ENCODER_STATE_DEBOUNCE,
    ENCODER_STATE_READ
} encoder_state_t;
```

### 11.2 RGB Matrix

**File:** `quantum/rgb_matrix/rgb_matrix.c`

87-zone RGB with SNLED27351 driver:

```c
// Per-key RGB effects
void rgb_matrix_task(void) {
    rgb_matrix_animate();
    rgb_matrix_indicators_user();
}
```

### 11.3 Wireless Modes

**File:** `keyboards/keychron/common/wireless/wireless.h`

Three connection modes:
1. **Wired USB** - Direct HID over USB
2. **Bluetooth** - Up to 3 paired devices
3. **2.4GHz** - Dedicated wireless dongle

---

## 12. Debug Facilities

### 12.1 Debug Flags

```c
// Enable in config.h
#define DEBUG_MATRIX_SCAN_RATE
#define DEBUG_ACTION
#define DEBUG_KEYCODE
```

### 12.2 Console Output

With `CONSOLE_ENABLE`:
```c
matrix_print();     // Print current matrix state
debug_event(event); // Print key event details
```

---

## 13. File Reference

### 13.1 Core Files

| File | Purpose |
|------|---------|
| `quantum/keyboard.c` | Main keyboard task and matrix processing |
| `quantum/matrix.c` | Generic matrix scanning |
| `quantum/action.c` | Action execution and tapping |
| `quantum/action_layer.c` | Layer state management |
| `quantum/keymap_common.c` | Keymap utilities |

### 13.2 Keychron-Specific Files

| File | Purpose |
|------|---------|
| `keyboards/keychron/common/matrix.c` | HC595 matrix scanning |
| `keyboards/keychron/common/keychron_common.c` | Common Keychron features |
| `keyboards/keychron/q3_max/q3_max.c` | Q3 Max specific code |
| `keyboards/keychron/q3_max/config.h` | Q3 Max configuration |
| `keyboards/keychron/q3_max/ansi_encoder/ansi_encoder.c` | ANSI layout LED config |

### 13.3 Process Keycode Modules

| File | Feature |
|------|---------|
| `process_combo.c` | Key combinations |
| `process_tap_dance.c` | Tap dance sequences |
| `process_leader.c` | Leader key |
| `process_unicode.c` | Unicode input |
| `process_rgb_matrix.c` | RGB effects on key events |

---

## 14. Summary

The QMK key processing pipeline for the Keychron Q3 Max follows this flow:

1. **Hardware Scan** - HC595 shift registers select columns, GPIO reads rows
2. **Debouncing** - State machine filters switch bounce
3. **Change Detection** - XOR comparison identifies changed keys
4. **Action Lookup** - Keymap + layer state → keycode
5. **Process Chain** - Modular keycode processors handle features
6. **HID Generation** - USB/wireless report to host

The architecture is modular, allowing customization at multiple levels:
- **Hardware** - Override matrix scanning
- **Keyboard** - Board-specific processing
- **Keymap** - User customization
- **Processors** - Feature modules

---

## Appendix A: Key Defines

```c
// Matrix configuration (from keyboard.json)
#define MATRIX_ROWS 6
#define MATRIX_COLS 17
#define DIODE_DIRECTION ROW2COL

// HC595 configuration
#define HC595_START_INDEX 0
#define HC595_END_INDEX 16
#define HC595_OFFSET_INDEX 0

// RGB configuration
#define RGB_MATRIX_LED_COUNT 87
#define CAPS_LOCK_INDEX 50
```

---

## Appendix B: Build Configuration

```bash
# Compile Q3 Max firmware
make keychron/q3_max/ansi_encoder:keychron

# Flash to keyboard
make keychron/q3_max/ansi_encoder:keychron:flash

# Clean build
make keychron/q3_max/ansi_encoder:keychron:clean
```

---

*Document Version: 1.0*  
*Generated: 2026-04-15*  
*QMK Firmware: Keychron/qmk_firmware*
