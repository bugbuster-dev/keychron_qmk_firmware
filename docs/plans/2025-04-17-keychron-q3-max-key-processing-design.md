# Key Processing Architecture Design Document
## Keychron Q3 Max Keyboard

**Document Version:** 1.0  
**Date:** 2025-04-17  
**Target Audience:** QMK developers maintaining the firmware  
**Purpose:** Reference and maintenance documentation for key processing pipeline

---

## 1. System Overview

The Keychron Q3 Max keyboard implements a sophisticated key processing pipeline that transforms physical key presses into USB HID reports. The system consists of:

- **Matrix Scanning** using HC595 shift registers for column expansion
- **Debouncing** with configurable algorithms stored in EEPROM
- **Action Processing** with support for layers, modifiers, tap dance, combos, and leader key
- **Custom Modifier Handling** for RDP compatibility via `register_code16` override
- **Integration** with RGB matrix, encoders, and wireless features

The firmware is based on QMK with custom Keychron extensions.

---

## 2. Hardware Configuration

### 2.1 Matrix Layout
- **Rows:** 6 (defined by `MATRIX_ROWS`)
- **Columns:** 17 (defined by `MATRIX_COLS`)
- **Diode Direction:** `ROW2COL` ( diodes in row-to-column configuration)
- **Total Keys:** 102 (6 × 17 matrix)

### 2.2 Pin Configuration (from `info.json`)

**Matrix Pins:**
```
Rows (outputs): C12, D2, B3, B4, B5, B6
Columns (inputs): C6, C7, C8, A14, A15, C10, C11, C13, C14, C15, C0, C1, C2, C3, A0, A1, A2
```

**Additional Hardware:**
- **Encoders:** 1 rotary encoder (B15 = pin A, B14 = pin B)
- **DIP Switch:** C9
- **RGB Matrix:** 87 LEDs (ANSI) / 88 LEDs (ISO) driven by SNLED27351 via SPI
- **Wireless:** LKBT51 module with dedicated pins for mode selection, power sense, battery indication

---

## 3. Matrix Scanning Architecture

### 3.1 Implementation

The Q3 Max uses a **custom matrix scanner** based on HC595 shift registers for column expansion, located in `keyboards/keychron/common/matrix.c`. This is a deviation from standard QMK direct-pin matrix scanning.

**Key Functions:**
- `matrix_init_kb()` - Initialize matrix hardware
- `matrix_scan_kb()` - Perform one matrix scan cycle (called from main loop)
- `matrix_get_row()` - Read raw state of a row

**Shift Register Configuration:**
- **SPI Driver:** SPID1
- **SPI Pins:** SCK=A5, MISO=A6, MOSI=A7
- **Chip Select:** B7 (SNLED27351_SDB_PIN)
- **Select Pins:** B8, B9 (for daisy-chaining)

**Scanning Process:**
1. For each row (output):
   - Set row pin LOW (select row)
   - Shift out column data to HC595 registers
   - Read column input states from shift registers
   - Set row pin HIGH (deselect row)
   - Apply delays for stable readings

2. Raw matrix data is stored in `raw_matrix[MATRIX_ROWS]` as bitfields

**Key Files:**
- `keyboards/keychron/common/matrix.c` - Main matrix scanning implementation
- `quantum/matrix.h` - Matrix API definitions
- `quantum/matrix_common.c` - Generic matrix processing wrapper

---

## 4. Debouncing System

### 4.1 Architecture

The Q3 Max uses Keychron's **custom debounce system** (`keyboards/keychron/common/debounce/`) which supports multiple debounce algorithms:

- **Symmetric debounce** (`DEBOUNCE_SYM_EQUAL_PER_KEY`)
- **Asymmetric debounce** (`DEBOUNCE_ASYM_EQUAL_PER_KEY`)
- **Per-key debounce** (`DEBOUNCE_PER_KEY`)
- **No debounce** (`DEBOUNCE_NONE`)

**Configuration:**
- Default debounce type: `DEBOUNCE_SYM_EQUAL_PER_KEY` (from `keychron_debounce.c`)
- Debounce time: 5ms (configurable via `DEBOUNCE` in `info.json`)
- Settings stored in EEPROM for persistence

**Key Functions:**
- `debounce_init(uint8_t num_rows)` - Initialize debounce state
- `debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)` - Apply debounce algorithm
- `debounce_set(uint8_t debounce_type)` - Change debounce algorithm at runtime
- `debounce_config_reset(void)` - Reset to defaults

