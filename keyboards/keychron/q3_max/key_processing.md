# Keychron Q3 Max — Key Event Processing

> Generated: 2026-04-28  
> Target: STM32F401 (ARM Cortex-M4)

---

## Overview

This document traces the complete call stack of a single key press from the physical matrix scan through to the HID report sent to the host. Every key event passes through the same pipeline; only the keycode and action differ.

### High-Level Stages

```
Physical Key Press
    │
    ├─ 1. Matrix Scan          GPIO read → raw_matrix[] → debounce → matrix[]
    ├─ 2. Change Detection     XOR matrix[] vs previous, ghost detection
    ├─ 3. Action Execution     keyevent_t → keyrecord_t, pre-process, pipeline pre-tap
    ├─ 4. Tap/Hold Resolution  Waiting buffer, tapping term disambiguation
    ├─ 5. Process Pipeline     30+ process_*() handlers (#ifdef-gated)
    ├─ 6. Action Resolution    keycode → action_t → register/unregister
    ├─ 7. Post-Process         Leader key matching
    └─ 8. HID Report           send_keyboard_report() → USB / BT / 2.4G
```

Stages 3-6 are nested calls within `action_exec()`, not sequential pipeline stages.

---

## Stage 1: Main Loop & Matrix Scan

### Entry Point

```
main()                                    // quantum/main.c
  └── while(true)
        └── protocol_keyboard_task()      // quantum/main.c
              └── keyboard_task()         // quantum/keyboard.c:727
                    ├── matrix_task()     // quantum/keyboard.c:604
                    └── quantum_task()    // periodic: combo, leader, tap dance, dip switch
```

### Matrix Scan (`matrix_scan()`)

```c
// quantum/matrix.c:319-345 (actual implementation; matrix_common.c version is weak fallback)
uint8_t matrix_scan(void) {
    // Set col, read rows (ROW2COL diodes for Q3 Max)
    for each col: matrix_read_rows_on_col(curr_matrix, col, row_shifter);

    bool changed = memcmp(raw_matrix, curr_matrix, sizeof(curr_matrix)) != 0;
    if (changed) memcpy(raw_matrix, curr_matrix, sizeof(curr_matrix));

    changed = debounce(raw_matrix, matrix, MATRIX_ROWS_PER_HAND, changed);
    matrix_scan_kb();  // weak → matrix_scan_user() (not overridden in Q3 Max)
    return (uint8_t)changed;
}
```

**Details:**
- `matrix_read_rows_on_col()` — auto-generated from `info.json` matrix pins (6 rows × 17 cols, ROW2COL diodes)
- `debounce()` — custom Keychron debounce engine, 15 ms window (per `info.json`: `"debounce_type": "custom"`, `"debounce": 15`)
- `matrix_scan_kb()` — weak function, not overridden in Q3 Max (falls through to empty `matrix_scan_user()`)

### Change Detection (`matrix_task()`)

```c
// quantum/keyboard.c:604-657
static bool matrix_task(void) {
    if (!matrix_can_read()) {
        generate_tick_event();   // drive state machines when matrix unavailable
        return false;
    }

    matrix_scan();

    // XOR previous vs current to find changed keys
    bool matrix_changed = false;
    for each row: matrix_changed |= matrix_previous[row] ^ matrix_get_row(row);

    if (!matrix_changed) {
        generate_tick_event();   // drive state machines when nothing changed
        return false;
    }

    const bool process_keypress = should_process_keypress();

    for each changed key:
        if (!has_ghost_in_row(row, current_row)) {
            if (process_keypress)
                action_exec(MAKE_KEYEVENT(row, col, pressed));   // line 646
            switch_events(row, col, pressed);                     // → led_matrix / rgb_matrix
        }
}
```

**Tick events:** When no keys change, `generate_tick_event()` creates a `TICK_EVENT` that flows through `action_exec()` to drive timer-based state machines (tap-hold resolution, leader timeout, etc.) without real key events.

