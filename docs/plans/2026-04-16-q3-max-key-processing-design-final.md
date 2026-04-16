# Key Processing Design Document: Keychron Q3 Max

**Document Version:** 1.0
**Date:** 2026-04-16
**Target Keyboard:** Keychron Q3 Max (ANSI Encoder variant)
**Firmware Base:** QMK Firmware (Keychron fork)
**MCU:** STM32F401 (ARM Cortex-M4, 84 MHz)
**Platform:** ChibiOS

---

## 1. Overview

This document describes the complete key processing pipeline in the QMK firmware running on the Keychron Q3 Max keyboard. It covers every stage from hardware matrix scanning through debouncing, action resolution, layer switching, custom keycode processing, and HID report generation — with specific notes on Q3 Max customizations at each stage.

### 1.1 Hardware Specification

| Component | Details |
|-----------|---------|
| MCU | STM32F401 (ARM Cortex-M4 @ 84 MHz) |
| Bootloader | stm32-dfu |
| Matrix | 6 rows x 17 columns (102 key positions, 87/88 physical keys) |
| Diode Direction | ROW2COL (rows driven LOW, columns read with pull-ups) |
| Encoder | 1 rotary encoder (pins B15/B14) |
| DIP Switch | 1-position (C9) — toggles Mac/Win default layer |
| RGB LED Driver | SNLED27351 (SPI interface, dual chips) |
| RGB LED Count | 87 (ANSI) / 88 (ISO) |
| USB VID:PID | 0x3434:0x0830 (ANSI) / 0x3434:0x0831 (ISO) |
| Wireless | BT51 module (3 hosts) + 2.4G P24G dongle |

### 1.2 Processing Pipeline Summary

```
[Hardware Matrix] → [Direct GPIO Scan] → [Custom Debounce] → [Key Event]
       ↓
[Action Layer + Tap-Detect] → [Layer Resolution] → [Record Chain]
       ↓
[Process Action / Register Code] → [HID Report] → [USB/BT Transmit]
```

### 1.3 File Structure

```
keyboards/keychron/q3_max/
├── q3_max.c                      # Keyboard-level hooks: dip switch, power-on LED, register_code16 override
├── q3_max_user.c                 # User-level init: QMKata, EEPROM sync, RGB host buffer
├── config.h                      # Build config: encoder pins, SPI, BT, NKRO, features
├── mcuconf.h                     # STM32 clock config (HSE 16MHz, PLL 96MHz)
├── halconf.h                     # ChibiOS HAL config (SPI, RTC, PAL callbacks)
├── board.h                       # GPIO pin initialization (all pins, pull-ups, modes)
├── rules.mk                      # Build rules: TAP_DANCE, COMBO, LEADER, wireless, QMKata
├── ansi_encoder/                 # ANSI layout variant
│   ├── keyboard.json             # Physical layout (LAYOUT_tkl_ansi), LED positions
│   ├── config.h                  # SNLED27351 current, RGB_MATRIX_LED_COUNT=87
│   ├── ansi_encoder.c            # LED-to-matrix mapping for SNLED27351
│   └── keymaps/keychron/keymap.c # 4-layer keymap, combos, tap dance, leader, encoder map
├── iso_encoder/                  # ISO layout variant (88 LEDs, ISO Enter)
│   └── ... (same structure as ANSI)
├── via_json/                     # VIA layout definitions (custom keycodes, menus)
├── dynld_func.h                  # Dynamic LED animation function pointers
├── qmkata_rgb_matrix_user.c      # QMKata dynamically loaded RGB animation runner
├── qmkata_sysex_handler.c        # QMKata SysEx protocol (991 lines: CLI, EEPROM, RGB, status)
├── rgb_matrix_user.inc           # SNLED27351 LED positions, custom effect declaration
└── debug_user.c/h                # Debug configuration macros
```

```
keyboards/keychron/common/
├── keychron.h                    # Main include header
├── keychron_common.c/h           # OS keycode mapping, Siri, key combos, OS toggle/select
├── keychron_task.c/h             # Record chain, RGB/LED indicator chain, housekeeping task
├── keychron_raw_hid.c/h          # Raw HID communication for QMKata
├── matrix.c                      # Matrix scanning (direct GPIO, no HC595)
├── debounce/
│   ├── keychron_debounce.c/h     # Dynamic debounce type switching via EEPROM/Raw HID
│   └── [standard debounce algos] # sym_defer_g, sym_defer_pk, etc.
├── tap_dance/tap_dance_eeprom.c  # Dynamic tap dance EEPROM persistence
├── combo/combo_eeprom.c          # Dynamic combo EEPROM persistence
├── leader/leader_eeprom.c        # Dynamic leader EEPROM persistence
├── eeconfig_kb.c/h               # EEPROM layout and management
├── nkro.c                        # NKRO implementation
├── state_notify.c                # State notification system
├── rgb/keychron_rgb.c            # RGB matrix integration
├── backlit_indicator.c/h         # LED indicator patterns (OS mode, etc.)
├── wireless/                     # Wireless support (BT51, 2.4G, LPM)
│   ├── wireless.c/h
│   └── lpm_stm32f401.c/h
└── factory_test.h                # Factory test support
```

---

## 2. Complete Data Flow

### 2.1 The Main Loop

All key processing is driven by the `keyboard_task()` loop in `quantum/keyboard.c`. Each iteration performs:

```c
void keyboard_task(void) {
    // 1. Matrix scanning → key event generation
    matrix_task();

    // 2. Quantum subsystem tasks (tap dance, combos, leader, etc.)
    quantum_task();

    // 3. Encoder task (queued events from ISR)
    encoder_task();

    // 4. Pointing device task
    pointing_device_task();

    // 5. Keychron-specific periodic tasks
    housekeeping_task_kb();  // → keychron_task()
}
```

