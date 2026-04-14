# Port Wireless Playground Features into Main QMK Firmware

**Date:** 2026-04-14
**Target branch:** `2025q3_q3_max`
**Target keyboard:** `q3_max` only
**Approach:** Copy-and-Adapt from `keychron_q3_max` branch (wireless playground)

## Summary

Port 5 feature groups from the wireless playground (`keychron_qmk_firmware_wireless_playground` on branch `keychron_q3_max`) into the main repo (`keychron_qmk_firmware` on branch `2025q3_q3_max`):

1. **RDP Fix** — `register_code16()`/`unregister_code16()` overrides for sequential modifier reports
2. **Combo EEPROM** — 16-slot runtime-configurable combo storage
3. **Tap Dance EEPROM** — 8-slot runtime-configurable tap dance storage
4. **Leader EEPROM** — 8-slot runtime-configurable leader sequence storage
5. **QMKATA Framework** — Firmata-based host communication, RGB host buffer, dynamic function loading

## Scope

### In scope
- All features listed above, wired into q3_max only
- API migration from old QMK GPIO/timer names to current names
- EEPROM chain extension
- Raw HID routing for QMKATA (0xFA command)
- C++ build integration for QMKATA/Firmata
- Dynamic function loading gated behind `DEVEL_BUILD`

### Out of scope
- Board definitions (Q12 Max, JIS encoder variants)
- Legacy bluetooth module (`keyboards/keychron/bluetooth/`)
- Changes to shared `common/wireless/` driver files (main already has updated versions)
- Porting to any keyboard other than q3_max

## Architecture

### File Map

```
keyboards/keychron/
├── common/
│   ├── combo/                        ← NEW: copy from playground
│   │   ├── combo_eeprom.c
│   │   ├── combo_eeprom.h
│   │   ├── combo_eeprom.mk
│   │   └── eeconfig_combo.h
│   ├── tap_dance/                    ← NEW: copy from playground
│   │   ├── tap_dance_eeprom.c
│   │   ├── tap_dance_eeprom.h
│   │   ├── tap_dance_eeprom.mk
│   │   └── eeconfig_tap_dance.h
│   ├── leader/                       ← NEW: copy from playground
│   │   ├── leader_eeprom.c
│   │   ├── leader_eeprom.h
│   │   ├── leader_eeprom.mk
│   │   └── eeconfig_leader.h
│   ├── eeconfig_kb.h                 ← MODIFY: extend EEPROM chain
│   ├── eeconfig_kb.c                 ← MODIFY: add reset calls
│   ├── keychron_raw_hid.c            ← MODIFY: add 0xFA QMKATA routing
│   ├── keychron_task.c               ← MODIFY: add keychron_task_user() weak hook
│   └── keychron_common.mk            ← MODIFY: conditional combo/td/leader includes
├── qmkata/                           ← NEW: copy from playground (20 files)
│   ├── QMKata.cpp / QMKata.h
│   ├── Firmata.cpp / Firmata.h
│   ├── FirmataConstants.h / FirmataDefines.h
│   ├── FirmataMarshaller.cpp / FirmataMarshaller.h
│   ├── FirmataParser.cpp / FirmataParser.h
│   ├── Boards.h
│   ├── Print.cpp / Print.h / Printable.h
│   ├── Stream.cpp / Stream.h
│   ├── WString.cpp / WString.h
│   ├── qmkata.mk
│   └── readme.md
└── q3_max/
    ├── q3_max.c                      ← MODIFY: add RDP fix + init hooks
    ├── q3_max_user.c                 ← NEW: copy from playground, adapt
    ├── qmkata_sysex_handler.c        ← NEW: copy from playground, adapt
    ├── qmkata_rgb_matrix_user.c      ← NEW: copy from playground
    ├── rgb_matrix_user.inc           ← NEW: copy from playground
    ├── debug_user.c                  ← NEW: copy from playground
    ├── debug_user.h                  ← NEW: copy from playground
    ├── dynld_func.h                  ← NEW: copy from playground
    ├── rules.mk                      ← MODIFY: add enables + SRC
    └── config.h                      ← MODIFY: if needed for EEPROM
```

### Data Flow

```
Host (USB Raw HID)
  │
  ▼
raw_hid_receive() / via_command_kb()      [keychron_raw_hid.c]
  │
  ├── 0xFA      → qmkata_recv_data()      [NEW: QMKATA Firmata sysex]
  │                 │
  │                 ├── Config get/set     → combo/td/leader EEPROM
  │                 ├── RGB host buffer    → rgb_matrix_host_buf_render()
  │                 ├── Status queries     → battery, dip switch, matrix
  │                 └── Dynamic load       → dynld (DEVEL_BUILD only)
  │
  └── 0xA0-0xAB → kc_raw_hid_rx()        [existing Keychron commands]
```

### EEPROM Chain Extension