**switch_events:** Routes key events to LED matrix and RGB matrix subsystems for per-key visual effects.

---

## Stage 2: Action Execution

### `action_exec()` — Event to Record Conversion

```c
// quantum/action.c:84-161
void action_exec(keyevent_t event) {
    // Debug printing for real events (IS_EVENT checks event.type != TICK_EVENT)
    if (IS_EVENT(event)) { /* debug */ }

    if (event.pressed) clear_weak_mods();

#ifdef SWAP_HANDS_ENABLE
    if (IS_EVENT(event)) process_hand_swap(&event);
#endif

    keyrecord_t record = {.event = event};

#ifndef NO_ACTION_ONESHOT
    // Clear timed-out oneshot layers/mods when another key is pressed
    if (keymap_config.oneshot_enable) {
        if (has_oneshot_layer_timed_out()) clear_oneshot_layer_state(...);
        if (has_oneshot_mods_timed_out()) clear_oneshot_mods();
    }
#endif

#ifndef NO_ACTION_TAPPING
#ifdef KEY_PROCESSING_SM_ENABLE
    // Pipeline pre-tap phase: SMs can intercept and consume key events
    // (e.g., vim modal mode translates h→Left and consumes the original)
    if (!IS_NOEVENT(record.event)) {
        if (pipeline_process_pre_tap(&event, &record)) {
            return;  // event consumed, skip further processing
        }
    }
#endif
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        action_tapping_process(record);   // tap/hold disambiguation
    }
#else
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        process_record(&record);
    }
#endif
}
```

---

## Stage 3: Pre-Process Pipeline

```c
// quantum/quantum.c:285-291
bool pre_process_record_quantum(keyrecord_t *record) {
    return pre_process_record_modules(keycode, record) &&  // module loader hooks
           pre_process_record_kb(keycode, record) &&       // → pre_process_record_user()
#ifdef COMBO_ENABLE
           process_combo(keycode, record) &&               // combo detection
#endif
           true;
}
```

**Call chain:**
- `pre_process_record_modules()` — weak, returns `true` (no-op unless modules loaded)
- `pre_process_record_kb()` — weak, delegates to `pre_process_record_user()` — not overridden in Q3 Max
- `process_combo()` — `#ifdef COMBO_ENABLE`; checks if the current key pair matches a registered combo definition

**Note:** Returning `false` at any point short-circuits the entire pipeline — the key is consumed and no further processing occurs.

---

## Stage 4: Tap/Hold Resolution

```c
// quantum/action_tapping.c:131-162
void action_tapping_process(keyrecord_t record) {
    if (process_tapping(&record)) {
        // Event resolved immediately (non tap-hold key, or tap-hold already decided)
    } else {
        // Event is pending tap-hold resolution — enqueue to waiting buffer
        if (!waiting_buffer_enq(record)) {
            // Buffer overflow: clear all states
            clear_keyboard();
            waiting_buffer_clear();
        }
    }

    // Process queued events from waiting buffer
    for (; waiting_buffer_tail != waiting_buffer_head; ...) {
        process_tapping(&waiting_buffer[waiting_buffer_tail]);
    }
}
```

This stage handles keys with dual tap/hold behavior (e.g., `LT()`, `MT()`, `LSFT_T(KC_LEFT)`). The `process_tapping()` function is a state machine that either resolves the event immediately or defers it in a circular buffer (`waiting_buffer[WAITING_BUFFER_SIZE]`) until the tapping term expires or another key press forces a decision. On resolution, it calls `process_record()`.

```c
// quantum/action.c:295-314
void process_record(keyrecord_t *record) {
    if (IS_NOEVENT(record->event)) return;

#ifdef FLOW_TAP_TERM
    flow_tap_update_last_event(record);
#endif

    if (!process_record_quantum(record)) {
#ifndef NO_ACTION_ONESHOT
        if (is_oneshot_layer_active() && record->event.pressed)
            clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
#endif
        return;     // key was consumed by a processor
    }
    process_record_handler(record);        // resolve action, execute it
    post_process_record_quantum(record);   // post-processing hooks
}
```