### 2.2 Key Event → HID Report Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        KEY PROCESSING PIPELINE                              │
│                                                                             │
│  [matrix_scan_custom()]              ← GPIO read, 17 cols x 6 rows         │
│  └─→ raw_matrix[]                    ← Undebounced column states            │
│      └─→ debounce()                  ← Custom (50ms, EEPROM configurable)   │
│          └─→ matrix[]                ← Cooked/debounced states              │
│              └─→ matrix_task()       ← Diff with previous, generate events  │
│                  └─→ FOR each changed bit:                                 │
│                      → MAKE_KEYEVENT(row, col, pressed, time)               │
│                      → action_exec(event)                                   │
│                        ├─→ action_tapping_process()    ← Tap-hold detection │
│                        └─→ process_record(record)                              │
│                            │                                                  │
│                            │  ┌─ QUANTUM HOOK CHAIN                         │
│                            │  │                                              │
│                            │  │  process_record_quantum()    ← quantum layer │
│                            │  │       ↓ (false = short-circuit)              │
│                            │  │                                              │
│                            │  ┌─ KEYCHAIN HOOK CHAIN (keychron_task.c:145)   │
│                            │  │                                              │
│                            │  │  process_record_kb()                         │
│                            │  │    ├─→ process_record_user()   ← user keymap │
│                            │  │    └─→ process_record_keychron()             │
│                            │  │         │                                     │
│                            │  │         │  ┌─ Standard keychron chain         │
│                            │  │         │  │                                  │
│                            │  │         │  │  process_record_keychron_common()← OS keycodes, Siri  │
│                            │  │         │  │  process_record_profile()       ← analog matrix       │
│                            │  │         │  │  process_record_wireless()      ← BT/2.4G keycodes    │
│                            │  │         │  │  process_record_report_rate()   ← USB rate keycodes   │
│                            │  │         │  │  process_record_factory_test()  ← factory test        │
│                            │  │         │  │  process_record_snap_click()    ← snap click          │
│                            │  │         │  │  process_record_keychron_kb()   ← KB-specific (weak)  │
│                            │  │         │  │  process_record_retail_demo()   ← retail demo         │
│                            │  │         │  │  process_record_keychron_rgb()  ← RGB macros          │
│                            │  │         │  │                                  │
│                            │  │         │  └─ each returns true→continue, false→stop           │
│                            │  │                                              │
│                            │  └─→ process_record_handler()       ← action resolution │
│                            │       └─→ process_action(record, action) ← dispatch     │
│                            │             │                                     │
│                            │             │  ACT_LMODS/ACT_RMODS     → register_code()│
│                            │             │  ACT_LAYER_TAP           → layer_on/off() │
│                            │             │  ACT_USAGE               → host_system_send()│
│                            │             │  ACT_CONSUMER            → host_consumer_send()│
│                            │             │  ACT_MOUSEKEY            → register_mouse()│
│                            │             │  ACT_LAYER               → layer ops      │
│                            │             │  ACT_SWAP_HANDS          → swap hands     │
│                            │             │                                     │
│                            │             └─→ post_process_record_quantum()  ← quantum post-hook│
│                            │                                                   │
│                            └─→ report building & sending                        │
│                                ├─→ register_code16()/unregister_code16()         │
│                                │   (Q3 Max override: sequential modifier reports)│
│                                ├─→ add_key()/del_key()                         │
│                                ├─→ send_keyboard_report() / send_nkro_report()  │
│                                └─→ host_keyboard_send(report)                  │
│                                    └─→ USB or Bluetooth driver                │
│                                                                             │
│  [Periodic Tasks: quantum_task() + keychron_task()]                          │
│    ├─→ tap_dance_task()         ← finalize dances, timeout taps              │
│    ├─→ combo_task()             ← finalize combos, timeout terms             │
│    ├─→ leader_task()            ← finalize leader sequences                  │
│    ├─→ wireless_tasks()         ← BT/2.4G mode switching                    │
│    ├─→ keychron_common_task()   ← Siri timeout, OS select, WinLock          │
│    ├─→ factory_test_task()      ← factory test mode                         │
│    ├─→ keychron_task_kb()       ← Q3 Max: power-on LED                      │
│    └─→ backlight_indicator()    ← LED indicator patterns                    │
│                                                                             │
│  [RGB Matrix Indicators: rgb_matrix_indicators_kb()]                         │
│    ├─→ rgb_matrix_indicators_user()   ← user overrides (leader LED)         │
│    └─→ rgb_matrix_indicators_keychron()                                  │
│         ├─→ os_state_indicate()         ← Mac/Win mode indicator            │
│         ├─→ rgb_matrix_indicators_bt()    ← BT host indicator               │
│         ├─→ analog_matrix_indicator()     ← profile indicator                │
│         ├─→ factory_test_indicator()      ← factory test indicator           │
│         ├─→ backlit_indicator()           ← backlight indicator patterns     │
│         └─→ rgb_matrix_host_buf_render()  ← QMKata host buffer synchronization│
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 The Record Hook Chain (Critical for Customization)

The processing chain is a **filter pipeline** — each stage can return `false` to short-circuit:

```
                    action_exec(keyevent)
                         │
                    process_record(record)     ← quantum/action.c:283
                         │
                    process_record_quantum()   ← weak hook, quantum layer
                         │ false → STOP
                    process_record_handler()   ← resolves action
                         │
                    process_action()           ← dispatch by action type
                         │
                    post_process_record_quantum() ← weak post-hook
                         │
                    ← back to action_exec → register_code() / send_report()
```

**Keyboard-level hook chain** (`keychron_task.c:145`):