```
Current chain (eeconfig_kb.h):
  EECONFIG_BASE_LANGUAGE          (offset 37)
  EECONFIG_BASE_HSUSB_REPORT_RATE
  EECONFIG_BASE_DEBOUNCE
  EECONFIG_BASE_SNAP_CLICK
  EECONFIG_BASE_ANALOG_MATRIX
  EECONFIG_BASE_CUSTOM_RGB
  EECONFIG_BASE_WIRELESS_CONFIG
  EECONFIG_END_WIRELESS_CONFIG    ← current end

New extension:
  EECONFIG_BASE_COMBO             (289 bytes: 16 slots × 18 bytes + 1 header)
  EECONFIG_BASE_TAP_DANCE         (65 bytes:  8 slots ×  8 bytes + 1 header)
  EECONFIG_BASE_LEADER            (97 bytes:  8 slots × 12 bytes + 1 header)
  EECONFIG_KB_DATA_SIZE           (extended to EECONFIG_END_LEADER - 37)
```

## API Migration

Mechanical find-and-replace in copied files where needed:

| Old (Playground)       | New (Main)                  |
|------------------------|-----------------------------|
| `setPinOutput(x)`      | `gpio_set_pin_output(x)`    |
| `setPinInput(x)`       | `gpio_set_pin_input(x)`     |
| `setPinInputHigh(x)`   | `gpio_set_pin_input_high(x)`|
| `writePinHigh(x)`      | `gpio_write_pin_high(x)`    |
| `writePinLow(x)`       | `gpio_write_pin_low(x)`     |
| `writePin(x,v)`        | `gpio_write_pin(x,v)`       |
| `readPin(x)`           | `gpio_read_pin(x)`          |

Most copied files (qmkata/, combo/, td/, leader/) are high-level logic and unlikely to need GPIO migration. The RDP fix and q3_max_user.c may need spot fixes.

## Integration Details

### RDP Fix (q3_max.c)
- Add `register_code16()` and `unregister_code16()` overrides
- Always-on, no toggle
- Self-contained, no dependencies

### Raw HID Routing (keychron_raw_hid.c)
- Add `0xFA` check before existing command dispatch
- Route to `qmkata_recv_data()` (extern declared)
- Gated behind `#ifdef QMKATA_ENABLE`

### Task Hook (keychron_task.c)
- Add `__attribute__((weak)) void keychron_task_user(void) {}` 
- Call from `keychron_task()` after `keychron_common_task()`
- q3_max_user.c implements this to call `qmkata_task()`

### Build (q3_max/rules.mk)
```makefile
COMBO_ENABLE = yes
TAP_DANCE_ENABLE = yes
LEADER_ENABLE = yes
include keyboards/keychron/common/combo/combo_eeprom.mk
include keyboards/keychron/common/tap_dance/tap_dance_eeprom.mk
include keyboards/keychron/common/leader/leader_eeprom.mk
include keyboards/keychron/qmkata/qmkata.mk
SRC += q3_max_user.c qmkata_sysex_handler.c qmkata_rgb_matrix_user.c debug_user.c
```

## Build Phases

### Phase 1: RDP Fix
- Add overrides to q3_max.c
- Verify: compiles, modifier combos work over RDP

### Phase 2: Combo/TapDance/Leader EEPROM
- Copy 3 module directories into common/
- Extend EEPROM chain in eeconfig_kb.h and eeconfig_kb.c
- Add conditional includes in q3_max/rules.mk
- Add init calls in keyboard_post_init_kb or q3_max_user.c
- Verify: compiles, EEPROM reset works, features persist across reboot

### Phase 3: QMKATA Framework
- Copy qmkata/ directory (20 files)
- Add 0xFA routing in keychron_raw_hid.c
- Add keychron_task_user() hook in keychron_task.c
- Verify: compiles with C++, host can send/receive Firmata sysex

### Phase 4: Q3 Max QMKATA Integration
- Copy q3_max_user.c, qmkata_sysex_handler.c, qmkata_rgb_matrix_user.c, rgb_matrix_user.inc, debug_user.c/h, dynld_func.h
- Adapt q3_max_user.c to main's init/task patterns
- Wire up rules.mk
- Verify: full build, host can query/set config via QMKATA

### Phase 5: DEVEL_BUILD Verification
- Verify dynld compiles only under DEVEL_BUILD
- Verify console redirection via Firmata stream
- Verify: build with DEVEL_BUILD=yes, test animation upload from host

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| EEPROM size overflow | +451 bytes. STM32F401 has 8KB emulated EEPROM — plenty of room |
| Raw HID endpoint size conflict | qmkata.mk sets RAW_EPSIZE_QMKATA=64, main uses 32-byte. Verify no conflict |
| C++ link errors | Watch for missing `extern "C"` wrappers on C headers included from .cpp |
| Flash size | QMKATA adds ~15-20KB. STM32F401 has 512KB — verify after build |
| keychron_task_kb() signature | Main returns void, playground returns bool. Keep main's void, add user hook |