---

## Stage 5: Process Record — The Main Pipeline

### `process_record_quantum()` — Full Processor Chain

```c
// quantum/quantum.c:304-456
bool process_record_quantum(keyrecord_t *record) {
    uint16_t keycode = get_record_keycode(record, true);

    // Pre-processing
#ifdef SECURE_ENABLE
    preprocess_secure(keycode, record);
#endif
#ifdef TAP_DANCE_ENABLE
    preprocess_tap_dance(keycode, record);
#endif
#ifdef RGBLIGHT_ENABLE
    preprocess_rgblight();
#endif
#ifdef WPM_ENABLE
    update_wpm(keycode);
#endif

    // Feature processors (all return true/false; false = consumed)
#if defined(KEY_LOCK_ENABLE)
    process_key_lock(&keycode, record);
#endif
#if defined(DYNAMIC_MACRO_ENABLE)
    process_dynamic_macro(keycode, record);
#endif
#ifdef REPEAT_KEY_ENABLE
    process_last_key(keycode, record);
    process_repeat_key(keycode, record);
#endif
#if defined(AUDIO_ENABLE) && defined(AUDIO_CLICKY)
    process_clicky(keycode, record);
#endif
#ifdef HAPTIC_ENABLE
    process_haptic(keycode, record);
#endif
#if defined(POINTING_DEVICE_ENABLE) && defined(POINTING_DEVICE_AUTO_MOUSE_ENABLE)
    process_auto_mouse(keycode, record);
#endif

    process_record_modules(keycode, record);  // module loader
    process_record_kb(keycode, record);       // ← KEYBOARD LEVEL (see below)
#ifdef VIA_ENABLE
    process_record_via(keycode, record);
#endif
#ifdef SECURE_ENABLE
    process_secure(keycode, record);
#endif
#ifdef SEQUENCER_ENABLE
    process_sequencer(keycode, record);
#endif
#if defined(MIDI_ENABLE) && defined(MIDI_ADVANCED)
    process_midi(keycode, record);
#endif
#ifdef AUDIO_ENABLE
    process_audio(keycode, record);
#endif
#ifdef BACKLIGHT_ENABLE
    process_backlight(keycode, record);
#endif
#ifdef LED_MATRIX_ENABLE
    process_led_matrix(keycode, record);
#endif
#ifdef STENO_ENABLE
    process_steno(keycode, record);
#endif
#if defined(AUDIO_ENABLE) || defined(MIDI_BASIC)
    process_music(keycode, record);
#endif
#ifdef CAPS_WORD_ENABLE
    process_caps_word(keycode, record);
#endif
#ifdef KEY_OVERRIDE_ENABLE
    process_key_override(keycode, record);
#endif
#ifdef TAP_DANCE_ENABLE
    process_tap_dance(keycode, record);
#endif
#ifdef UNICODE_COMMON_ENABLE
    process_unicode_common(keycode, record);
#endif
#ifdef LEADER_ENABLE
    process_leader(keycode, record);
#endif
#ifdef AUTO_SHIFT_ENABLE
    process_auto_shift(keycode, record);
#endif
#ifdef DYNAMIC_TAPPING_TERM_ENABLE
    process_dynamic_tapping_term(keycode, record);
#endif
#ifdef SPACE_CADET_ENABLE
    process_space_cadet(keycode, record);
#endif
#ifdef MAGIC_ENABLE
    process_magic(keycode, record);
#endif
#ifdef GRAVE_ESC_ENABLE
    process_grave_esc(keycode, record);
#endif
#if defined(RGBLIGHT_ENABLE) || defined(RGB_MATRIX_ENABLE)
    process_underglow(keycode, record);
#endif
#ifdef RGB_MATRIX_ENABLE
    process_rgb_matrix(keycode, record);
#endif
#ifdef JOYSTICK_ENABLE
    process_joystick(keycode, record);
#endif
#ifdef PROGRAMMABLE_BUTTON_ENABLE
    process_programmable_button(keycode, record);
#endif
#ifdef AUTOCORRECT_ENABLE
    process_autocorrect(keycode, record);
#endif
#ifdef TRI_LAYER_ENABLE
    process_tri_layer(keycode, record);
#endif
#ifndef NO_ACTION_LAYER
    process_default_layer(keycode, record);
#endif
#ifdef LAYER_LOCK_ENABLE
    process_layer_lock(keycode, record);
#endif
#ifdef CONNECTION_ENABLE
    process_connection(keycode, record);
#endif
#ifndef NO_ACTION_ONESHOT
    process_oneshot(keycode, record);
#endif
    process_quantum(keycode, record);  // built-in QMK keycode handling

    return true;
}
```