```
process_record_kb()
  │
  ├─→ process_record_user()         ← user keymap (keymap.c)
  │    │ false → STOP
  │    │ true → continue
  │    │
  │    └─ Q3 Max user implementation (keymap.c:94):
  │        • Leader LED tracking (RGB)
  │        • Combo→leader interception (record->keycode == QK_LEADER)
  │        • process_record_keychron_common() call
  │
  └─→ process_record_keychron()     ← keychron_task.c:36
       │ false → STOP
       │
       ├─→ process_record_keychron_common()   ← keychron_common.c:157
       │    • KC_MCTRL, KC_LNPAD (Mission Control, Launchpad)
       │    • KC_LOPTN/ROPTN/LCMMD/RCMMD (OS key translation)
       │    • KC_SIRI (Siri activation)
       │    • KC_TASK/FILE/SNAP/CTANA/WLCK/MLCK (key combos)
       │    • BL_SPI/BL_SPD (LED speed)
       │    • GU_TOGG (GUI toggle)
       │    • OS_TOGGL (OS toggle)
       │    • OS_WIN/OS_MAC (OS select combo)
       │
       ├─→ process_record_profile()      ← analog matrix profiles
       ├─→ process_record_wireless()     ← wireless keycodes (BT_HST1/2/3, P2P4G)
       ├─→ process_record_report_rate()  ← USB report rate
       ├─→ process_record_factory_test() ← factory test
       ├─→ process_record_snap_click()   ← snap click
       ├─→ process_record_keychron_kb()  ← KB-specific (weak, Q3 Max can override)
       ├─→ process_record_retail_demo()  ← retail demo
       └─→ process_record_keychron_rgb() ← RGB macros
```

---

## 3. Matrix Scanning

### 3.1 Hardware Configuration

**File:** `keyboards/keychron/q3_max/info.json:23-26`

```json
"matrix_pins": {
    "cols": ["C6", "C7", "C8", "A14", "A15", "C10", "C11", "C13", "C14", "C15", "C0", "C1", "C2", "C3", "A0", "A1", "A2"],
    "rows": ["C12", "D2", "B3", "B4", "B5", "B6"]
}
```

**Diode Direction:** ROW2COL — rows are driven LOW (outputs), columns are read with pull-ups (inputs).

### 3.2 Scanning Algorithm

The Q3 Max uses **direct GPIO matrix scanning** (no HC595 shift registers). Each scan iteration:

1. For each of the 17 columns:
   - Drive the column pin LOW (sink current)
   - Read all 6 row pins
   - If a row pin reads LOW, the key at (row, col) is pressed
   - Store in `current_matrix[row]` as a bitmask
   - Release the column (set HIGH/input)
2. Compare with previous matrix state using `memcmp`
3. If changed, copy to `raw_matrix[]` and trigger debounce

**Pin timing:**
- Column settle time after driving LOW: ~200 cycles (~2 μs at 84 MHz)
- Row float time after releasing column: ~200 cycles
- Total per scan: 17 cols × (settle + read + float) ≈ negligible (< 100 μs)

### 3.3 Column Pin Mapping

The 17 columns map to physical key positions as defined by the `LAYOUT_tkl_ansi` macro in `ansi_encoder/keyboard.json`. The column order follows the physical layout from left to right across each row.

---

## 4. Debounce System

### 4.1 Configuration

**File:** `keyboards/keychron/q3_max/info.json:74-76`

```json
"build": {
    "debounce_type": "custom"
},
"debounce": 50
```

The Q3 Max uses the **Keychron custom debounce system** (`keyboards/keychron/common/debounce/`) which provides:

- **Default algorithm:** `DEBOUNCE_SYM_EAGER_PER_KEY` (per-key, both press and release eager)
- **Default debounce time:** 50ms (configurable 1-255ms)
- **Storage:** EEPROM (dynamic, changeable at runtime)
- **Change method:** Via Raw HID commands or QMKata protocol

### 4.2 Available Algorithms

| Algorithm | Press Behavior | Release Behavior | Per-Key Timer | Memory |
|-----------|---------------|------------------|---------------|--------|
| `SYM_DEFER_GLOBAL` | Wait debounce time | Wait debounce time | Shared | ~2 B |
| `SYM_DEFER_PER_ROW` | Wait debounce time | Wait debounce time | Per row (6) | ~12 B |
| `SYM_DEFER_PER_KEY` | Wait debounce time | Wait debounce time | Per key (102) | ~408 B |
| `SYM_EAGER_PER_ROW` | Immediate | Wait debounce time | Per row (6) | ~12 B |
| `SYM_EAGER_PER_KEY` | Immediate | Wait debounce time | Per key (102) | ~408 B |
| `ASYM_EAGER_DEFER_PER_KEY` | Immediate | Wait 2× debounce | Per key (102) | ~408 B |
| `NONE` | Immediate | Immediate | None | 0 B |

**Q3 Max default:** `SYM_EAGER_PER_KEY` with 50ms debounce. This provides near-instant press detection (immediate on press) but waits 50ms for stable release, which is appropriate for mechanical switches that typically bounce more on release.

### 4.3 Custom Debounce Interface

**File:** `keyboards/keychron/common/debounce/keychron_debounce.c`

```c
// Dynamic debounce selection via EEPROM
void debounce_set(uint8_t new_type, uint8_t time, bool force) {
    // Validate, free old algorithm, load new one
    debounce_free();
    switch (new_type) {
        case DEBOUNCE_SYM_EAGER_PER_KEY:
            debounce_func = sym_eager_pk_debounce;
            break;
        // ... other algorithms
    }
    debounce_func.init(MATRIX_ROWS);
}

// Called from matrix_scan() each iteration
bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed);
```

---

## 5. Key Event Generation

### 5.1 Event Types

**File:** `quantum/keyboard.c` (around line 586)

```c
typedef enum {
    TICK_EVENT = 0,         // Internal timer tick
    KEY_EVENT = 1,          // Regular key press/release
    ENCODER_CW_EVENT = 2,   // Encoder clockwise rotation
    ENCODER_CCW_EVENT = 3,  // Encoder counter-clockwise rotation
    COMBO_EVENT = 4,        // Combo fired
    DIP_SWITCH_ON_EVENT = 5,
    DIP_SWITCH_OFF_EVENT = 6,
} keyevent_type_t;

typedef struct {
    keypos_t        key;           // {row, col} physical position
    uint16_t        time;          // timer_read() timestamp
    keyevent_type_t type;
    bool            pressed;
} keyevent_t;
```

### 5.2 Event Pipeline