**Debounce Storage in EEPROM:**
```c
// From econfig_debounce.h
typedef struct {
    uint8_t debounce_type;  // Debounce algorithm
} debounce_config_t;

extern debounce_config_t debounce_config;
```

**State Tracking:**
- Each key tracks its state transitions with timing
- Different algorithms handle key bounce differently
- The system supports dynamic debounce type changes

---

## 5. Key Event Processing Pipeline

### 5.1 Main Loop (quantum/matrix_common.c)

The key processing occurs in `matrix_scan_kb()` which is called repeatedly:

```c
void matrix_scan_kb(void) {
    // 1. Scan hardware matrix (keyboard-specific)
    matrix_scan();

    // 2. Debounce raw matrix into cooked matrix
    bool changed = debounce(raw_matrix, matrix, MATRIX_ROWS, &changed_mask);

    // 3. Process changed keys
    if (changed) {
        for (uint8_t i = 0; i < MATRIX_ROWS * MATRIX_COLS; i++) {
            if (changed_mask & (1UL << i)) {
                keyevent_t event = {
                    .keycode = keymap_key_to_keycode(i),
                    .pressed = !!(matrix[i / MATRIX_COLS] & (1 << (i % MATRIX_COLS))),
                };
                process_record(&event, i / MATRIX_COWS, i % MATRIX_COLS);
            }
        }
    }

    // 4. Send updates to host
    host_keyboard_update();
    host_mouse_update();
}
```

### 5.2 Action Processing (quantum/action.c)

The `process_record()` function is the core of key event handling:

**Key Responsibilities:**
- Convert keycode to action (via `key_to_action()`)
- Handle layer switching (`action_get_layer()`)
- Process modifiers (`register_mods()`, `unregister_mods()`)
- Handle tap dance (timing-based)
- Process combos
- Handle leader key sequences
- Process dynamic macros
- Execute user functions

**Processing Flow:**
1. Pre-processing (tap dance timing, combo detection)
2. Action lookup from keymap
3. Action-specific processing:
   - `ACTION_LAYER_TAP`: Layer switch on hold, keycode on tap
   - `ACTION_MODS_TAP`: Modifier on hold, keycode on tap
   - `ACTION_MODS_KEY`: Modifier + keycode simultaneously
   - `ACTION_TAP_DANCE`: Multi-tap sequences
   - `ACTION_COMBO`: Chorded keys
   - `ACTION_LEADER`: Leader key sequences
   - Standard keycodes: direct registration
4. Post-processing (key release handling)

---

## 6. Custom Override: register_code16/unregister_code16

### 6.1 Problem Addressed

Standard QMK batches all modifier bits into a single HID report. This causes issues with:
- RDP (Remote Desktop Protocol)
- Some remote desktop clients
- Shortcut detection (e.g., Ctrl+Alt+Home)

### 6.2 Solution (q3_max.c)

The Q3 Max overrides `register_code16()` and `unregister_code16()` to send each modifier as a **separate HID report** sequentially, mimicking physical keypress behavior:

```c
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        bool right = !!(code & QK_RMODS_MIN);
        if (code & QK_LCTL) register_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        uint8_t basic = code & 0xFF;
        if (basic) register_code(basic);
    } else {
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) {
            register_mods(0);
        } else {
            register_weak_mods(0);
        }
        register_code(code);
    }
}

void unregister_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        uint8_t basic = code & 0xFF;
        bool    right = !!(code & QK_RMODS_MIN);
        if (basic) unregister_code(basic);
        if (code & QK_LGUI) unregister_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        if (code & QK_LALT) unregister_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LSFT) unregister_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LCTL) unregister_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
    } else {
        unregister_code(code);
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) {
            unregister_mods(0);
        } else {
            unregister_weak_mods(0);
        }
    }
}
```

**Key Points:**
- Only affects `QK_MODS` keycodes (e.g., `QK_LCTL`, `QK_RSFT`)
- Non-QK_MODS keycodes use standard QMK behavior
- Unregister reverses the order (basic first, then modifiers)
- Ensures compatibility with RDP and remote desktop software

---

## 7. Integration with Other Features