### Q3 Max Keyboard-Level Override: `process_record_kb()`

```c
// quantum/quantum.c
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    return process_record_user(keycode, record);  // delegates to keymap
}
```

```c
// ansi_encoder/keymaps/keychron/keymap.c:95-119
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
#ifdef MODULE_LOADER_ENABLE
    // 1. Module loader dispatch
    if (!module_dispatch_process_record(keycode, record)) return false;
#endif

#if defined(LEADER_ENABLE) && defined(RGB_MATRIX_ENABLE)
    // 2. Leader LED tracking (RGB indicator)
    if (record->event.pressed && (IS_QK_TAP_DANCE(keycode) || keycode == QK_LEADER)) {
        leader_trigger_led = g_led_config.matrix_co[record->event.key.row][record->event.key.col];
    }
#endif

#if defined(COMBO_ENABLE) && defined(LEADER_ENABLE)
    // 3. Combo → Leader interception
    if (record->keycode == QK_LEADER && record->event.pressed) {
        leader_start();
        return false;
    }
#endif

    // 4. Keychron common processing
    if (!process_record_keychron_common(keycode, record)) return false;
    return true;
}
```

### Keychron Common Processing

```c
// keyboards/keychron/common/keychron_common.c:162-341
bool process_record_keychron_common(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
#ifdef KC_MCTRL
        case KC_MCTRL:     // Mission Control
            register/unregister_code(KC_MISSION_CONTROL); return false;
#endif
#ifdef KC_LNPAD
        case KC_LNPAD:     // Launchpad
            register/unregister_code(KC_LAUNCHPAD); return false;
#endif
        case KC_LOPTN:     // Left Option (OS-aware)
        case KC_ROPTN:     // Right Option
        case KC_LCMMD:     // Left Command
        case KC_RCMMD:     // Right Command
            // Maps to Alt or Cmd depending on MAC/Win layer
            register/unregister_code(mac_keycode[keycode - KC_LOPTN]); return false;
#ifdef KC_SIRI
        case KC_SIRI:      // Siri / Assistant
            if (record->event.pressed) {
                if (!is_siri_active) {
                    is_siri_active = true;
                    register_code(KC_LCMD);
                    register_code(KC_SPACE);
                }
                siri_timer = timer_read32();
            }
            // Auto-unregister after 500ms via keychron_common_task()
            return false;
#endif
        case KC_TASK:      // Task View (Win+Tab)
        case KC_FILE:      // File Explorer (Win+E)
#ifdef KC_SNAP
        case KC_SNAP:      // Screenshot (Shift+Cmd+4)
#endif
#ifdef KC_CTANA
        case KC_CTANA:     // Lock (Win+L)
#endif
#ifdef WIN_LOCK_SCREEN_ENABLE
        case KC_WLCK:      // Windows lock screen
#endif
#ifdef MAC_LOCK_SCREEN_ENABLE
        case KC_MLCK:      // Mac lock screen
#endif
            // Register multi-key combos from key_comb_list[] array
            for each key in combo: register/unregister_code(key);
            return false;
#ifdef LED_MATRIX_ENABLE
        case BL_SPI:        // LED matrix speed increase
        case BL_SPD:        // LED matrix speed decrease
            led_matrix_increase/decrease_speed(); break;
#endif
#if defined(WIN_LOCK_LED_PIN) || defined(WINLOCK_LED_LIST)
        case GU_TOGG:       // GUI lock toggle
            gui_toggle(); return false;
#endif
#ifdef KEYCOMBO_OS_TOGGLE_ENABLE
        case OS_TOGGL:      // OS toggle (Mac ↔ Win)
            os_toggle(); return false;
#endif
#ifdef KEYCOMBO_OS_SELECT_ENABLE
        case OS_WIN:        // Select Windows
        case OS_MAC:        // Select Mac
            os_selection logic; break;
#endif
        default: return true;  // pass through to QMK pipeline
    }
    return true;
}
```

