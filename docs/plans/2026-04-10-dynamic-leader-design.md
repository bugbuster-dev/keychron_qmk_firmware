# Dynamic Leader Key Design

**Date:** 2026-04-10
**Branch:** dynamic_combo_tapdance_leader
**Status:** Approved

## Overview

Add EEPROM-backed dynamic leader key sequences to the Keychron Q3 Max firmware
and a corresponding UI tab in the QMKata host tool. Follows the same architecture
as the existing dynamic combo and tap dance systems.

Each of the 8 leader slots stores a sequence of up to 5 keycodes and an action
keycode. When the user types a matching sequence after pressing the leader key,
the action keycode is fired immediately (early termination) without waiting for
the full timeout. The leader key itself is assigned via VIA, or can be triggered
from a combo or tap dance slot using `QK_LEADER` (`0x7C00`).

## Requirements

- 8 slots, each with: sequence (up to 5 keycodes, KC_NO terminated) + action keycode
- Early termination: fire action immediately on exact match, end early when no prefix matches remain
- sequence[0] = KC_NO disables the slot entirely
- Leader key assignment via VIA (`QK_LEADER = 0x7C00`) -- no compile-time keymap edits needed
- Can also trigger leader mode via combo or tap dance (output `QK_LEADER` from a slot)
- Behaviors configured at runtime via QMKata host tool
- Keycode names accepted (KC_A, LCTL(KC_B), etc.)

## Chosen Approach

Mirror the combo/tap dance EEPROM pattern exactly:
- New firmware module `keyboards/keychron/common/leader/leader_eeprom.c/h`
- Data-driven matching engine in `leader_end_user()` + per-key early termination hook
- New SysEx command ID `QMKATA_ID_LEADER = 13`
- New `LeaderConfigTab` in `QMKata.py`

## Data Structures

### `leader_def_t` (12 bytes, packed)

```c
typedef struct __attribute__((packed)) {
    uint16_t sequence[5];  // up to 5 keycodes, KC_NO (0) terminated
    uint16_t keycode;      // action keycode when sequence matches
} leader_def_t;
```

### EEPROM Layout

New `keyboards/keychron/common/leader/eeconfig_leader.h`:

```c
#define LEADER_DEF_MAX_SLOTS     8
#define LEADER_DEF_MAX_SEQ_LEN   5
// 1 byte magic + 8 slots x 12 bytes = 97 bytes
#define EECONFIG_SIZE_LEADER     (1 + LEADER_DEF_MAX_SLOTS * 12)
```

Chained after tap dance in `keyboards/keychron/common/eeconfig_kb.h`:

```c
#ifdef DYNAMIC_LEADER_ENABLE
#    include "eeconfig_leader.h"
#    define __EECONFIG_SIZE_LEADER EECONFIG_SIZE_LEADER
#else
#    define __EECONFIG_SIZE_LEADER 0
#endif
#define EECONFIG_BASE_LEADER  EECONFIG_END_TAP_DANCE
#define EECONFIG_END_LEADER   (EECONFIG_BASE_LEADER + __EECONFIG_SIZE_LEADER)

#undef EECONFIG_KB_DATA_SIZE
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_LEADER - EECONFIG_BASE_LANGUAGE)
```

Total new EEPROM: **97 bytes** (current ~398 -> ~495 bytes; macros drop from 398 -> 301 bytes).

## Firmware Module

### `leader_eeprom.h`

```c
#pragma once
#include <stdint.h>
#include "eeconfig_leader.h"

typedef struct __attribute__((packed)) {
    uint16_t sequence[5];  // KC_NO (0) terminated
    uint16_t keycode;      // action keycode
} leader_def_t;

_Static_assert(sizeof(leader_def_t) == 12, "leader_def_t unexpected size");

void leader_eeprom_init(void);
void leader_eeprom_save(void);
void leader_eeprom_set(uint8_t slot, const leader_def_t *def);
void leader_eeprom_get(uint8_t slot, leader_def_t *out);
void leader_eeprom_clear(uint8_t slot);
void leader_eeprom_reset_defaults(void);
```

### `leader_eeprom.c` -- Key Logic

**RAM mirror:**
```c
static leader_def_t leader_defs[LEADER_DEF_MAX_SLOTS];
```