```c
static bool matrix_task(void) {
    matrix_scan();  // Updates raw_matrix[] and matrix[]

    // Detect changes
    for each row:
        changed = matrix[row] XOR previous_matrix[row];
        for each column in changed:
            bool pressed = matrix[row] & (1 << col);
            action_exec(MAKE_KEYEVENT(row, col, pressed, timer_read()));
            switch_events(row, col, pressed);  // RGB/LED matrix hooks

    return changed;
}
```

### 5.3 Encoder Events

**File:** `keyboards/keychron/common/keychron_common.c:341-362`

Encoder events come from **ChibiOS PAL callbacks** (not polling):

```c
// ISR callback registered on encoder A/B pins
static void encoder_pad_pins(void *param) {
    uint8_t index = (uint32_t)param;
    encoder_quadrature_handle_inerrupt_read(index);
}

void encoder_cb_init(void) {
    for (i = 0; i < NUM_ENCODERS; i++) {
        palEnableLineEvent(encoders_a_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
        palEnableLineEvent(encoders_b_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
        palSetLineCallback(encoders_a_pins[i], encoder_pad_pins, (void *)i);
        palSetLineCallback(encoders_b_pins[i], encoder_pad_pins, (void *)i);
    }
}
```

The ISR queues the rotation event, which is later processed by `encoder_task()` in the main loop:

```c
// Main loop: encoder_task()
bool encoder_task(void) {
    while (encoder_dequeue_event(&index, &clockwise)) {
        action_exec(MAKE_ENCODER_CW_EVENT(index, clockwise, timer_read()));
    }
    return true;
}
```

### 5.4 DIP Switch Events

**File:** `keyboards/keychron/q3_max/q3_max.c:24-33`

```c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (dip_switch_update_user(index, active)) return true;

    if (index == 0) {
        // Dip switch 0: active=1 → layer 2 (WIN_BASE), active=0 → layer 0 (MAC_BASE)
        default_layer_set(1UL << (active ? 2 : 0));
    }
    return true;
}
```

---

## 6. Action Resolution and Tap-Detect

### 6.1 Tap-Hold State Machine

**File:** `quantum/action_tapping.c:131`

When a key is pressed, `action_exec()` routes through `action_tapping_process()`:

```
State: IDLE (no pending tap-hold key)
  On press of MOD_TAP or LAYER_TAP key:
    → State: PRESSED
    → Store tapping_key = {pressed_key, time, keycode}
    → Queue current event in waiting_buffer[]
    → Wait for resolution:

State: PRESSED (waiting for tap/hold determination)
  On release of tapping_key (within TAPPING_TERM):
    → TAP detected: set tapping_key.tap.count=1
    → State: RELEASED
    → Process queued events, emit tap action

  On press of OTHER key (within TAPPING_TERM):
    → HOLD detected (or TAP, depending on HOLD_ON_OTHER_KEY_PRESS)
    → State: RELEASED
    → Process queued events, emit hold/tap action

  On TAPPING_TERM timeout:
    → HOLD detected
    → Process all queued events, emit hold action

State: RELEASED
  → Emit any remaining buffered events
  → State: IDLE
```

### 6.2 Action Resolution

**File:** `quantum/action_layer.c:342`

```c
uint8_t layer_switch_get_layer(keypos_t key) {
    // Combine default + active layers
    layer_state_t layers = layer_state | default_layer_state;

    // Scan from highest layer down to 0
    for (layer = MAX_LAYER-1; layer >= 0; layer--) {
        if (layers & (1UL << layer)) {
            action_t action = action_for_key(layer, key);
            if (action.value != ACTION_TRANSPARENT) {
                return layer;
            }
        }
    }
    return 0;  // Fallback to layer 0
}
```

**Action Caching** (`store_or_get_action()`): On press, the resolved layer is cached per key position. On release, the cached layer is used to ensure the release action matches the press action (critical for tap-hold keys).

### 6.3 Action Dispatch

**File:** `quantum/action.c:382`

```c
void process_action(keyrecord_t *record, action_t action) {
    switch (action.value) {
        case ACT_LMODS:           // Left modifiers + basic key
        case ACT_RMODS:           // Right modifiers + basic key
            register_code();
            break;

        case ACT_LMODS_TAP:       // Mod-tap (MOD on hold, KC on tap)
            action_tapping_handle();
            break;

        case ACT_LAYER_TAP:       // Layer-tap (layer on hold, KC on tap)
        case ACT_LAYER_TAP_EXT:   // Layer-tap with subtype
            layer_tap_action();
            break;

        case ACT_USAGE:           // System key (power, sleep, etc.)
            host_system_send(code);
            break;

        case ACT_CONSUMER:        // Consumer key (volume, play, etc.)
            host_consumer_send(code);
            break;

        case ACT_MOUSEKEY:        // Mouse key (move, click, scroll)
            register_mouse(code, pressed);
            break;

        case ACT_LAYER:           // Momentary/toggle/tap-toggle layer
            layer_on(layer); / layer_off(layer);
            break;

        case ACT_SWAP_HANDS:      // Hand swap
            swap_hands_action();
            break;

        default:                  // Standard keycode
            register_code(code);
            break;
    }
}
```

---

## 7. Keychron Custom Processing

### 7.1 OS Keycode Translation

**File:** `keyboards/keychron/common/keychron_common.c:72-77, 177-186`

The Keychron firmware provides OS-specific modifier keycodes that map to the correct HID codes:

```c
static uint8_t mac_keycode[4] = { KC_LOPT, KC_ROPT, KC_LCMD, KC_RCMD };

// In process_record_keychron_common():
case KC_LOPTN:   // Left Option
case KC_ROPTN:   // Right Option
case KC_LCMMD:   // Left Command
case KC_RCMMD:   // Right Command
    register_code(mac_keycode[keycode - KC_LOPTN]);
    return false;  // Short-circuit
```

### 7.2 Siri Key

**File:** `keyboards/keychron/common/keychron_common.c:187-199`