---

## Stage 6: Action Resolution & Execution

After all processors return `true`, the key is resolved to an action and executed:

```c
// quantum/action.c:316-338
void process_record_handler(keyrecord_t *record) {
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    action_t action;
    if (record->keycode) {
        // Combo or repeat key: use the overridden keycode
        action = action_for_keycode(record->keycode);
    } else {
        action = store_or_get_action(record->event.pressed, record->event.key);
    }
#else
    action_t action = store_or_get_action(record->event.pressed, record->event.key);
#endif
    process_action(record, action);
}
```

### `process_action()` — Action Dispatch

```c
// quantum/action.c:394-924
void process_action(keyrecord_t *record, action_t action) {
    switch (action.kind.id) {
        case ACT_LMODS:       // left mod + key (LCTL(KC_A))
        case ACT_RMODS:       // right mod + key (RCTL(KC_A))
            if (event.pressed) {
                if (mods) add_weak_mods(mods); send_keyboard_report();  // send mod FIRST
                register_code(action.key.code);                          // then the key
            } else {
                unregister_code(action.key.code);
                if (mods) del_weak_mods(mods); send_keyboard_report();
            }
            break;

        case ACT_LMODS_TAP:   // tap-hold mod (LSFT_T(KC_LEFT))
        case ACT_RMODS_TAP:
            // Complex: tap = send keycode, hold = send modifier
            // Also handles MODS_ONESHOT and MODS_TAP_TOGGLE
            // Uses tap_count from action_tapping_process()
            break;

#ifdef EXTRAKEY_ENABLE
        case ACT_USAGE:       // system/consumer HID usage (not plain keycodes)
            switch (action.usage.page) {
                case PAGE_SYSTEM:    host_system_send(...); break;
                case PAGE_CONSUMER:  host_consumer_send(...); break;
            }
            break;
#endif

        case ACT_MOUSEKEY:    // mouse key emulation
            register_mouse(action.key.code, event.pressed);
            break;

#ifndef NO_ACTION_LAYER
        case ACT_LAYER:       // layer operations
            if (action.layer_bitop.on == 0) {
                // Default layer bitwise operation (AND/OR/XOR/SET)
            } else {
                // Layer bitwise operation (MO, TG, TO, etc.)
                // Handles ON_PRESS, ON_RELEASE, ON_BOTH
            }
            break;
#endif

#ifdef SWAP_HANDS_ENABLE
        case ACT_SWAP_HANDS:  // hand swap toggle
            break;
#endif
    }
}
```

**Action types:** `ACT_LMODS`, `ACT_RMODS`, `ACT_LMODS_TAP`, `ACT_RMODS_TAP`, `ACT_USAGE`, `ACT_MOUSEKEY`, `ACT_LAYER`, `ACT_SWAP_HANDS`.

### Q3 Max Override: `register_code16()` — RDP Compatibility Fix

