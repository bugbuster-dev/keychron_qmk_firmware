# Leader Sequence → VIA Macro String Support

## Problem

Dynamic leader sequences currently fire a single keycode via `tap_code16()`.
There is no way to have a leader sequence type a string of characters.

## Solution

Reuse the existing VIA macro system. When a leader slot's action keycode falls
in the `QK_MACRO` range (`0x7700`–`0x777F`), call `dynamic_keymap_macro_send()`
instead of `tap_code16()`. This requires one firmware change and zero protocol,
EEPROM, or UI changes.

## User Workflow

1. Define a macro string in VIA's macro editor (e.g. macro 0 = "Hello World").
2. In QMKata's leader tab, set the Action field to `MC_0` (or `0x7700`).
3. Press the leader key followed by the configured sequence → keyboard types
   "Hello World".

## Firmware Change

**File:** `keyboards/keychron/common/leader/leader_eeprom.c`

Add include:
```c
#include "dynamic_keymap.h"
```

Replace line 82:
```c
tap_code16(leader_defs[i].keycode);
```

With:
```c
uint16_t kc = leader_defs[i].keycode;
if (kc >= QK_MACRO && kc <= QK_MACRO_MAX) {
    dynamic_keymap_macro_send(kc - QK_MACRO);
} else {
    tap_code16(kc);
}
```

## What Does NOT Change

- `leader_def_t` struct (still 12 bytes, static assert unchanged)
- EEPROM layout (no new storage)
- QMKata sysex protocol (no new commands)
- QMKata Python code (no changes)
- LeaderConfigTab UI (already supports `MC_0` via KeycodeResolver)

## Dependencies

- VIA enabled (always true for Keychron builds)
- `send_string` enabled (already true in q3_max `info.json`)
- Macro strings configured via VIA's macro editor

## Future Extension

If QMKata macro editing is desired later (bypassing VIA), add:
- `QMKATA_ID_MACRO = 14` sysex command wrapping
  `dynamic_keymap_macro_{get,set}_buffer()`
- Python API methods + Macros tab in QMKata UI
