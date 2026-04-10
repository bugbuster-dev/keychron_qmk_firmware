# Dynamic Tap Dance Design

**Date:** 2026-04-10
**Branch:** dynamic_combo_tapdance_leader
**Status:** Approved

## Overview

Add EEPROM-backed dynamic tap dance definitions to the Keychron Q3 Max firmware
and a corresponding UI tab in the QMKata host tool. Follows the same architecture
as the existing dynamic combo system (`combo_eeprom.c/h`).

Each of the 8 tap dance slots supports four keycodes: tap×1, tap×2, tap×3, and
hold. Slots are assigned to physical keys via VIA's dynamic keymap (`TD(0)`..
`TD(7)`); behaviors are configured at runtime via the QMKata host tool.

## Requirements

- 8 slots, each with: kc1 (tap×1), kc2 (tap×2), kc3 (tap×3), hold
- Tap fall-through: unset tap count falls to previous (kc3=0 → use kc2, kc2=0 → use kc1)
- Hold: if hold=0, do nothing (no fall-through)
- kc1=0 disables the slot entirely
- Physical key-to-slot assignment via VIA (no compile-time keymap edits needed)
- Behaviors configured at runtime via QMKata host tool
- Keycode names accepted (KC_A, LCTL(KC_B), LT(1,KC_SPC), etc.)

## Chosen Approach

Mirror the `combo_eeprom` pattern exactly:
- New firmware module `keyboards/keychron/common/tap_dance/tap_dance_eeprom.c/h`
- One set of shared QMK tap dance callbacks for all 8 slots (use `user_data` for per-slot config)
- `tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS]` defined in `keymap.c`, populated from EEPROM at init
- New SysEx command ID `QMKATA_ID_TAP_DANCE = 12`
- New `TapDanceConfigTab` in `QMKata.py`

## Data Structures

### `tap_dance_def_t` (8 bytes, packed)

```c
typedef struct __attribute__((packed)) {
    uint16_t kc1;   // tap ×1  (0 = slot disabled)
    uint16_t kc2;   // tap ×2  (0 = fall through to kc1)
    uint16_t kc3;   // tap ×3  (0 = fall through to kc2)
    uint16_t hold;  // hold    (0 = do nothing)
} tap_dance_def_t;
```

### EEPROM Layout

New `keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h`:

```c
#define TAP_DANCE_DEF_MAX_SLOTS  8
// 1 byte magic + 8 slots × 8 bytes = 65 bytes
#define EECONFIG_SIZE_TAP_DANCE  (1 + TAP_DANCE_DEF_MAX_SLOTS * sizeof(tap_dance_def_t))
```

Chained after combo in `keyboards/keychron/common/eeconfig_kb.h`:

```c
#ifdef DYNAMIC_TAP_DANCE_ENABLE
#    include "eeconfig_tap_dance.h"
#    define __EECONFIG_SIZE_TAP_DANCE EECONFIG_SIZE_TAP_DANCE
#else
#    define __EECONFIG_SIZE_TAP_DANCE 0
#endif
#define EECONFIG_BASE_TAP_DANCE  EECONFIG_END_COMBO
#define EECONFIG_END_TAP_DANCE   (EECONFIG_BASE_TAP_DANCE + __EECONFIG_SIZE_TAP_DANCE)

#undef EECONFIG_KB_DATA_SIZE
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_TAP_DANCE - EECONFIG_BASE_LANGUAGE)
```

Total new EEPROM: **65 bytes** (current ~290 → ~355 bytes; 2KB budget).

## Firmware Module

### `tap_dance_eeprom.h`

```c
#pragma once
#include <stdint.h>
#include "eeconfig_tap_dance.h"

typedef struct __attribute__((packed)) {
    uint16_t kc1;
    uint16_t kc2;
    uint16_t kc3;
    uint16_t hold;
} tap_dance_def_t;

void tap_dance_eeprom_init(void);
void tap_dance_eeprom_set(uint8_t slot, const tap_dance_def_t *def);
void tap_dance_eeprom_get(uint8_t slot, tap_dance_def_t *out);
void tap_dance_eeprom_clear(uint8_t slot);
void tap_dance_eeprom_reset_defaults(void);
```

### `tap_dance_eeprom.c` — Key Logic

**RAM mirror** (persistent pointers required by `tap_dance_actions[].user_data`):
```c
static tap_dance_def_t td_defs[TAP_DANCE_DEF_MAX_SLOTS];
static uint16_t        td_last_kc[TAP_DANCE_DEF_MAX_SLOTS];  // tracks last sent keycode for reset
```

**Shared callbacks** — written once, shared across all 8 slots:

```c
static void _td_finished(tap_dance_state_t *state, void *user_data) {
    uint8_t slot = (tap_dance_def_t*)user_data - td_defs;
    tap_dance_def_t *def = (tap_dance_def_t *)user_data;
    uint16_t kc = KC_NO;
    if (state->pressed && def->hold) {
        kc = def->hold;
    } else {
        switch (state->count) {
            case 1:  kc = def->kc1; break;
            case 2:  kc = def->kc2 ? def->kc2 : def->kc1; break;
            default: kc = def->kc3 ? def->kc3 : (def->kc2 ? def->kc2 : def->kc1); break;
        }
    }
    td_last_kc[slot] = kc;
    if (kc) register_code16(kc);
}

static void _td_reset(tap_dance_state_t *state, void *user_data) {
    uint8_t slot = (tap_dance_def_t*)user_data - td_defs;
    if (td_last_kc[slot]) {
        unregister_code16(td_last_kc[slot]);
        td_last_kc[slot] = KC_NO;
    }
}
```

**`tap_dance_eeprom_apply()`** — populates `tap_dance_actions[]` from `td_defs[]`:
```c
static void tap_dance_eeprom_apply(void) {
    for (uint8_t i = 0; i < TAP_DANCE_DEF_MAX_SLOTS; i++) {
        tap_dance_actions[i].fn.on_each_tap      = NULL;
        tap_dance_actions[i].fn.on_dance_finished = _td_finished;
        tap_dance_actions[i].fn.on_reset          = _td_reset;
        tap_dance_actions[i].fn.on_each_release   = NULL;
        tap_dance_actions[i].user_data            = &td_defs[i];
    }
}
```

### `keymap.c` changes

```c
#ifdef DYNAMIC_TAP_DANCE_ENABLE
tap_dance_action_t tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS];
#else
// existing static definitions remain
tap_dance_action_t tap_dance_actions[] = { ... };
#endif
```

`tap_dance_eeprom_init()` called from `keyboard_post_init_user()` (same as `combo_eeprom_init()`).

### `rules.mk` / `common.mk`

Add `DYNAMIC_TAP_DANCE_ENABLE = yes` and include the new module's path.

## SysEx Protocol

### New command ID

In `keyboards/keychron/qmkata/QMKata.h`:
```c
QMKATA_ID_TAP_DANCE = 12,  // EEPROM-backed tap dance definitions
```

### SET (host → keyboard)

```
buf[0]    = slot (0–7)
buf[1..2] = kc1  (uint16_t LE)
buf[3..4] = kc2  (uint16_t LE)
buf[5..6] = kc3  (uint16_t LE)
buf[7..8] = hold (uint16_t LE)
```
Total: 9 bytes. Calls `tap_dance_eeprom_set(slot, &def)`.

### GET (host → keyboard)

```
buf[0] = slot (0–7)
```

### GET response (keyboard → host)

```
resp[0]     = seqnum
resp[1]     = QMKATA_ID_TAP_DANCE
resp[2]     = slot
resp[3..10] = tap_dance_def_t (8 bytes)
```
Total: 11 bytes.

Handler location: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`, guarded by
`#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)`, dispatched
alongside the existing combo handler.

## Host Tool (QMKata)

### `QMKataKeyboard.py`

```python
signal_tap_dance = pyqtSignal(int, object)  # (slot, tap_dance_def bytes)

def keyb_get_tap_dance(self, slot): ...     # fire-and-forget GET
def keyb_set_tap_dance(self, slot, kc1, kc2, kc3, hold): ...  # SET
```

`sysex_response_handler` new case for `ID_TAP_DANCE = 12`: unpacks 11-byte
response, emits `signal_tap_dance(slot, raw_bytes)`.

### `TapDanceConfigTab` in `QMKata.py`

- `QTableWidget` with 8 rows, columns: **Slot | Tap×1 | Tap×2 | Tap×3 | Hold**
- Each cell: `QLineEdit` accepting keycode names via `KeycodeResolver`
- **Refresh** button: GETs all 8 slots sequentially
- **Save** button per row: SETs that slot; shows error in cell on resolution failure
- `signal_tap_dance` drives `update_slot()` to populate cells on GET response
- `KC_NO` / empty shown as blank; placeholder text `KC_NO`
- Wired into `MainWindow` alongside `ComboConfigTab`

## File Map

### New files
| File | Purpose |
|------|---------|
| `keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h` | EEPROM size/slot constants |
| `keyboards/keychron/common/tap_dance/tap_dance_eeprom.h` | Public API + `tap_dance_def_t` |
| `keyboards/keychron/common/tap_dance/tap_dance_eeprom.c` | EEPROM load/save + shared callbacks |

### Modified files
| File | Change |
|------|--------|
| `keyboards/keychron/common/eeconfig_kb.h` | Add `EECONFIG_BASE_TAP_DANCE` chain entry |
| `keyboards/keychron/q3_max/rules.mk` | `DYNAMIC_TAP_DANCE_ENABLE = yes` |
| `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c` | Dynamic `tap_dance_actions[]` + init call |
| `keyboards/keychron/q3_max/qmkata_sysex_handler.c` | SET/GET handlers for `QMKATA_ID_TAP_DANCE` |
| `keyboards/keychron/qmkata/QMKata.h` | `QMKATA_ID_TAP_DANCE = 12` |
| `qmk-tools/qmk/QMKata/QMKataKeyboard.py` | `signal_tap_dance`, `keyb_get/set_tap_dance`, response handler |
| `qmk-tools/qmk/QMKata/QMKata.py` | `TapDanceConfigTab`, `MainWindow` wiring |
