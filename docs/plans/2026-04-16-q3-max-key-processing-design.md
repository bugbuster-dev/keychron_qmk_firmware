# Key Processing Design Document - Keychron Q3 Max

## 1. Overview

This document describes the complete key processing pipeline in QMK firmware for the Keychron Q3 Max keyboard, from hardware interrupt to HID report generation.

### 1.1 Hardware Specification

| Component | Details |
|-----------|---------|
| MCU | STM32F401 |
| Matrix | 6 rows × 17 columns (102 keys max) |
| Encoder | 1 rotary encoder (pins B15/B14) |
| RGB LED Driver | SNLED27351 (2 drivers, SPI interface) |
| Bootloader | STM32-DFU |
| USB VID:PID | 0x3434:0xXXXX |

### 1.2 Processing Pipeline Summary

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Q3 MAX KEY PROCESSING PIPELINE                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  [HW Interrupt] → [Matrix Scan] → [Dewalking] → [Action Layer] → [Keymap]  │
│                                                              ↓             │
│                                                       [User Hooks]          │
│                                                              ↓             │
│                                                    [Register/Unregister]     │
│                                                              ↓             │
│                                                    [HID Report] → [USB]    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Hardware Abstraction Layer

### 2.1 Matrix Configuration

**File:** `keyboards/keychron/q3_max/info.json`

```c
"matrix_pins": {
    "cols": ["C6", "C7", "C8", "A14", "A15", "C10", "C11", "C13", "C14", "C15", "C0", "C1", "C2", "C3", "A0", "A1", "A2"],
    "rows": ["C12", "D2", "B3", "B4", "B5", "B6"]
}
```

**Diode Direction:** ROW2COL (anodes to rows, cathodes to columns)

This configuration means:
- Rows are scan lines (output)
- Columns are sense lines (input with pull-ups)
- Each key is at row × column intersection

### 2.2 Encoder Configuration

**File:** `keyboards/keychron/q3_max/info.json`

```c
"encoder": {
    "rotary": [
        {
            "pin_a": "B15",
            "pin_b": "B14"
        }
    ]
}
```

Encoder uses quadrature signaling:
- A leads B = clockwise
- B leads A = counter-clockwise

---

## 3. Matrix Scanning

### 3.1 Scan Process

QMK's matrix scanning (`quantum/matrix.c`) operates as:

1. **Row Selection:** Set one row LOW (sink), others HIGH (float)
2. **Column Reading:** Read column pins to detect pressed keys
3. **Debouncing:** Apply debounce algorithm to filter mechanical bounce
4. **State Change Detection:** Track key press/release state changes

**Debounce Configuration:**
```c
// From info.json
"debounce_type": "custom"
"debounce": 50  // 50ms debounce time
```

### 3.2 Key State Representation

Each key state is represented in a `keyrecord_t` structure:

```c
typedef struct keyrecord_t {
    keyevent_t event;      // Key event data
    uint16_t keycode;      // Resolved keycode
    uint8_t  layer;        // Active layer when processed
} keyrecord_t;

typedef struct keyevent_t {
    keypos_t key;          // Physical position (row, col)
    bool     pressed;      // true=press, false=release
    uint32_t time;         // Event timestamp
} keyevent_t;
```

---

## 4. Action Layer Processing

### 4.1 Action Determination Flow

The action layer (`quantum/action.c`) resolves the raw matrix position to an action:

```
Matrix Position (row, col)
         ↓
    [Action Layer]
         ↓
    Resolved Action:
    - Key press/release
    - Layer switch (MO, TG, TT, etc.)
    - Modifiers (MT, OSM)
    - Macro
    - Custom action
```

### 4.2 Layer System

Q3 Max uses a **layered keymap** with 4 base layers:

| Layer | Name | Purpose |
|-------|------|---------|
| 0 | MAC_BASE | macOS default layout |
| 1 | MAC_FN | macOS function layer |
| 2 | WIN_BASE | Windows/Linux default layout |
| 3 | WIN_FN | Windows function layer |

**Keymap Definition** (`ansi_encoder/keymaps/keychron/keymap.c`):
```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_tkl_ansi(
        KC_ESC,   KC_BRID,  KC_BRIU,  KC_MCTRL, KC_LNPAD, ...
        ...
    ),
    [MAC_FN] = LAYOUT_tkl_ansi(...),
    [WIN_BASE] = LAYOUT_tkl_ansi(...),
    [WIN_FN] = LAYOUT_tkl_ansi(...),
};
```