```c
// keyboards/keychron/q3_max/q3_max.c:77-94
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // RDP fix: send each modifier as a separate HID report
        bool right = !!(code & QK_RMODS_MIN);
        if (code & QK_LCTL) register_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        uint8_t basic = code & 0xFF;
        if (basic) register_code(basic);
    } else {
        // Standard behavior for non-chord keycodes
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) register_mods(0);
        else                                             register_weak_mods(0);
        register_code(code);
    }
}

// keyboards/keychron/q3_max/q3_max.c:96-113
void unregister_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // Reverse order: basic keycode first, then modifiers in reverse
        uint8_t basic = code & 0xFF;
        bool    right = !!(code & QK_RMODS_MIN);
        if (basic) unregister_code(basic);
        if (code & QK_LGUI) unregister_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        if (code & QK_LALT) unregister_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LSFT) unregister_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LCTL) unregister_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
    } else {
        unregister_code(code);
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) unregister_mods(0);
        else                                             unregister_weak_mods(0);
    }
}
```

**Why this matters:** QMK default batches all modifier bits in one `register_weak_mods()` call, producing a single HID report. RDP and some remote desktop clients miss shortcut detection (e.g., Ctrl+Alt+Home) because they expect modifiers to arrive sequentially as physical keypresses would produce. This override splits each modifier into its own HID report.

---

## Stage 7: Post-Process Pipeline

```c
// quantum/quantum.c:294-298
void post_process_record_quantum(keyrecord_t *record) {
    uint16_t keycode = get_record_keycode(record, false);
    post_process_record_modules(keycode, record);
    post_process_record_kb(keycode, record);
}
```

### Q3 Max Leader Key Post-Processing

```c
// ansi_encoder/keymaps/keychron/keymap.c:199-226
#ifdef LEADER_ENABLE
#ifdef DYNAMIC_LEADER_ENABLE
void leader_end_user(void) {
#ifdef RGB_MATRIX_ENABLE
    leader_trigger_led = NO_LED;
#endif
    // Timeout fallback: try one final exact match
    if (!leader_already_matched) {
        leader_eeprom_try_match(leader_sequence, leader_sequence_size);
    }
    leader_already_matched = false;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    // Early termination: check for exact match after each key
    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
        leader_already_matched = true;
        leader_end();
        return;
    }

    // No prefix matches remain — end early
    if (!leader_eeprom_has_prefix(leader_sequence, leader_sequence_size)) {
        leader_end();
    }
}
#endif // DYNAMIC_LEADER_ENABLE
#endif // LEADER_ENABLE
```

---

## Stage 8: HID Report Output

```c
// quantum/action_util.c:331-348
void send_keyboard_report(void) {
#ifdef NKRO_ENABLE
#ifdef APDAPTIVE_NKRO_ENABLE
    if (kb_report_changed & KB_RPT_STD) send_6kro_report();
    if (host_can_send_nkro() && (kb_report_changed & KB_RPT_NKRO))
        send_nkro_report();
#else
    if (host_can_send_nkro() && keymap_config.nkro) {
        send_nkro_report();     // N-key roll-over
    } else {
        send_6kro_report();     // Standard 6-key
    }
#endif
#else
    send_6kro_report();
#endif
}
```

**Transport layer** (for Q3 Max wireless):
- **USB wired** → standard USB HID interrupt endpoint
- **Bluetooth** → LKBT51 chip handles BLE HID over GATT
- **2.4G** → LKBT51 chip handles proprietary 2.4 GHz protocol

---

## Complete Call Stack Summary

