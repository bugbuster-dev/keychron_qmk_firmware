# Sticky Combo — Design

> Branch: `refactor/key-processing-sm` (or new feature branch)
> Pipeline: extends the existing `KEY_PROCESSING_SM_ENABLE` framework
> Status: design approved, ready for implementation plan

---

## Problem

QMK's combo system fires a single keycode when N keys are pressed simultaneously,
then returns to idle. There's no built-in way to express "after combo fires,
arm a follow-up tap action on one of the combo keys".

Use case: a "combo + tap" gesture for advanced shortcuts. E.g., `A+B` triggers
a navigation mode where holding `A` and tapping `B` repeatedly cycles through
windows, while holding `B` and tapping `A` cycles back.

## Solution

A new pipeline feature: `sticky_combo`. Uses a 4-state machine (StateSmith) to
model:

```
idle → armed_both → armed_for_key1 → idle
                  ↘ armed_for_key2 ↗
```

The held key acts as a sticky modifier; the other key becomes a tap trigger
that fires a configurable action on each press.

## Data model

User defines an array of sticky combo definitions:

```c
typedef struct {
    uint16_t key1;
    uint16_t key2;
    uint16_t combo_action;    // fired on simultaneous press of key1+key2; 0 = none
    uint16_t tap_action_1;    // fired on each tap of key1 (while key2 still held)
    uint16_t tap_action_2;    // fired on each tap of key2 (while key1 still held)
} sticky_combo_def_t;

extern const sticky_combo_def_t sticky_combos[];
extern const uint8_t sticky_combo_count;
```

Example user configuration:
```c
const sticky_combo_def_t sticky_combos[] = {
    // A+B fires Ctrl+C, then A→F1 (with B held), B→F2 (with A held)
    {KC_A, KC_B, LCTL(KC_C), KC_F1, KC_F2},
    // J+K silent arm, then J→Up, K→Down (held nav)
    {KC_J, KC_K, KC_NO,      KC_UP, KC_DOWN},
};
const uint8_t sticky_combo_count = 2;
```

## State machine

```plantuml
@startuml StickyCombo
state idle
state armed_both
state armed_for_key1   ' key2 still held; tapping key1 fires tap_action_1
state armed_for_key2   ' key1 still held; tapping key2 fires tap_action_2

[*] -> idle

idle --> armed_both : on_combo_press
armed_both --> armed_for_key1 : on_release_key2
armed_both --> armed_for_key2 : on_release_key1
armed_both --> idle : on_release_both

armed_for_key1 --> armed_for_key1 : on_tap_key1
armed_for_key1 --> idle : on_release_key2

armed_for_key2 --> armed_for_key2 : on_tap_key2
armed_for_key2 --> idle : on_release_key1
@enduml
```

**Note**: In `armed_for_key1`, the "tap" is what fires the action; the held
modifier (key2) stays down. When key2 is released, the sequence ends.

## Architecture

```
keyevent → pipeline PRE_TAP phase → sticky_combo machine
                                    │
                                    ▼
                                   idle?       → observe only, pass through
                                   armed_both? → watch for releases, consume key events
                                   armed_for_*? → tap = fire action + consume
                                                  release of held key = → idle
```

### Adapter state

```c
typedef struct {
    StickyCombo sm;
    int8_t  active_combo;       // index into sticky_combos[], -1 if none
    bool    key1_pressed;       // physical state of key1 (active combo)
    bool    key2_pressed;       // physical state of key2 (active combo)
    uint16_t first_press_time;  // for simultaneous-press detection
    uint16_t first_press_keycode; // which key was pressed first
} sticky_combo_state_t;
```

### Detection of simultaneous press

Self-contained — no dependency on QMK's combo system:
- On press of any key that matches `key1` or `key2` of any defined combo:
  - If no first_press recorded → record it, start timer
  - If first_press matches the other key of the same combo, and within `COMBO_WINDOW` (50ms): fire `combo_action`, transition to `armed_both`
  - On timeout: clear first_press, pass the key through normally

### Action dispatch

Use `tap_code16()` for the combo action and tap actions. This handles modifier-bearing
keycodes correctly (e.g., `LCTL(KC_C)`).

### Pipeline placement

- **Phase**: `PHASE_PRE_TAP` (runs before tap/hold resolution)
- **Priority**: 50 (runs before vim_modal at 50, may need higher; TBD per testing)
- **Consume rules**:
  - `idle`: pass through (don't consume) — let QMK combo and normal processing run
  - `armed_*`: consume events for the active combo's keys (key1, key2)
  - Other keys always pass through

## Build configuration

```makefile
KEY_PROCESSING_SM_ENABLE = yes
STICKY_COMBO_ENABLE = yes
```

The `sticky_combos[]` array is declared `extern` in the adapter; the user
provides the definition in their keymap. Build fails at link time if undefined
when `STICKY_COMBO_ENABLE = yes`.

## Files

| File | Purpose | LOC est. |
|------|---------|---------|
| `quantum/features/sticky_combo.puml` | State diagram | ~20 |
| `quantum/features/StickyCombo.c/h` | Generated SM (StateSmith output) | ~200 |
| `quantum/features/sticky_combo_adapter.h` | Public API | ~10 |
| `quantum/features/sticky_combo_adapter.c` | Detection + dispatch | ~150 |
| User keymap | `sticky_combos[]` definition | ~5 per combo |

## Edge cases

| Scenario | Behavior |
|----------|----------|
| Third key pressed during `armed_both` | Pass through; don't affect SM |
| Third key pressed during `armed_for_key1` | Pass through; don't affect SM |
| Both combo keys pressed but not simultaneously | First press passes through normally after timeout; no combo fires |
| User presses key1+key2, releases both before timeout | Combo fires immediately on second press; `armed_both` is brief; `on_release_both` returns to idle |
| Another combo's keys pressed while one combo is `armed_*` | Ignored — only the active combo's keys are tracked |
| `combo_action == KC_NO` | No combo action fired; SM still arms |
| `tap_action_1 == KC_NO` | Taps do nothing but still consumed |

## Testing checklist

1. Press A+B simultaneously → combo_action fires
2. Press A+B, release B, tap B repeatedly → tap_action_2 fires per tap
3. Press A+B, release A, tap A repeatedly → tap_action_1 fires per tap
4. Press A+B, release both → no further taps process
5. Press A+B, release B, tap B, release A → returns to idle, A+B works again
6. Press A alone (not paired) → passes through normally after window expires
7. Multiple combos defined: A+B and C+D both work independently
8. `combo_action = KC_NO`: no combo fires but arming works
9. Third key during `armed_*`: passes through, doesn't break SM

## Open questions

1. **Simultaneous window**: 50ms default. Per-combo override? (YAGNI for now)
2. **Tap window in armed state**: do we time out the armed state? Spec says "release of held key ends it", so no timeout. But if a user holds key1 for 30 seconds without tapping, that's fine.
3. **Re-entering armed state**: after `armed_for_key1 → idle`, can user press key1 again to re-arm? Yes, naturally — back to detection.
4. **Interaction with QMK's regular combos**: if A+B is also defined in `key_combos[]`, both fire? Recommend: don't define the same combo in both places.

## Non-goals

- ❌ EEPROM persistence (future work if needed)
- ❌ Per-combo timing config (just use COMBO_WINDOW default)
- ❌ More than 2 keys per combo (extension for later)
- ❌ Nested sticky combos (one active at a time)