```c
case KC_SIRI:
    if (pressed) {
        if (!is_siri_active) {
            is_siri_active = true;
            register_code(KC_LCMD);
            register_code(KC_SPACE);
        }
        siri_timer = timer_read32();  // Reset timeout on each press
    }
    return false;
```

**Timeout handling** (`keychron_common_task()`, line 291):
```c
if (is_siri_active && timer_elapsed32(siri_timer) > 500) {
    unregister_code(KC_LCMD);
    unregister_code(KC_SPACE);
    is_siri_active = false;
    siri_timer = 0;
}
```

### 7.3 OS Key Combinations (KC_TASK, KC_FILE, etc.)

**File:** `keyboards/keychron/common/keychron_common.c:80-92, 201-224`

```c
static key_combination_t key_comb_list[] = {
    {2, {KC_LWIN, KC_TAB}},            // KC_TASK: Task View (Win)
    {2, {KC_LWIN, KC_E}},              // KC_FILE: File Explorer (Win)
    {3, {KC_LSFT, KC_LCMD, KC_4}},     // KC_SNAP: Screenshot (Mac)
    {2, {KC_LWIN, KC_C}},              // KC_CTANA: Cortana (Win)
    {2, {KC_LWIN, KC_L}},              // KC_WLCK: Lock Screen (Win)
    {3, {KC_LCTL, KC_LCMD, KC_Q}},     // KC_MLCK: Lock Screen (Mac)
};

// Each sends multiple HID codes at once on press, and all on release
```

### 7.4 OS Toggle and Select

**OS Toggle** (`os_toggle()`, line 131): Toggles between MAC_BASE (layer 0) and WIN_BASE (layer 2) by XORing the default layer state.

**OS Select** (line 268): Hold OS_WIN or OS_MAC for 3 seconds to switch the default layer.

### 7.5 Keychron Task Chain

**File:** `keyboards/keychron/common/keychron_task.c:128-143`

```c
void keychron_task(void) {
    wireless_tasks();                    // BT/2.4G mode switching
    factory_test_task();                 // Factory test
    retail_demo_task();                  // Retail demo
    keychron_common_task();              // Siri timeout, WinLock, OS select
    keychron_task_kb();                  // Q3 Max: power-on LED
}
```

---

## 8. HID Report Generation

### 8.1 Q3 Max Modifier Override

**File:** `keyboards/keychron/q3_max/q3_max.c:77-113`

The Q3 Max **overrides** `register_code16()` and `unregister_code16()` to send modifiers **sequentially** instead of batched:

```c
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // Each modifier gets its own HID report
        if (code & QK_LCTL) register_code(KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(KC_LEFT_GUI);
        register_code(code & 0xFF);  // Basic keycode
    } else {
        register_code(code);
    }
}
```

**Why:** RDP and some remote desktop clients expect modifiers to arrive sequentially (like physical keypresses), not batched. The default QMK behavior sends all modifier bits in a single report, which causes RDP to miss shortcut detection (e.g., Ctrl+Alt+Home for the RDP connection bar).

### 8.2 Report Types

**6KRO Report** (8 bytes, standard boot protocol):
```c
typedef struct {
    uint8_t report_id;
    uint8_t mods;      // GASCGASC: LCtrl, LShift, LAlt, LGUI, RCtrl, RShift, RAlt, RGUI
    uint8_t reserved;
    uint8_t keys[6];   // Up to 6 simultaneous keys
} report_keyboard_t;
```

**NKRO Report** (16 bytes, N-Key Rollover):
```c
typedef struct {
    uint8_t report_id;
    uint8_t mods;
    uint8_t bits[30];  // 30 key bits (keycode 0-29 per report block)
} report_nkro_t;
```

Q3 Max enables NKRO via `NKRO_ENABLE = yes` in `config.h`. The report type is selected at runtime based on host capability (`host_can_send_nkro()`) and `keymap_config.nkro`.

### 8.3 Report Sending

```c
void send_keyboard_report(void) {
    keyboard_report->mods = get_mods_for_report();  // real_mods | weak_mods | oneshot_mods

    // Only send if changed from last report (avoids redundant reports)
    if (memcmp(keyboard_report, &last_report, sizeof(report_keyboard_t)) != 0) {
        host_keyboard_send(keyboard_report);
    }
}
```

---

## 9. Q3 Max Layer System

### 9.1 Layer Map

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c:34-39`

| Layer | Name | Active When |
|-------|------|-------------|
| 0 | MAC_BASE | Default (dip switch OFF) |
| 1 | MAC_FN | FN key held (MO(MAC_FN)) |
| 2 | WIN_BASE | Default (dip switch ON) |
| 3 | WIN_FN | FN key held (MO(WIN_FN)) |

### 9.2 Default Layer Selection

The DIP switch (pin C9) determines the default layer:

```
DIP OFF → default_layer_set(1UL << 0) → MAC_BASE active
DIP ON  → default_layer_set(1UL << 2) → WIN_BASE active
```

The FN keys use `MO(MAC_FN)` / `MO(WIN_FN)` which are **momentary** — they activate the FN layer only while held and deactivate on release.

### 9.3 Layer State Storage

```c
layer_state_t default_layer_state;  // Stored in EEPROM, set by DIP switch
layer_state_t layer_state;           // Volatile, set by MO/TG/TT keys
```

The effective layer for any key is the **highest-numbered active layer** (bitwise OR of both states) that contains a non-transparent action at that key position.

---

## 10. Advanced Features

### 10.1 Tap Dance

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c:20-32, 152-171`

```c
#ifdef TAP_DANCE_ENABLE
enum { TD_ESC, TD_GRV };

// Dynamic (EEPROM-stored) definitions
#ifdef DYNAMIC_TAP_DANCE_ENABLE
#include "tap_dance_eeprom.h"
tap_dance_action_t tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS];
#else
// Static definitions
tap_dance_action_t tap_dance_actions[] = {
    [TD_ESC] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, LCTL(LALT(KC_HOME))),
    [TD_GRV] = 0,
};
#endif
```