| # | Stage | File:Line | Function | Purpose |
|---|-------|-----------|----------|---------|
| 1 | Main loop | `quantum/main.c` | `protocol_keyboard_task()` | Main task dispatcher |
| 2 | Keyboard task | `quantum/keyboard.c:727` | `keyboard_task()` | Calls matrix_task + quantum_task + RGB + encoder |
| 3 | Matrix task | `quantum/keyboard.c:604` | `matrix_task()` | Scan, debounce, detect changes |
| 4 | Matrix scan | `quantum/matrix.c:319` | `matrix_scan()` | GPIO read → debounce |
| 5 | Action exec | `quantum/action.c:84` | `action_exec()` | Create keyrecord, pre-process |
| 6 | Pre-process | `quantum/quantum.c:285` | `pre_process_record_quantum()` | Modules, kb, combo |
| 7 | Tap resolve | `quantum/action_tapping.c:131` | `action_tapping_process()` | Waiting buffer, tap vs hold |
| 8 | Process | `quantum/action.c:295` | `process_record()` | FLOW_TAP_TERM, quantum, handler, post |
| 9 | Process pipeline | `quantum/quantum.c:304` | `process_record_quantum()` | 30+ feature processors |
| 10 | Keymap handler | `keymap.c:95` | `process_record_user()` | Module dispatch, Keychron common |
| 11 | Keychron common | `keychron_common.c:162` | `process_record_keychron_common()` | OS-aware key mapping |
| 12 | VIA processing | `quantum/quantum.c:364` | `process_record_via()` | BT, RGB, battery keycodes |
| 13 | Leader tracking | `quantum/quantum.c:403` | `process_leader()` | Leader key sequence |
| 14 | Action resolve | `quantum/action.c:316` | `process_record_handler()` | keycode → action_t |
| 15 | Action execute | `quantum/action.c:394` | `process_action()` | register/unregister keys |
| 16 | Code register | `q3_max.c:77` | `register_code16()` | RDP-compatible mod sending |
| 17 | Code unregister | `q3_max.c:96` | `unregister_code16()` | RDP-compatible mod un-register |
| 18 | Post-process | `quantum/quantum.c:294` | `post_process_record_quantum()` | Leader matching |
| 19 | HID report | `quantum/action_util.c:331` | `send_keyboard_report()` | USB/BT/2.4G output |

---

## Key Data Structures

### `keyevent_type_t`

```c
// quantum/keyboard.h:35
typedef enum keyevent_type_t {
    TICK_EVENT = 0,
    KEY_EVENT = 1,
    ENCODER_CW_EVENT = 2,
    ENCODER_CCW_EVENT = 3,
    COMBO_EVENT = 4,
    DIP_SWITCH_ON_EVENT = 5,
    DIP_SWITCH_OFF_EVENT = 6
} keyevent_type_t;
```

`IS_NOEVENT(event)` returns `true` when `event.type == TICK_EVENT` (timer-driven, no real key change).
`IS_EVENT(event)` returns `true` for all other types.

### `keyevent_t`

```c
// quantum/keyboard.h:38-43
typedef struct {
    keypos_t        key;          // { row, col }
    uint16_t        time;         // timer tick at event
    keyevent_type_t type;         // TICK_EVENT, KEY_EVENT, COMBO_EVENT, etc.
    bool            pressed;      // true = down, false = up
} keyevent_t;
```

### `tap_t`

```c
// quantum/action.h:40-45
typedef struct {
    bool    interrupted : 1;      // another key pressed during tap-hold
    bool    reserved2   : 1;
    bool    reserved1   : 1;
    bool    reserved0   : 1;
    uint8_t count       : 4;      // number of taps (for tap-hold, tap dance)
} tap_t;
```

### `keyrecord_t`

```c
// quantum/action.h:48-56
typedef struct keyrecord_t {
    keyevent_t event;
#ifndef NO_ACTION_TAPPING
    tap_t tap;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    uint16_t keycode;  // set by combo/repeat_key; 0 = use event
#endif
} keyrecord_t;
```

### `action_t`