### 4.3 Default Layer Switching

The DIP switch (`config.h`: `dip_switch_update_kb`) toggles between Mac/Windows:

```c
// From q3_max.c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (dip_switch_update_user(index, active)) return true;
    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));  // 2=WIN, 0=MAC
    }
    return true;
}
```

---

## 5. User Processing Hooks

### 5.1 Processing Order

QMK provides user hooks at multiple stages:

```
pre_process_record_user()    →  Process keycode before standard handling
        ↓
process_record_user()        →  Main user keycode processing
        ↓
post_process_record_user()   →  Post-processing (e.g., leader key)
```

### 5.2 Q3 Max User Processing

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c`

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Leader key integration
    #if defined(COMBO_ENABLE) && defined(LEADER_ENABLE)
    if (record->keycode == QK_LEADER && record->event.pressed) {
        leader_start();
        return false;
    }
    #endif

    // Keychron common processing (OS-specific keycodes)
    if (!process_record_keychron_common(keycode, record)) {
        return false;
    }
    return true;
}
```

### 5.3 Keychron Common Processing

**File:** `keyboards/keychron/common/keychron_common.c`

Handles macOS-specific keycodes that translate to different USB HID codes:

```c
bool process_record_keychron_common(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_LOPTN:  // Left Option (macOS)
            if (record->event.pressed) {
                register_code(KC_LALT);  // Send as Alt to host
            } else {
                unregister_code(KC_LALT);
            }
            return false;  // Skip standard processing
        // ... more cases
    }
}
```

---

## 6. HID Report Generation

### 6.1 Modifier Key Handling

Q3 Max overrides the standard `register_code16`/`unregister_code16` to send each modifier separately:

**File:** `keyboards/keychron/q3_max/q3_max.c`

```c
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // Send each modifier as separate HID report
        bool right = !!(code & QK_RMODS_MIN);
        if (code & QK_LCTL) register_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        // ...
    }
}
```

**Why:** RDP and some remote desktop clients expect modifiers to arrive sequentially, not batched.

### 6.2 HID Keycode Mapping

Standard keycodes map directly to USB HID usage codes. Custom keycodes:

| Keycode | Description | HID Output |
|---------|-------------|------------|
| KC_LOPTN | Left Option | KC_LEFT_ALT |
| KC_ROPTN | Right Option | KC_RIGHT_ALT |
| KC_LCMMD | Left Command | KC_LEFT_GUI |
| KC_RCMMD | Right Command | KC_RIGHT_GUI |
| KC_MCTRL | Mission Control | Custom macOS shortcut |
| KC_LNPAD | Launchpad | Custom macOS shortcut |

---

## 7. Advanced Features

### 7.1 Tap Dance

**Purpose:** Single key acts differently on tap vs hold.

**Implementation:**
```c
#ifdef TAP_DANCE_ENABLE
enum { TD_ESC, TD_GRV };

// Example: ESC on tap, Ctrl+Alt+Home on hold
[T TD_ESC] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, LCTL(LALT(KC_HOME)))
```

**Processing:** QMK's tap dance system (`quantum/tap_dance.c`) tracks:
- Key press timing
- Hold vs tap detection
- Interrupts on timeout

### 7.2 Combos

**Purpose:** Multiple key presses combine to single action.

**File:** `keymaps/keychron/keymap.c`

```c
const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)}, // Select All
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)}, // Copy
    // ...
};
```

**Processing Flow:**
1. Combo detection checks if keys form matching sequence
2. If match, intercept and emit combo keycode
3. Otherwise, process normally

### 7.3 Leader Key

**Purpose:** Sequential key chords (like Vim leader).

**File:** `keymaps/keychron/keymap.c`

```c
const leader_def_t leader_default_defs[] = {
    {.sequence = {KC_V, KC_C, KC_NO, KC_NO, KC_NO}, .keycode = MC_0},
    {.sequence = {KC_V, KC_P, KC_NO, KC_NO, KC_NO}, .keycode = MC_1},
};
```

**Processing:**
```c
void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (leader_sequence_active()) {
        if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
            leader_already_matched = true;
            leader_end();
        }
    }
}
```

### 7.4 Encoder Processing

**File:** `keymaps/keychron/keymap.c`

```c
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [MAC_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [MAC_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
    [WIN_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [WIN_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
};
```