### 7.1 RGB Matrix
- **Driver:** SNLED27351 (SPI-based LED driver)
- **LED Count:** 87 (ANSI) / 88 (ISO)
- **Indices:** Defined in `ansi_encoder/config.h` and `iso_encoder/config.h`
- **Caps Lock Indicator:** Index 50 (ANSI) / 51 (ISO)
- **Implementation:** Separate `rgb_matrix_user.c` handles per-key LED updates
- **Host Buffer:** Supports `rgb_matrix_host_buf_render()` for host-controlled lighting

### 7.2 Encoder Support
- **Mapping:** `ENCODER_MAP_KEY_DELAY` = 2ms debounce for encoder
- **Implementation:** Standard QMK encoder API with custom mapping

### 7.3 Wireless (LK_WIRELESS_ENABLE)
- **Module:** LKBT51
- **Pins:**
  - P24G mode select: A10
  - BT mode select: A9
  - Reset: C4
  - Wireless to MCU interrupt: B1
  - MCU to wireless interrupt: A4
  - USB power sense: B0
  - Battery charging: B13
  - Battery low LED: A8
- **Features:**
  - Bluetooth device switching (3 hosts)
  - Battery level indication via RGB matrix LEDs
  - LED driver reinit on transport change
  - USB connection kept in wireless mode
  - Wireless NKRO support

### 7.4 DIP Switch
- **Pin:** C9
- **Function:** Mac/Windows mode switch
- **Implementation:** `dip_switch_update_user()` sets `s_keyb_switch_macwin_mode`
- **Override:** User can override via `keyb_user_set_macwin_mode()`

### 7.5 Leader Key
- **Configuration:** `LEADER_NO_TIMEOUT`, `LEADER_TIMEOUT=500ms`, `LEADER_PER_KEY_TIMING`
- **EEPROM Support:** Dynamic leader key configuration stored in EEPROM

### 7.6 Tap Dance & Combos
- **EEPROM Support:** Both features store configuration in EEPROM
- **Dynamic:** Can be enabled/disabled at runtime
- **Initialization:** Performed in `keyboard_post_init_user()`

---

## 8. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         Hardware Interrupt/Timer             │
│                         (matrix_scan_kb called)             │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  1. MATRIX SCANNING                         │
│  - Set row LOW (HC595 shift register)                      │
│  - Shift out column data                                   │
│  - Read column input states                                │
│  - Set row HIGH                                            │
│  Result: raw_matrix[6] (bitfield)                          │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  2. DEBOUNCING                             │
│  - Input: raw_matrix[], previous cooked_matrix[]           │
│  - Algorithm: configurable (symmetric/asymmetric/per-key)  │
│  - Timing: based on DEBOUNCE setting (5ms default)        │
│  - Output: cooked_matrix[], changed_mask bitfield          │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  3. CHANGE DETECTION                       │
│  - Compare raw vs cooked                                   │
│  - Generate changed_mask (bit per key)                     │
│  - For each changed key:                                   │
│    • Calculate key index (row * COLS + col)                │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  4. ACTION PROCESSING                      │
│  process_record(record, row, col)                          │
│    ├─ Pre-process (tap dance, combo detection)            │
│    ├─ Lookup action from keymap                            │
│    ├─ Handle layer switches                                │
│    ├─ Process modifiers (register/unregister)             │
│    ├─ Handle tap dance states                              │
│    ├─ Check combos                                         │
│    ├─ Process leader key sequences                         │
│    ├─ Execute user function                                │
│    └─ Post-process (key release handling)                 │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  5. HID REPORT GENERATION                  │
│  - host_keyboard_update() - send keyboard report           │
│  - host_mouse_update() - send mouse report (if any)        │
│  - Custom register_code16() ensures sequential modifier    │
│    reports for RDP compatibility                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 9. State Management

### 9.1 Matrix State
- `raw_matrix[MATRIX_ROWS]` - Raw, undebounced matrix state
- `matrix[MATRIX_ROWS]` - Cooked, debounced matrix state (from `matrix_common.c`)
- Both are `matrix_row_t` type (uint8/16/32 based on column count)

### 9.2 Debounce State
- Per-key debounce timers and states (in `keychron_debounce.c`)
- Configurable debounce algorithm
- Stored in EEPROM for persistence

### 9.3 Layer State
- `default_layer_state` - Base layer (set by DIP switch or user function)
- `layer_state` - Current active layers (bitfield)
- Layer switching via `default_layer_set()`, `layer_on()`, `layer_off()`