```c
// quantum/action_code.h:119-158 (16-bit union)
typedef union {
    uint16_t code;
    struct action_kind {
        uint16_t param : 12;
        uint8_t  id : 4;          // ACT_LMODS, ACT_LAYER, ACT_USAGE, etc.
    } kind;
    struct action_key {
        uint8_t code : 8;
        uint8_t mods : 4;
        uint8_t kind : 4;
    } key;                        // ACT_LMODS, ACT_RMODS
    struct action_layer_bitop {
        uint8_t bits : 4;
        uint8_t xbit : 1;
        uint8_t part : 3;
        uint8_t on : 2;
        uint8_t op : 2;
        uint8_t kind : 4;
    } layer_bitop;                // ACT_LAYER
    struct action_layer_mods {
        uint8_t mods : 8;
        uint8_t layer : 4;
        uint8_t kind : 4;
    } layer_mods;                 // ACT_LAYER_MODS
    struct action_layer_tap {
        uint8_t code : 8;
        uint8_t val : 5;
        uint8_t kind : 3;
    } layer_tap;                  // ACT_LMODS_TAP, ACT_RMODS_TAP
    struct action_usage {
        uint16_t code : 10;
        uint8_t  page : 2;        // PAGE_SYSTEM, PAGE_CONSUMER
        uint8_t  kind : 4;
    } usage;                      // ACT_USAGE
    struct action_swap {
        uint8_t code : 8;
        uint8_t opt : 4;
        uint8_t kind : 4;
    } swap;                       // ACT_SWAP_HANDS
} action_t;
```

---

## Early Exit Points

A key event can be consumed (short-circuited) at any processor that returns `false`:

| Processor | When It Consumes |
|-----------|-----------------|
| `pipeline_process_pre_tap()` | `#ifdef KEY_PROCESSING_SM_ENABLE`; loaded SM intercepts the key |
| `pre_process_record_modules()` | Loaded module intercepts the key |
| `process_combo()` | `#ifdef COMBO_ENABLE`; combo triggers and replaces the key |
| `process_record_user()` | Keychron common handles custom keycode (KC_SIRI, KC_MCTRL, etc.) |
| `process_record_via()` | `#ifdef VIA_ENABLE`; VIA-specific keycode (BT_HST1, UG_NEXT, etc.) |
| `process_tap_dance()` | `#ifdef TAP_DANCE_ENABLE`; tap dance internal handling |
| `process_leader()` | `#ifdef LEADER_ENABLE`; leader key sequence in progress |
| `process_oneshot()` | Oneshot modifier consumed |
| `process_magic()` | `#ifdef MAGIC_ENABLE`; magic keycode (EE_CLR, etc.) |

When a processor returns `false`, `process_record_quantum()` exits immediately and neither `process_record_handler()` nor `post_process_record_quantum()` are called.

---

## Feature Flags Affecting Key Processing (Q3 Max)

| Flag | Enabled | Effect |
|------|---------|--------|
| `COMBO_ENABLE` | Yes | `process_combo()` in pre-process; combo → keycode remapping |
| `TAP_DANCE_ENABLE` | Yes | `preprocess_tap_dance()` + `process_tap_dance()` |
| `LEADER_ENABLE` | Yes | `process_leader()` + `post_process_record_user()` |
| `MODULE_LOADER_ENABLE` | Yes | `module_dispatch_process_record()` in `process_record_user()` |
| `VIA_ENABLE` | Yes (keychron keymap) | `process_record_via()` handles BT, RGB, battery keycodes |
| `RGB_MATRIX_ENABLE` | Yes | `process_rgb_matrix()` handles UG_* and RGB effect keycodes |
| `NKRO_ENABLE` | Yes | `send_keyboard_report()` chooses NKRO vs 6KRO |
| `DIP_SWITCH_ENABLE` | Yes | `dip_switch_task()` in `quantum_task()`; `dip_switch_update_kb()` on toggle |
| `ENCODER_ENABLE` | Yes | Separate pipeline via `encoder_task()` → `encoder_update()` |
| `KEY_PROCESSING_SM_ENABLE` | Yes | `pipeline_process_pre_tap()` in `action_exec()` — SMs can intercept events |