**Processing flow:**
1. `preprocess_tap_dance()` intercepts tap dance keycodes before action resolution
2. Tracks tap count, timeout, and interrupts
3. `tap_dance_task()` (called from `quantum_task()`) finalizes dances on timeout
4. On finish, calls `on_dance_finished()` callback to emit the resolved keycode

### 10.2 Combos

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c:120-147`

```c
#ifdef DYNAMIC_COMBO_ENABLE
combo_t key_combos[COMBO_DEF_MAX_SLOTS] = {};

const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)}, // Select All
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)}, // Copy
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)}, // Paste
    {.keys = {KC_V, KC_F, COMBO_END}, .keycode = LCTL(KC_X)}, // Cut
    {.keys = {KC_X, KC_D, COMBO_END}, .keycode = LCTL(KC_Z)}, // Undo
};
```

**Processing flow:**
1. `process_combo()` intercepts keypresses that match combo key sequences
2. When all keys in a combo are pressed simultaneously, a `COMBO_EVENT` is queued
3. `combo_task()` (called from `quantum_task()`) handles COMBO_TERM timeout
4. On timeout, the combo keycode is emitted via `action_exec()`

**Key observation:** Combo detection runs **before** tap-hold resolution. If a key is part of a combo, it cannot simultaneously be used for tap-hold detection.

### 10.3 Leader Key

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c:176-226`

```c
#ifdef DYNAMIC_LEADER_ENABLE
#include "leader_eeprom.h"
#include "leader.h"

const leader_def_t leader_default_defs[] = {
    {.sequence = {KC_V, KC_C, KC_NO, KC_NO, KC_NO}, .keycode = MC_0},
    {.sequence = {KC_V, KC_P, KC_NO, KC_NO, KC_NO}, .keycode = MC_1},
};

// Early termination optimization
void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    // Check for exact match
    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
        leader_already_matched = true;
        leader_end();  // Immediately fire and stop
        return;
    }

    // No prefix matches remain — end early
    if (!leader_eeprom_has_prefix(leader_sequence, leader_sequence_size)) {
        leader_end();
    }
}
```

**Processing flow:**
1. Combo that outputs `QK_LEADER` triggers `leader_start()` in `process_record_user()`
2. Each subsequent keypress is added to `leader_sequence[]` by `preprocess_record_quantum()`
3. `post_process_record_user()` checks for early termination (exact match or no prefix)
4. `leader_end_user()` fires as a final match attempt on timeout
5. RGB matrix indicator lights the trigger key during the sequence

### 10.4 Encoder Mapping

**File:** `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c:80-87`

```c
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [MAC_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [MAC_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
    [WIN_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [WIN_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
};
```

The encoder map is resolved per-layer. CW (clockwise) and CCW (counter-clockwise) map to different keycodes depending on the active layer.

### 10.5 QMKata Protocol

**File:** `keyboards/keychron/q3_max/qmkata_sysex_handler.c` (991 lines)

QMKata is a proprietary host-keyboard communication protocol using Raw HID:

| Feature | Implementation |
|---------|---------------|
| Protocol | SysEx-like messages over Raw HID |
| Commands | SET, GET, ADD, DEL, PUB, SUB |
| Key Event Reporting | Host subscribes to key events |
| EEPROM Management | Read/write EEPROM from host |
| RGB Matrix | Host buffer synchronization for dynamic animations |
| Layer Control | Default layer switching, Mac/Win mode |
| Status Reporting | Battery, DIP switch, matrix state |
| Dynamic LED Functions | Load and execute custom LED animation code |

**Integration points:**
- `qmkata_rgb_matrix_user.c` — Runs dynamically loaded RGB animation code
- `rgb_matrix_indicators_keychron()` — Calls `rgb_matrix_host_buf_render()` for host buffer sync
- `keyboard_post_init_user()` — Initializes QMKata

---

## 11. RGB Matrix Integration

### 11.1 LED Driver

**File:** `keyboards/keychron/q3_max/ansi_encoder/keyboard.json`, `config.h`

```json
"rgb_matrix": {
    "driver": "snled27351_spi",
    "animations": {
        "breathing": true,
        "cycle_all": true,
        "cycle_left_right": true,
        ... (20+ animations)
    }
}
```

Two SNLED27351 SPI drivers handle:
- **ANSI:** 87 LEDs across 2 chips
- **ISO:** 88 LEDs across 2 chips

### 11.2 Indicator Chain

**File:** `keyboards/keychron/common/keychron_task.c:99-121`

```c
bool rgb_matrix_indicators_keychron(void) {
    os_state_indicate();                        // Mac/Win mode indicator
    rgb_matrix_indicators_bt();                 // BT host indicator
    analog_matrix_indicator();                  // Profile indicator
    factory_test_indicator();                   // Factory test indicator
    backlit_indicator();                        // Backlight patterns
    rgb_matrix_host_buf_render();               // QMKata host buffer sync
    return true;
}
```

**Q3 Max user override** (`keymap.c:232-238`):
```c
bool rgb_matrix_indicators_user(void) {
    if (leader_sequence_active() && leader_trigger_led != NO_LED) {
        rgb_matrix_set_color(leader_trigger_led, 255, 255, 255);
    }
    return true;
}
```

The indicator chain is called from `rgb_matrix_indicators_kb()` which calls user first, then keychron.

---

## 12. EEPROM Storage

### 12.1 Wear Leveling

**File:** `keyboards/keychron/q3_max/info.json:66-71`

```json
"eeprom": {
    "wear_leveling": {
        "driver": "embedded_flash",
        "logical_size": 3072,
        "backing_size": 6144
    }
}
```

STM32F401 uses embedded flash with wear leveling: 3072 bytes logical EEPROM backed by 6144 bytes of flash (2:1 ratio). This protects flash memory from premature wear due to frequent writes.

### 12.2 Stored Configuration