### 9.4 Modifier State
- Tracked by QMK core in `mods` variable
- `register_mods()`, `unregister_mods()` update modifier state
- `register_weak_mods()` for one-shot modifiers

### 9.5 Tap Dance State
- Per-key tap dance state machines
- `tap_state` tracks tap count and timing
- Configurable timeout and keycodes per tap count

### 9.6 Combo State
- `combo_state` tracks currently active combos
- `COMBO_ENABLE` requires all combo keys pressed within `COMBO_TERM`

### 9.7 Leader Key State
- `leader_key.sequence_index` tracks position in leader sequence
- `leader_key.sequence` stores pressed keys
- `LEADER_TIMEOUT` defines max time between key presses

---

## 10. Performance Considerations

### 10.1 Matrix Scan Rate
- Default QMK: ~1000Hz (1ms interval)
- Can be adjusted via `MATRIX_SCAN_DELAY` or `MATRIX_IO_DELAY`
- Q3 Max uses custom delays for HC595 shift register timing

### 10.2 Debounce Timing
- Default: 5ms (configurable)
- Must be longer than mechanical bounce (typically 5-20ms)
- Per-key debounce allows different timing per key

### 10.3 Action Processing
- `process_record()` should be fast (no blocking delays)
- Long operations should use deferred execution
- Tap dance timing uses `timer_elapsed32()` for non-blocking checks

### 10.4 USB Reporting
- Keyboard reports sent on every key state change
- Mouse reports sent when mouse keys or encoder used
- Wireless mode may batch reports for power efficiency

---

## 11. Testing Strategies

### 11.1 Unit Tests
- Matrix scanning logic (hardware-independent)
- Debounce algorithms (test with simulated bounce)
- Action lookup and processing
- Keycode translation

### 11.2 Integration Tests
- Full key press/release cycle
- Modifier combinations
- Layer switching
- Tap dance sequences
- Combo detection
- Leader key sequences

### 11.3 Hardware Tests
- Physical key matrix continuity
- HC595 shift register communication
- SPI communication with SNLED27351
- Encoder quadrature decoding
- Wireless module communication

### 11.4 Performance Tests
- Matrix scan rate measurement
- Debounce accuracy
- USB report latency
- CPU usage profiling

---

## 12. Configuration and Customization

### 12.1 Keymap Configuration
- Located in `keyboards/keychron/q3_max/[ansi_encoder|iso_encoder]/keymaps/`
- 3D layers (0-2) by default, configurable
- Keycodes defined in `quantum_keycodes.h`
- Supports QK_MODS for modifier combinations

### 12.2 Feature Flags (rules.mk)
```
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
LEADER_ENABLE = yes
```

### 12.3 Customization Points
- `keymap.c` - Key assignments per layer
- `keychron_common_init()` in `keychron_common.c` - Hardware initialization
- `keychron_task_user()` - Periodic tasks (battery, RGB, etc.)
- `rgb_matrix_user.c` - LED effects and indicators
- Override functions in `q3_max_user.c`

---

## 13. Key Files Reference

| File | Purpose |
|------|---------|
| `keyboards/keychron/q3_max/q3_max.c` | Keyboard init, tasks, custom register_code16 overrides |
| `keyboards/keychron/q3_max/q3_max_user.c` | User code: EEPROM init, RGB host buffer, Mac/Win mode |
| `keyboards/keychron/common/matrix.c` | HC595-based matrix scanning |
| `keyboards/keychron/common/debounce/keychron_debounce.c` | Configurable debounce system |
| `quantum/matrix.c` | Generic matrix scanning wrapper |
| `quantum/matrix_common.c` | Main matrix processing loop |
| `quantum/action.c` | Action processing and key event handling |
| `quantum/debounce.h` | Debounce API |
| `quantum/matrix.h` | Matrix API |
| `quantum/quantum.h` | Main QMK include |

---

## 14. Conclusion

The Keychron Q3 Max key processing architecture demonstrates:
- Custom matrix scanning for hardware constraints (HC595 shift registers)
- Flexible debouncing with runtime configuration
- Full QMK feature support (layers, tap dance, combos, leader key)
- Custom modifier handling for enterprise compatibility (RDP)
- Integration with advanced features (RGB matrix, wireless, encoders)

The design maintains compatibility with QMK's standard APIs while allowing keyboard-specific customizations where needed.