**Callback Setup** (`keychron_common.c`):
```c
void encoder_cb_init(void) {
    pin_t encoders_a_pins[] = ENCODER_A_PINS;
    pin_t encoders_b_pins[] = ENCODER_B_PINS;
    // Enable both edges, set callbacks
    palEnableLineEvent(encoders_a_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
    palEnableLineEvent(encoders_b_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
    palSetLineCallback(encoders_a_pins[i], encoder_pad_pins, (void *)i);
}
```

---

## 8. QMKata Integration

### 8.1 Overview

QMKata is a proprietary protocol for host-keyboard communication via Raw HID:

**File:** `keyboards/keychron/qmkata/QMKata.h`

```c
enum {
    QMKATA_CMD_SET      = 1,
    QMKATA_CMD_GET      = 2,
    QMKATA_CMD_ADD      = 3,
    QMKATA_CMD_DEL      = 4,
    QMKATA_CMD_PUB      = 5,
    QMKATA_CMD_SUB      = 6,
};
```

### 8.2 Key Event Reporting

```c
QMKATA_ID_KEYEVENT = 10  // Host can subscribe to key events
```

This allows the host to:
- Log all keypresses
- Display visual feedback
- Trigger host-side macros

---

## 9. Data Flow Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              FULL DATA FLOW                                  │
└──────────────────────────────────────────────────────────────────────────────┘

[Hardware]           [QMK Core]              [Keyboard]           [User Code]
     │                    │                       │                    │
     │ SW_Debounce()      │                       │                    │
     │ ← matrix_scan()    │                       │                    │
     │                    │                       │                    │
     │                    │ process_action()      │                    │
     │                    │ ← matrix_event_t      │                    │
     │                    │                       │                    │
     │                    │                action_t │                    │
     │                    │                       │                    │
     │                    │                 pre_process_record_user()  │
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │    process_record_user()
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │ process_record_keychron_common()
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │        register_code16()
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │              send_report()
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │        register_code()
     │                    │                       │ ←───────────────  │
     │                    │                       │                    │
     │                    │                       │               USB HID │
     │                    │                       │ ←───────────────  │
```

---

## 10. Error Handling

### 10.1 Keycode Overflow Protection

```c
// From keychron_common.h
static_assert(NEW_SAFE_RANGE <= 0x7E1F /* QK_KB_31 */, "Keycode overflow");
```

### 10.2 EEPROM Wear Leveling

```json
"eeprom": {
    "wear_leveling": {
        "driver": "embedded_flash",
        "logical_size": 3072,
        "backing_size": 6144
    }
}
```

### 10.3 Wireless Mode Fallback

```c
// From q3_max.c
#ifdef LK_WIRELESS_ENABLE
bool lpm_is_kb_idle(void) {
    return power_on_indicator_timer == 0 && !backlight_indicator_is_active();
}
#endif
```

---

## 11. Testing Considerations

### 11.1 Key Processing Tests

- Matrix scan correctness
- Debounce timing
- Layer switching
- Modifier key handling
- Encoder quadrature

### 11.2 Integration Tests

- HID report compliance
- macOS/Windows keycode translation
- Combo/Tap Dance/Leader sequences
- EEPROM persistence

---

## 12. File Summary

| File | Purpose |
|------|---------|
| `q3_max.c` | Keyboard-specific init, register_code16 override |
| `q3_max_user.c` | User init, QMKata task |
| `keychron_common.c` | OS-specific keycode processing |
| `keychron_common.h` | Custom keycode definitions |
| `ansi_encoder.c` / `iso_encoder.c` | LED configuration |
| `keymaps/keychron/keymap.c` | Keymap, combos, tap dance, leader |
| `config.h` | Build-time configuration |

---

## 13. Glossary

| Term | Definition |
|------|------------|
| HID | Human Interface Device (USB) |
| NKRO | N-Key Rollover - all keys scanned simultaneously |
| QK_MODS | QMK modifier keycode prefix |
| MO | Momentary layer switch |
| TG | Toggle layer |
| TT | Tap Toggle layer |
| MT | Modifier-Tap (hold shift, tap escape) |
| OSM | One-Shot modifier |
| EEPROM | Electrically Erasable Programmable ROM |
| MCU | Microcontroller Unit |
| DFU | Device Firmware Update |

---

*Document Version: 1.0*
*Created: 2026-04-16*
*Target: Keychron Q3 Max (ANSI/ISO Encoder variants)*