| Setting | Storage Location | Persistence |
|---------|-----------------|-------------|
| Default layer | `eeconfig_update_default_layer()` | EEPROM wear leveling |
| NKRO on/off | `eeconfig_update_keymap()` | EEPROM wear leveling |
| No-GUI | `eeconfig_update_keymap()` | EEPROM wear leveling |
| Debounce type/time | EEPROM `OFFSET_DEBOUNCE` | Dynamic via Raw HID |
| Tap dance actions | `tap_dance_eeprom.h` | Dynamic via Raw HID |
| Combo definitions | `combo_eeprom.h` | Dynamic via Raw HID |
| Leader sequences | `leader_eeprom.h` | Dynamic via Raw HID |
| RGB config | `eeconfig_custom_rgb.h` | Dynamic via Raw HID |

---

## 13. Error Handling and Edge Cases

### 13.1 Matrix Scan Edge Cases

| Scenario | Detection | Mitigation |
|----------|-----------|------------|
| Row pin floating | Inconsistent readings across scans | Internal pull-ups on all row pins (board.h) |
| Column stuck LOW | All keys in column always pressed | Column released after each scan cycle |
| Ghosting | Non-adjacent keys simultaneously pressed | ROW2COL diode direction prevents most ghosting |
| Chattering key | Multiple transitions within debounce window | Debounce algorithm filters bounces |

### 13.2 Keycode Processing Edge Cases

| Scenario | Handling |
|----------|----------|
| Unknown custom keycode | `process_record_keychron_common()` returns `true` → falls through to standard QMK processing |
| Modifier stacking overflow | QMK limits to 8 modifier bits + 6 keys (6KRO) or unlimited (NKRO) |
| Layer overflow | `layer_state_t` is 32-bit (32 layers max), keymap size fixed at compile time |
| Combo firing during tap-hold | Combo detection runs before tap-hold resolution; combo keys cannot be tap-hold keys |
| Leader key early termination | `post_process_record_user()` checks prefix; ends sequence if no prefix matches |
| Duplicate key re-press | `register_code()` calls `del_key()` then `add_key()` to refresh the key in the report |

### 13.3 EEPROM Edge Cases

| Scenario | Handling |
|----------|----------|
| Power loss during write | Wear leveling driver (embedded_flash) protects against corruption |
| Corrupted EEPROM | Default settings loaded on first boot; bootmagic resets EEPROM |
| Flash wear | 2:1 backing ratio; wear leveling spreads writes across flash sectors |

### 13.4 Q3 Max-Specific Edge Cases

| Scenario | Handling |
|----------|----------|
| RDP shortcut detection | `register_code16()` override sends modifiers sequentially (q3_max.c:66-113) |
| Dip switch during operation | Toggles default layer instantly; FN layer still works as momentary override |
| Siri timeout | `keychron_common_task()` releases Cmd+Space after 500ms idle |
| Power-on LED | `keychron_task_kb()` pulses BAT_LOW_LED for 3 seconds after boot (q3_max.c:42-57) |
| Combo outputs QK_LEADER | `process_record_user()` intercepts via `record->keycode` (not `keycode` param) because `process_record_quantum` re-derives keycode from position (keymap.c:106) |

---

## 14. Timing and Performance

### 14.1 Matrix Scan Timing

| Operation | Time |
|-----------|------|
| Column drive LOW | ~1 μs |
| Column settle time | ~2 μs (200 cycles at 84 MHz) |
| 6 row pin reads | ~1 μs |
| Column release | ~1 μs |
| Per-column total | ~5 μs |
| 17 columns total | ~85 μs |
| memcmp comparison | ~5 μs |
| **Total per scan** | **~100 μs** |

Matrix scanning is not a bottleneck — the MCU can easily scan at 1 kHz with 90%+ CPU headroom.

### 14.2 End-to-End Latency

| Stage | Latency |
|-------|---------|
| Matrix scan | ~100 μs |
| Debounce (eager per-key) | 0 ms (press) / 50 ms (release) |
| Action resolution | ~5 μs |
| Report building | ~5 μs |
| USB transfer (110 Mbps) | ~15 μs (16 bytes) |
| **Total (press, eager)** | **~120 μs** |
| **Total (release, 50ms debounce)** | **~50 ms** |

Press latency is dominated by the eager per-key algorithm (near-instant). Release latency is dominated by the 50ms debounce window.

### 14.3 Memory Usage

| Component | Size (bytes) |
|-----------|-------------|
| Matrix (raw + cooked) | 6 × 4 × 2 = 48 |
| Debounce (per-key state) | 102 × 4 ≈ 408 |
| Keymap (4 layers × 6 × 17) | 4 × 102 × 2 = 816 |
| QMKata sysex handler | ~991 lines (~15-20 KB flash) |
| **Total RAM** | **~5-8 KB** |
| **Total Flash** | **~80-120 KB** |

STM32F401 has 512 KB Flash, 96 KB RAM → well within limits.

---

## 15. Customization Reference

### 15.1 User-Level Hooks (keymap.c)

| Function | Called By | Purpose |
|----------|-----------|---------|
| `process_record_user()` | `process_record_kb()` | Intercept any keycode, handle leader/combo |
| `encoder_update_user()` | `encoder_task()` | Custom encoder event handling |
| `matrix_scan_user()` | `matrix_scan()` | Custom matrix scan callback |
| `matrix_init_user()` | `matrix_init()` | Custom matrix initialization |
| `keyboard_post_init_user()` | `keyboard_post_init()` | Post-initialization (QMKata, EEPROM) |
| `housekeeping_task_user()` | `keyboard_task()` | Every main loop iteration |
| `keychron_task_user()` | `keychron_task_kb()` | Keychron-specific per-iteration task |
| `layer_state_set_user()` | `layer_*()` | Layer state changed |
| `default_layer_state_set_user()` | `default_layer_*()` | Default layer changed |
| `rgb_matrix_indicators_user()` | `rgb_matrix_indicators_kb()` | RGB indicator state |
| `post_process_record_user()` | `action_exec()` | Post-action processing (leader early termination) |
| `leader_end_user()` | `leader_end()` | Leader sequence ended |
| `leader_add_user()` | `leader_sequence_add()` | Leader sequence key capture |
| `dip_switch_update_user()` | `dip_switch_update_kb()` | Dip switch change |