**Standard EEPROM API** (same pattern as combo/tap dance):
- `leader_eeprom_init()` -- read magic, load from EEPROM or reset defaults
- `leader_eeprom_save()` -- write magic + full RAM mirror
- `leader_eeprom_set/get/clear/reset_defaults()` -- slot operations

**Early termination matching engine:**

The key innovation is hooking into the leader key processing to check for
matches after each keypress, rather than only on timeout.

```c
// Called after each keypress during an active leader sequence.
// Returns true if the sequence should continue, false to end early.
bool leader_eeprom_check_sequence(const uint16_t *sequence, uint8_t seq_len) {
    bool has_prefix_match = false;

    for (uint8_t i = 0; i < LEADER_DEF_MAX_SLOTS; i++) {
        if (leader_defs[i].sequence[0] == KC_NO) continue;  // slot disabled

        // Check if current input matches this slot's sequence prefix
        bool prefix_ok = true;
        for (uint8_t k = 0; k < seq_len; k++) {
            if (leader_defs[i].sequence[k] != sequence[k]) {
                prefix_ok = false;
                break;
            }
        }
        if (!prefix_ok) continue;

        // Exact match: slot sequence ends right after our input
        if (leader_defs[i].sequence[seq_len] == KC_NO) {
            // Fire the action keycode
            tap_code16(leader_defs[i].keycode);
            return false;  // end sequence
        }

        // Prefix match: more keys expected
        has_prefix_match = true;
    }

    // No prefix matches remain -- end early (no valid sequence possible)
    return has_prefix_match;
}
```

**Integration with QMK leader system:**

Override `leader_end_user()` for timeout fallback (final exact-match scan).
The per-key early termination is handled via `process_leader_key()` or by
hooking `process_record_user()` to intercept keypresses during active leader
sequences. The exact hook point will be determined during implementation by
examining how `process_leader.c` calls into user code.

Two possible hook strategies:
1. **Override `process_record_user()`** -- when `leader_sequence_active()` is
   true, call `leader_eeprom_check_sequence()` after QMK appends the key.
   If it returns false, call `leader_end()`.
2. **Patch `leader_task()`** -- less clean, requires modifying QMK core.

Strategy 1 is preferred (no QMK core changes).

### `keymap.c` / `q3_max_user.c` changes

- `leader_eeprom_init()` called from `keyboard_post_init_user()`
- `leader_end_user()` implemented with final exact-match scan
- `process_record_user()` extended with early termination check

### Build integration

New `leader_eeprom.mk`:
```makefile
DYNAMIC_LEADER_ENABLE = yes
OPT_DEFS += -DDYNAMIC_LEADER_ENABLE
SRC += $(KEYCHRON_COMMON)/leader/leader_eeprom.c
VPATH += $(KEYCHRON_COMMON)/leader
```

Included from `keychron_common.mk`:
```makefile
ifeq ($(strip $(LEADER_ENABLE)), yes)
    include $(KEYCHRON_COMMON)/leader/leader_eeprom.mk
endif
```

`LEADER_ENABLE = yes` is already set in `keyboards/keychron/q3_max/rules.mk`.

## SysEx Protocol

### New command ID

In `keyboards/keychron/qmkata/QMKata.h`:
```c
QMKATA_ID_LEADER = 13,  // EEPROM-backed leader sequences
```

### SET (host -> keyboard)

```
buf[0]     = slot (0-7)
buf[1..10] = sequence[0..4] (5 x uint16_t LE)
buf[11..12] = keycode (uint16_t LE)
```
Total: 13 bytes. Calls `leader_eeprom_set(slot, &def)`.

Short payload (len==1): clears slot.

### GET (host -> keyboard)

```
buf[0] = slot (0-7)
```

### GET response (keyboard -> host)

```
resp[0]      = seqnum
resp[1]      = QMKATA_ID_LEADER
resp[2]      = slot
resp[3..14]  = leader_def_t (12 bytes)
```
Total: 15 bytes.

Handler location: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`, guarded by
`#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)`.

Bounds check: `if (slot >= LEADER_DEF_MAX_SLOTS) return;` in both SET and GET.

## Host Tool (QMKata)

### `QMKataKeyboard.py`

```python
ID_LEADER = 13

signal_leader = Signal(int, object)  # (slot, leader_def bytes)

def keyb_get_leader(self, slot):
    """Fire-and-forget GET for one leader slot."""
    self.dbg.tr("SYSEX_COMMAND", "keyb_get_leader: slot={}", slot)
    buf = bytes([slot & 0xFF])
    self.send_sysex(QMKataKeybCmd.GET, bytes([QMKataKeybCmd.ID_LEADER]) + buf)

def keyb_set_leader(self, slot, seq, keycode):
    """SET leader slot: seq is list of up to 5 keycodes, keycode is the action."""
    self.dbg.tr("SYSEX_COMMAND",
                "keyb_set_leader: slot={}, seq={}, keycode={}",
                slot, seq, keycode)
    payload = struct.pack(self.pack_endian + "B", slot)
    for i in range(5):
        kc = seq[i] if i < len(seq) else 0
        payload += struct.pack(self.pack_endian + "H", kc)
    payload += struct.pack(self.pack_endian + "H", keycode)
    self.send_sysex(QMKataKeybCmd.SET,
                    bytes([QMKataKeybCmd.ID_LEADER]) + payload)
```

Response handler: new case for `ID_LEADER = 13` in `sysex_response_handler`.
`buf[0]` is the ID (seqnum already stripped), `slot = buf[1]`, data is
`buf[2:14]` (12 bytes = leader_def_t).

### `LeaderConfigTab` in `QMKata.py`

- `QTableWidget` with 8 rows
- Columns: **Save [0x7Cxx] | Seq 1 | Seq 2 | Seq 3 | Seq 4 | Seq 5 | Action**
- Save button shows VIA keycode hint (though leader slots don't need individual
  keycodes -- the hint is for `QK_LEADER` itself)
- Save button per row with tooltip: "Assign QK_LEADER (0x7C00) to a key in VIA"
- Each cell: `QLineEdit` accepting keycode names via `KeycodeResolver`
- **Refresh All** button: GETs all 8 slots
- Hint label: "assign QK_LEADER (0x7C00) to a key in VIA, or use a combo/tap dance slot"
- `signal_leader` drives `update_slot()` to populate cells on GET response
- Tab label: "leader config"

## File Map

### New files
| File | Purpose |
|------|---------|
| `keyboards/keychron/common/leader/eeconfig_leader.h` | EEPROM size/slot constants |
| `keyboards/keychron/common/leader/leader_eeprom.h` | Public API + `leader_def_t` |
| `keyboards/keychron/common/leader/leader_eeprom.c` | EEPROM load/save + matching engine |
| `keyboards/keychron/common/leader/leader_eeprom.mk` | Build system module file |

### Modified files
| File | Change |
|------|--------|
| `keyboards/keychron/common/eeconfig_kb.h` | Add `EECONFIG_BASE_LEADER` chain entry |
| `keyboards/keychron/common/keychron_common.mk` | Conditional include of `leader_eeprom.mk` |
| `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c` | `leader_end_user()` + early termination in `process_record_user()` |
| `keyboards/keychron/q3_max/q3_max_user.c` | `leader_eeprom_init()` call in `keyboard_post_init_user()` |
| `keyboards/keychron/q3_max/qmkata_sysex_handler.c` | SET/GET handlers for `QMKATA_ID_LEADER` |
| `keyboards/keychron/qmkata/QMKata.h` | `QMKATA_ID_LEADER = 13` |
| `qmk-tools/qmk/QMKata/QMKataKeyboard.py` | `signal_leader`, `keyb_get/set_leader`, response handler |
| `qmk-tools/qmk/QMKata/QMKata.py` | `LeaderConfigTab`, `MainWindow` wiring |

## Key Differences from Tap Dance / Combo

| Aspect | Combo / Tap Dance | Leader |
|--------|-------------------|--------|
| QMK runtime array | `key_combos[]` / `tap_dance_actions[]` | None -- leader has no data table |
| `apply()` function | Populates QMK array from RAM | Not needed -- matching engine reads RAM directly |
| Matching | QMK core handles it | Custom matching engine in `leader_end_user()` + `process_record_user()` |
| Early termination | N/A | New feature -- fire on exact match, abort when no prefix matches |
| Hook mechanism | QMK callbacks | `leader_end_user()` (timeout) + `process_record_user()` (per-key) |