### 15.2 Keyboard-Level Hooks (q3_max.c)

| Function | Called By | Purpose |
|----------|-----------|---------|
| `keyboard_post_init_kb()` | `keyboard_post_init()` | Keyboard init (calls `keychron_common_init()`) |
| `keychron_task_kb()` | `keychron_task()` | Power-on LED indicator |
| `dip_switch_update_kb()` | dip switch handler | Layer toggle on DIP change |
| `register_code16()` | action dispatch | Sequential modifier HID reports |
| `unregister_code16()` | action dispatch | Sequential modifier cleanup |
| `process_record_keychron_kb()` | `process_record_keychron()` | KB-specific processing (weak, default: `true`) |

### 15.3 Quantum-Level Hooks

| Function | Called By | Purpose |
|----------|-----------|---------|
| `process_record_quantum()` | `process_record()` | Quantum pre-processing hook (weak, return false to short-circuit) |
| `post_process_record_quantum()` | `process_record()` | Quantum post-processing hook (weak) |
| `bootmagic_reset_eeprom()` | `bootmagic()` | EEPROM reset on bootmagic |

---

## 16. Troubleshooting Guide

### 16.1 Keys Not Registering

1. **Check matrix scanning:** Enable `MATRIX_DEBUG` in `config.h`, verify row/column pins in `board.h`
2. **Check debounce:** Increase debounce time via Raw HID or EEPROM; try different algorithm
3. **Check diode direction:** ROW2COL means rows are outputs, cols are inputs; verify with multimeter
4. **Check combo interception:** A press might be consumed by a combo before reaching standard processing

### 16.2 Keys Sending Wrong Codes

1. **Check layer state:** `get_highest_layer(default_layer_state | layer_state)` determines which layer is active
2. **Check dip switch:** DIP 0 toggles between MAC_BASE (layer 0) and WIN_BASE (layer 2)
3. **Check OS keycodes:** `KC_LOPTN` etc. are translated by `process_record_keychron_common()`
4. **Check tap-hold:** MOD_TAP / LAYER_TAP keys may be in tap-hold detection state

### 16.3 Encoder Not Working

1. **Check PAL callbacks:** `encoder_cb_init()` registers ChibiOS PAL callbacks on B15/B14
2. **Check encoder map:** `encoder_map[][0][0]` (CCW) and `encoder_map[][0][1]` (CW) per layer
3. **Check `encoder_map` vs `encoder_map_keycode`:** QMK supports two encoder mapping APIs

### 16.4 RDP Not Detecting Shortcuts

1. **Verify override:** `register_code16()` in `q3_max.c:77` should send modifiers sequentially
2. **Check QK_MODS:** Only `IS_QK_MODS(code)` triggers sequential behavior; standard keycodes use original QMK behavior

### 16.5 Leader Key Not Triggering

1. **Check combo → leader path:** If combo outputs `QK_LEADER`, `process_record_user()` checks `record->keycode` (not the `keycode` parameter)
2. **Check early termination:** `post_process_record_user()` ends sequence if no prefix matches
3. **Check `leader_already_matched` flag:** Prevents double-fire via `leader_end_user()`

---

## 17. Glossary

| Term | Definition |
|------|------------|
| **ChibiOS** | Real-time operating system running on STM32 |
| **DIP Switch** | Hardware switch (pin C9) toggling Mac/Win mode |
| **FN** | Function layer key, momentary (MO) activation |
| **HID** | Human Interface Device (USB protocol) |
| **KC_NO** | No-op keycode |
| **KC_TRNS** | Transparent keycode (passes through to lower layer) |
| **NKRO** | N-Key Rollover (all keys reported simultaneously) |
| **PAL** | ChibiOS Peripheral Abstraction Layer (GPIO, interrupts) |
| **QK_MODS** | QMK modifier keycode prefix (0x20xx) |
| **QMKata** | Keychron's proprietary host-keyboard protocol via Raw HID |
| **ROW2COL** | Diode direction: rows driven LOW, columns read with pull-ups |
| **SNLED27351** | SPI RGB LED driver IC (144 channels max) |
| **TAPPING_TERM** | Time window for tap-hold detection (typically 200ms) |
| **VIA** | Visual Interface for Anatomy (keymap configuration tool) |
| **Raw HID** | USB Human Interface Device with custom data endpoints |

---

## 18. Appendix: Processing Flow Decision Tree

```
Key Press Detected (matrix changed)
  │
  ├─→ Is it a changed bit (press or release)?
  │   Yes → action_exec(MAKE_KEYEVENT)
  │   No  → skip
  │
  ├─→ Is it a tap-hold key (MOD_TAP / LAYER_TAP)?
  │   Yes → Enter tap-hold state machine:
  │         │
  │         ├─→ Release same key within TAPPING_TERM? → TAP
  │         ├─→ Other key pressed within TAPPING_TERM? → HOLD (or TAP)
  │         └─→ TAPPING_TERM expired? → HOLD
  │   No  → Continue immediately
  │
  ├─→ Resolve action (layer_switch_get_layer → action_for_key)
  │   │
  │   └─→ Is action TRANSPARENT? → Check next lower layer
  │
  ├─→ Call process_record chain:
  │   process_record_user() → process_record_keychron() → ...
  │   │
  │   └─→ Any handler returns false? → Skip remaining chain
  │
  ├─→ Process action (register_code / layer_on / host_send / ...)
  │
  ├─→ Build and send HID report
  │   │
  │   └─→ Report changed from last? → Send; else skip
  │
  └─→ Periodic tasks (quantum_task, keychron_task)
      │
      ├─→ Timer-based timeouts (Siri, OS select, WinLock)
      ├─→ Tap dance / combo / leader finalization
      └─→ LED / RGB indicator updates
```

---

**End of Document**
