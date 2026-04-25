# Port Wireless Playground Features — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Port 5 feature groups (RDP fix, Combo/TapDance/Leader EEPROM, QMKATA framework) from the wireless playground repo into the main QMK firmware for the Keychron Q3 Max keyboard.

**Architecture:** Copy-and-Adapt — copy playground files into main repo directories, then mechanically update API calls (GPIO names, function signatures) to match new QMK conventions. Integration via EEPROM chain extension, Raw HID routing (0xFA), and task hook.

**Tech Stack:** C/C++ (QMK firmware, ChibiOS RTOS, STM32F401), Firmata protocol, EEPROM emulation, Raw HID

---

## Important Notes

- **Main repo**: `/home/user/qmk/keychron_qmk_firmware` (branch: `2025q3_q3_max`)
- **Playground repo**: `/home/user/qmk/keychron_qmk_firmware_wireless_playground` (branch: `keychron_q3_max`)
- **WARNING**: The main repo has pre-existing unstaged deletions (1332 keyboard files). When committing, ALWAYS use `git add <specific-files>` — NEVER use `git add -A` or `git add .`.
- **No tests**: QMK firmware has no unit test framework for keyboard-level code. Verification is by successful compilation only: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
- **API migration table** (apply when copying playground files if any of these appear):

| Old (Playground)       | New (Main)                  |
|------------------------|-----------------------------|
| `setPinOutput(x)`      | `gpio_set_pin_output(x)`    |
| `setPinInput(x)`       | `gpio_set_pin_input(x)`     |
| `setPinInputHigh(x)`   | `gpio_set_pin_input_high(x)`|
| `writePinHigh(x)`      | `gpio_write_pin_high(x)`    |
| `writePinLow(x)`       | `gpio_write_pin_low(x)`     |
| `writePin(x,v)`        | `gpio_write_pin(x,v)`       |
| `readPin(x)`           | `gpio_read_pin(x)`          |

---

## Phase 1: RDP Fix

### Task 1.1: Add RDP fix to q3_max.c

**Files:**
- Modify: `keyboards/keychron/q3_max/q3_max.c`

**Step 1: Add register_code16/unregister_code16 overrides**

Append the following code at the end of `keyboards/keychron/q3_max/q3_max.c` (after the `lpm_is_kb_idle` function):

```c
/* Override register_code16/unregister_code16 to send each modifier key as a
 * separate HID report instead of batching all modifier bits in one call.
 *
 * QMK default: do_code16() calls register_weak_mods(all_mods_at_once), which
 * sets all modifier bits in a single HID report. This causes RDP and some
 * remote desktop clients to miss shortcut detection (e.g. Ctrl+Alt+Home for
 * the RDP connection bar) because they expect modifiers to arrive sequentially,
 * as physical keypresses would produce.
 *
 * For non-QK_MODS keycodes the original QMK behaviour is preserved exactly.
 */
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

**Step 2: Verify build**

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
Expected: Build succeeds (linker resolves `register_code16`/`unregister_code16` to our overrides)

**Step 3: Commit**

```bash
git add keyboards/keychron/q3_max/q3_max.c
git commit -m "feat(q3_max): add RDP fix — sequential modifier key sending"
```

---

## Phase 2: Combo/TapDance/Leader EEPROM Modules

### Task 2.1: Copy EEPROM module directories

**Files:**
- Create: `keyboards/keychron/common/combo/` (4 files)
- Create: `keyboards/keychron/common/tap_dance/` (4 files)
- Create: `keyboards/keychron/common/leader/` (4 files)

**Step 1: Copy combo directory**

```bash
cp -r /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/common/combo \
      /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/common/combo
```

**Step 2: Copy tap_dance directory**

```bash
cp -r /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/common/tap_dance \
      /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/common/tap_dance
```

**Step 3: Copy leader directory**

```bash
cp -r /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/common/leader \
      /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/common/leader
```

**Step 4: Verify files copied**

Run: `ls -la keyboards/keychron/common/combo/ keyboards/keychron/common/tap_dance/ keyboards/keychron/common/leader/`
Expected: 4 files in each directory

No API migration needed — these files are high-level EEPROM logic with no GPIO calls.

### Task 2.2: Extend EEPROM chain in eeconfig_kb.h

**Files:**
- Modify: `keyboards/keychron/common/eeconfig_kb.h`

**Step 1: Add combo/tap_dance/leader EEPROM chain entries**

Replace the final `EECONFIG_KB_DATA_SIZE` line (line 77) and add chain entries after `EECONFIG_END_WIRELESS_CONFIG`. The file should end with:

```c
#ifdef DYNAMIC_COMBO_ENABLE
#    include "eeconfig_combo.h"
#    define __EECONFIG_SIZE_COMBO EECONFIG_SIZE_COMBO
#else
#    define __EECONFIG_SIZE_COMBO 0
#endif
#define EECONFIG_BASE_COMBO EECONFIG_END_WIRELESS_CONFIG
#define EECONFIG_END_COMBO (EECONFIG_BASE_COMBO + __EECONFIG_SIZE_COMBO)

#ifdef DYNAMIC_TAP_DANCE_ENABLE
#    include "eeconfig_tap_dance.h"
#    define __EECONFIG_SIZE_TAP_DANCE EECONFIG_SIZE_TAP_DANCE
#else
#    define __EECONFIG_SIZE_TAP_DANCE 0
#endif
#define EECONFIG_BASE_TAP_DANCE EECONFIG_END_COMBO
#define EECONFIG_END_TAP_DANCE (EECONFIG_BASE_TAP_DANCE + __EECONFIG_SIZE_TAP_DANCE)

#ifdef DYNAMIC_LEADER_ENABLE
#    include "eeconfig_leader.h"
#    define __EECONFIG_SIZE_LEADER EECONFIG_SIZE_LEADER
#else
#    define __EECONFIG_SIZE_LEADER 0
#endif
#define EECONFIG_BASE_LEADER EECONFIG_END_TAP_DANCE
#define EECONFIG_END_LEADER (EECONFIG_BASE_LEADER + __EECONFIG_SIZE_LEADER)

#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_LEADER - EECONFIG_BASE_LANGUAGE)
```

This replaces the old line:
```c
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_WIRELESS_CONFIG - EECONFIG_BASE_LANGUAGE)
```

### Task 2.3: Add EEPROM reset calls in eeconfig_kb.c

**Files:**
- Modify: `keyboards/keychron/common/eeconfig_kb.c`

**Step 1: Add combo/tap_dance/leader reset calls**

Add the following blocks before the closing `}` of `eeconfig_init_kb_datablock()` (after the existing `USB_REPORT_INTERVAL_ENABLE` block):

```c
#if defined(DYNAMIC_COMBO_ENABLE) && defined(COMBO_ENABLE)
    extern void combo_eeprom_reset_defaults(void);
    combo_eeprom_reset_defaults();
#endif
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)
    extern void tap_dance_eeprom_reset_defaults(void);
    tap_dance_eeprom_reset_defaults();
#endif
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
    extern void leader_eeprom_reset_defaults(void);
    leader_eeprom_reset_defaults();
#endif
```

### Task 2.4: Add conditional includes in keychron_common.mk

**Files:**
- Modify: `keyboards/keychron/common/keychron_common.mk`

**Step 1: Add combo/tap_dance/leader mk includes**

Append the following at the end of the file (after the `USB_REPORT_INTERVAL_ENABLE` block):

```makefile
ifeq ($(strip $(COMBO_ENABLE)), yes)
include $(KEYCHRON_COMMON_DIR)/combo/combo_eeprom.mk
endif

ifeq ($(strip $(TAP_DANCE_ENABLE)), yes)
include $(KEYCHRON_COMMON_DIR)/tap_dance/tap_dance_eeprom.mk
endif

ifeq ($(strip $(LEADER_ENABLE)), yes)
include $(KEYCHRON_COMMON_DIR)/leader/leader_eeprom.mk
endif
```

**Important:** The KEYCHRON_COMMON_DIR in the main repo is set to `$(TOP_DIR)/keyboards/keychron/common`. The copied .mk files use relative paths like `common/combo/...`. This needs to be consistent. Check the existing .mk patterns — the main repo uses `$(KEYCHRON_COMMON_DIR)/...` which is the full path. The combo/td/leader .mk files define their own dir as `common/combo` etc. This works because VPATH adds the keychron parent. Verify this is correct during the build step.

### Task 2.5: Enable features in q3_max/rules.mk

**Files:**
- Modify: `keyboards/keychron/q3_max/rules.mk`

**Step 1: Add combo/tap_dance/leader enables**

Add the following before the first `include` line:

```makefile
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
LEADER_ENABLE = yes
```

So the file becomes:

```makefile
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
LEADER_ENABLE = yes

include keyboards/keychron/common/wireless/wireless.mk
include keyboards/keychron/common/keychron_common.mk

VPATH += $(TOP_DIR)/keyboards/keychron
```

### Task 2.6: Verify Phase 2 build

**Step 1: Verify build compiles**

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
Expected: Build fails — the default keymap doesn't define `key_combos[]`, `tap_dance_actions[]` etc.

This is expected. The `via` keymap from the playground has these definitions. We need a keymap that provides them. We'll create a `keychron_qmkata` keymap variant in Phase 4. For now, verify the common modules compile correctly by checking the linker errors are specifically about missing combo/td/leader symbols from keymap.c, not about our headers/modules.

**Alternative**: Temporarily disable the 3 features in rules.mk, verify the base build still works, then re-enable them when the keymap is ready in Phase 4.

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default` (with features commented out)
Expected: Build succeeds.

### Task 2.7: Commit Phase 2

```bash
git add keyboards/keychron/common/combo/ \
        keyboards/keychron/common/tap_dance/ \
        keyboards/keychron/common/leader/ \
        keyboards/keychron/common/eeconfig_kb.h \
        keyboards/keychron/common/eeconfig_kb.c \
        keyboards/keychron/common/keychron_common.mk \
        keyboards/keychron/q3_max/rules.mk
git commit -m "feat(q3_max): add EEPROM-backed combo, tap dance, and leader modules"
```

---

## Phase 3: QMKATA Framework

### Task 3.1: Copy qmkata directory

**Files:**
- Create: `keyboards/keychron/qmkata/` (20 files)

**Step 1: Copy qmkata directory**

```bash
cp -r /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/qmkata \
      /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/qmkata
```

**Step 2: Verify files**

Run: `ls keyboards/keychron/qmkata/`
Expected: 20 files (Boards.h, Firmata.cpp, Firmata.h, FirmataConstants.h, FirmataDefines.h, FirmataMarshaller.cpp, FirmataMarshaller.h, FirmataParser.cpp, FirmataParser.h, Print.cpp, Print.h, Printable.h, QMKata.cpp, QMKata.h, qmkata.mk, readme.md, Stream.cpp, Stream.h, WString.cpp, WString.h)

**Step 3: Scan for GPIO API migration**

Search copied files for old GPIO names:
```bash
grep -rn 'setPinOutput\|setPinInput\|writePinHigh\|writePinLow\|writePin\|readPin' keyboards/keychron/qmkata/
```
Expected: No matches (qmkata is high-level C++ Firmata protocol code, no GPIO).

### Task 3.2: Add QMKATA Raw HID routing

**Files:**
- Modify: `keyboards/keychron/common/keychron_raw_hid.c`

**Step 1: Add QMKATA include and routing**

Add after the existing `#ifdef SNAP_CLICK_ENABLE` include block (around line 34):

```c
#ifdef QMKATA_ENABLE
#    include "qmkata/QMKata.h"
#endif
```

Add the QMKATA routing case inside `kc_raw_hid_rx()`, just before the `default:` case (around line 265). Add it as a new case in the switch statement:

```c
#ifdef QMKATA_ENABLE
        case RAWHID_QMKATA_MSG:
            qmkata_recv_data(data, length);
            return true;
#endif
```

**Important signature note:** The main repo's `kc_raw_hid_rx` takes `(uint8_t src, uint8_t *data, uint8_t length)` — it has a `src` parameter that the playground version doesn't have. The QMKATA case doesn't use `src`, so no adaptation needed for the case body itself.

### Task 3.3: Add keychron_task_user() weak hook

**Files:**
- Modify: `keyboards/keychron/common/keychron_task.c`

**Step 1: Add weak keychron_task_user declaration and call**

The main repo already has `keychron_task_kb()` as a weak function (line 117). We need to add a `keychron_task_user()` weak function.

Add before the `keychron_task()` function:

```c
__attribute__((weak)) void keychron_task_user(void) {}
```

Then add a call to it at the end of `keychron_task_kb()` in q3_max.c (not in keychron_task.c, because keychron_task_kb is defined in q3_max.c).

Actually, looking at the playground, `keychron_task_user()` is called from q3_max.c's `keychron_task_kb()` — the q3_max board-level code calls it. So the weak definition goes in keychron_task.c for safety (in case no keyboard defines it), and the real call happens in q3_max.c.

Add to `keyboards/keychron/common/keychron_task.c`, before `keychron_task()`:

```c
__attribute__((weak)) void keychron_task_user(void) {}
```

### Task 3.4: Add QMKATA RGB host buffer rendering

**Files:**
- Modify: `keyboards/keychron/common/keychron_task.c`

**Step 1: Add rgb_matrix_host_buf_render call in rgb_matrix_indicators_keychron**

Add at the end of `rgb_matrix_indicators_keychron()`, just before `return true;` (around line 113):

```c
#ifdef QMKATA_ENABLE
    {
        extern void rgb_matrix_host_buf_render(void);
        rgb_matrix_host_buf_render();
    }
#endif
```

### Task 3.5: Commit Phase 3

```bash
git add keyboards/keychron/qmkata/ \
        keyboards/keychron/common/keychron_raw_hid.c \
        keyboards/keychron/common/keychron_task.c
git commit -m "feat: add QMKATA Firmata framework and integration hooks"
```

---

## Phase 4: Q3 Max QMKATA Integration Files

### Task 4.1: Copy Q3 Max integration files

**Files:**
- Create: `keyboards/keychron/q3_max/q3_max_user.c`
- Create: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`
- Create: `keyboards/keychron/q3_max/qmkata_rgb_matrix_user.c`
- Create: `keyboards/keychron/q3_max/rgb_matrix_user.inc`
- Create: `keyboards/keychron/q3_max/debug_user.c`
- Create: `keyboards/keychron/q3_max/debug_user.h`
- Create: `keyboards/keychron/q3_max/dynld_func.h`

**Step 1: Copy files from playground**

```bash
cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/q3_max_user.c \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/q3_max_user.c

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/qmkata_sysex_handler.c \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/qmkata_sysex_handler.c

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/qmkata_rgb_matrix_user.c \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/qmkata_rgb_matrix_user.c

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/rgb_matrix_user.inc \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/rgb_matrix_user.inc

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/debug_user.c \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/debug_user.c

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/debug_user.h \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/debug_user.h

cp /home/user/qmk/keychron_qmk_firmware_wireless_playground/keyboards/keychron/q3_max/dynld_func.h \
   /home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/dynld_func.h
```

### Task 4.2: Adapt q3_max_user.c for main repo

**Files:**
- Modify: `keyboards/keychron/q3_max/q3_max_user.c`

**Step 1: Fix include path**

The file includes `"keychron_task.h"` — verify this header is on the include path. It should be, because `keychron_common.mk` adds `VPATH` to the common directory.

No changes needed — the include `"keychron_task.h"` is resolved via VPATH.

**Step 2: Verify dip_switch_update_user interaction**

The playground's `q3_max.c` calls `dip_switch_update_user()` and expects it to return `bool`. The main repo's `q3_max.c` also calls `dip_switch_update_user()` but doesn't check the return value:

```c
// Main repo q3_max.c (line 25):
dip_switch_update_user(index, active);
```

The playground's `q3_max_user.c` defines `dip_switch_update_user()` which returns `false` to prevent q3_max.c from setting the default layer when user override is active. But the main repo's q3_max.c ignores the return value and always sets the layer.

**Fix**: Modify `q3_max.c`'s `dip_switch_update_kb` to check the return value:

In `keyboards/keychron/q3_max/q3_max.c`, change:
```c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));
    }
    dip_switch_update_user(index, active);

    return true;
}
```

To:
```c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (!dip_switch_update_user(index, active)) {
        if (index == 0) {
            default_layer_set(1UL << (active ? 2 : 0));
        }
    }

    return true;
}
```

Wait — the playground does it the other way: `dip_switch_update_user` returns `true` to skip default layer set, `false` to allow it. Let me re-read:

```c
// Playground q3_max.c:
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (dip_switch_update_user(index, active)) return true;  // user handled it
    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));
    }
    return true;
}
```

And playground `q3_max_user.c`:
```c
bool dip_switch_update_user(uint8_t index, bool active) {
    // ...
    if (s_keyb_user_macwin_mode != -1) return true;  // user override, skip default
    return false;  // let kb handler set default layer
}
```

So: return `true` from user = "I handled it, skip kb logic". Return `false` = "proceed with kb logic".

**Fix q3_max.c**: Change the `dip_switch_update_kb` function to match the playground pattern:

```c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (dip_switch_update_user(index, active)) return true;

    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));
    }

    return true;
}
```

### Task 4.3: Add keychron_task_user call to q3_max.c

**Files:**
- Modify: `keyboards/keychron/q3_max/q3_max.c`

**Step 1: Add extern declaration and call**

Add after the `#define POWER_ON_LED_DURATION` line:

```c
extern void keychron_task_user(void);
```

Add `keychron_task_user();` at the end of `keychron_task_kb()`, just before the closing `}`:

```c
void keychron_task_kb(void) {
    if (power_on_indicator_timer) {
        if (timer_elapsed32(power_on_indicator_timer) > POWER_ON_LED_DURATION) {
            power_on_indicator_timer = 0;
#ifdef LK_WIRELESS_ENABLE
            gpio_write_pin(BAT_LOW_LED_PIN, !BAT_LOW_LED_PIN_ON_STATE);
#endif

        } else {
#ifdef LK_WIRELESS_ENABLE
            gpio_write_pin(BAT_LOW_LED_PIN, BAT_LOW_LED_PIN_ON_STATE);
#endif
        }
    }

    keychron_task_user();
}
```

### Task 4.4: Wire up q3_max/rules.mk with QMKATA and SRC

**Files:**
- Modify: `keyboards/keychron/q3_max/rules.mk`

**Step 1: Add QMKATA include and SRC entries**

Update to:

```makefile
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
LEADER_ENABLE = yes

include keyboards/keychron/common/wireless/wireless.mk
include keyboards/keychron/common/keychron_common.mk

include keyboards/keychron/qmkata/qmkata.mk

SRC += \
    q3_max_user.c \
    debug_user.c

VPATH += $(TOP_DIR)/keyboards/keychron
```

Note: `qmkata_sysex_handler.c` and `qmkata_rgb_matrix_user.c` are already added by `qmkata.mk` (they're in the SRC list there). Wait — let me re-check. Looking at `qmkata.mk`:

```makefile
SRC += \
qmkata_sysex_handler.c \
qmkata_rgb_matrix_user.c \
$(QMKATA_DIR)/FirmataParser.cpp \
...
```

Yes, `qmkata.mk` includes `qmkata_sysex_handler.c` and `qmkata_rgb_matrix_user.c` — these are keyboard-specific files resolved via VPATH. So they don't need to be in q3_max/rules.mk.

### Task 4.5: Create keymap with combo/td/leader definitions

The combo, tap dance, and leader features need a keymap that defines the required arrays (`key_combos[]`, `tap_dance_actions[]`, etc.). We have two options:

**Option A**: Modify the existing `keymaps/default/keymap.c` to add these definitions.
**Option B**: Copy the playground's `keymaps/via/keymap.c` as a new keymap variant.

We'll go with **Option A** — modify the existing default keymap minimally. The combo/td/leader definitions are simple arrays that QMK requires when the features are enabled.

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/default/keymap.c`

**Step 1: Read and update default keymap**

Add the combo/tap_dance/leader boilerplate after the existing `process_record_user` function. These are the same definitions from the playground's `via/keymap.c`:

```c
////////////////////////////////////////////////////////////////////////////////
// COMBO
////////////////////////////////////////////////////////////////////////////////
#ifdef COMBO_ENABLE
#    ifdef DYNAMIC_COMBO_ENABLE
#        include "combo_eeprom.h"

combo_t key_combos[COMBO_DEF_MAX_SLOTS] = {};

const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)},
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)},
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)},
    {.keys = {KC_V, KC_F, COMBO_END}, .keycode = LCTL(KC_X)},
    {.keys = {KC_X, KC_D, COMBO_END}, .keycode = LCTL(KC_Z)},
};
const uint8_t combo_default_count = sizeof(combo_default_defs) / sizeof(combo_def_t);

#    endif // DYNAMIC_COMBO_ENABLE
#endif     // COMBO_ENABLE

////////////////////////////////////////////////////////////////////////////////
// TAP DANCE
////////////////////////////////////////////////////////////////////////////////
#ifdef TAP_DANCE_ENABLE
#    ifdef DYNAMIC_TAP_DANCE_ENABLE
#        include "tap_dance_eeprom.h"
tap_dance_action_t tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS];
#    endif
#endif

////////////////////////////////////////////////////////////////////////////////
// LEADER KEY
////////////////////////////////////////////////////////////////////////////////
#ifdef LEADER_ENABLE
#    ifdef DYNAMIC_LEADER_ENABLE
#        include "leader_eeprom.h"
#        include "leader.h"

extern uint16_t leader_sequence[5];
extern uint8_t  leader_sequence_size;

static bool leader_already_matched = false;

void leader_end_user(void) {
    if (!leader_already_matched) {
        leader_eeprom_try_match(leader_sequence, leader_sequence_size);
    }
    leader_already_matched = false;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
        leader_already_matched = true;
        leader_end();
        return;
    }

    if (!leader_eeprom_has_prefix(leader_sequence, leader_sequence_size)) {
        leader_end();
    }
}

#    endif // DYNAMIC_LEADER_ENABLE
#endif     // LEADER_ENABLE
```

Also add the combo-leader intercept to `process_record_user`:

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
#if defined(COMBO_ENABLE) && defined(LEADER_ENABLE)
    if (record->keycode == QK_LEADER && record->event.pressed) {
        leader_start();
        return false;
    }
#endif
    if (!process_record_keychron_common(keycode, record)) {
        return false;
    }
    return true;
}
```

### Task 4.6: Verify full Phase 4 build

**Step 1: Build**

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
Expected: Build succeeds. C++ files compile via gnu++14. All symbols resolve.

**Step 2: Check for warnings**

Look for any compiler warnings in the output, particularly:
- Missing `extern "C"` issues
- Implicit function declarations
- Type mismatches

### Task 4.7: Commit Phase 4

```bash
git add keyboards/keychron/q3_max/q3_max_user.c \
        keyboards/keychron/q3_max/qmkata_sysex_handler.c \
        keyboards/keychron/q3_max/qmkata_rgb_matrix_user.c \
        keyboards/keychron/q3_max/rgb_matrix_user.inc \
        keyboards/keychron/q3_max/debug_user.c \
        keyboards/keychron/q3_max/debug_user.h \
        keyboards/keychron/q3_max/dynld_func.h \
        keyboards/keychron/q3_max/q3_max.c \
        keyboards/keychron/q3_max/rules.mk \
        keyboards/keychron/q3_max/ansi_encoder/keymaps/default/keymap.c
git commit -m "feat(q3_max): integrate QMKATA framework with combo/td/leader EEPROM"
```

---

## Phase 5: DEVEL_BUILD Verification

### Task 5.1: Verify DEVEL_BUILD gating

**Files:**
- Read: `keyboards/keychron/qmkata/qmkata.mk`

**Step 1: Check DEVEL_BUILD flag**

Verify that `qmkata.mk` defines `DEVEL_BUILD`:
```makefile
OPT_DEFS += -DDEVEL_BUILD
```

The dynld code in `qmkata_sysex_handler.c` is gated behind `#ifdef DEVEL_BUILD`:
- `_return_cli_error` function
- `_QMKATA_HANDLE_CMD_SET(cli)` body
- Memory read/write CLI commands
- EEPROM read/write CLI commands
- Function call CLI commands
- `__QMK_BUILDDATE__` variable in QMKata.cpp

Verify: `grep -n 'DEVEL_BUILD' keyboards/keychron/q3_max/qmkata_sysex_handler.c keyboards/keychron/qmkata/QMKata.cpp keyboards/keychron/q3_max/q3_max_user.c`

Expected: All dynld and memory-access code is inside `#ifdef DEVEL_BUILD` blocks.

### Task 5.2: Verify build with DEVEL_BUILD=yes

**Step 1: Build with DEVEL_BUILD**

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
Expected: Succeeds (DEVEL_BUILD is already defined in qmkata.mk)

### Task 5.3: Verify build without DEVEL_BUILD

**Step 1: Temporarily remove DEVEL_BUILD from qmkata.mk**

Comment out `-DDEVEL_BUILD` in `qmkata.mk`, rebuild:

```bash
# In qmkata.mk, change:
#   OPT_DEFS += -DQMKATA_ENABLE -DRAW_EPSIZE_QMKATA=64 -DDIP_SWITCH_STATE_STATIC= -DDEVEL_BUILD
# To:
#   OPT_DEFS += -DQMKATA_ENABLE -DRAW_EPSIZE_QMKATA=64 -DDIP_SWITCH_STATE_STATIC=
```

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km default`
Expected: Succeeds — dynld code compiles out cleanly.

**Step 2: Restore DEVEL_BUILD**

Revert the comment-out change.

### Task 5.4: Flash size check

**Step 1: Check build output**

After a successful build, check the firmware size from `qmk compile` output.
Expected: Firmware fits within STM32F401's 512KB flash. Look for the line like:
```
Checking file size of keychron_q3_max_ansi_encoder_default.bin ... [OK]
```

### Task 5.5: Final commit

```bash
# Only if any changes were made during verification
git add -p  # review changes interactively
git commit -m "chore(q3_max): verify DEVEL_BUILD gating and build"
```

---

## Troubleshooting Guide

### Common build errors and fixes

1. **`undefined reference to 'key_combos'`** — COMBO_ENABLE is set but keymap.c doesn't define `key_combos[]`. Add the combo boilerplate to the keymap.

2. **`undefined reference to 'tap_dance_actions'`** — TAP_DANCE_ENABLE is set but keymap.c doesn't define `tap_dance_actions[]`. Add the tap dance boilerplate.

3. **`undefined reference to 'leader_end_user'`** — LEADER_ENABLE is set but keymap.c doesn't define leader hooks. Add the leader boilerplate.

4. **`multiple definition of 'sendchar'`** — QMKata.cpp defines `sendchar()` which conflicts with QMK's default. This is intentional (QMKATA redirects console to Firmata stream). Ensure only one definition is linked. The `CONSOLE_QMKATA = yes` in qmkata.mk should handle this.

5. **`undefined reference to 'battery_get_percentage'`** — `qmkata_sysex_handler.c` includes `battery.h` and calls battery functions. These are provided by the wireless driver (`LK_WIRELESS_ENABLE`). If building without wireless, may need `#ifdef LK_WIRELESS_ENABLE` guards around battery calls.

6. **`undefined reference to 'rgb_config_t'`** — The `dynld_func.h` references `rgb_config_t` and `effect_params_t`. These require `RGB_MATRIX_ENABLE`. Should be fine since q3_max always has RGB.

7. **VLA (Variable Length Array) warnings in qmkata_sysex_handler.c** — The sysex handler uses VLAs like `uint8_t resp[len + 3]`. These are valid C99/C11 but may warn with `-Wpedantic`. Acceptable for firmware code.

8. **`COMBO_END` undefined** — Ensure `process_combo.h` is included. The combo_eeprom.c already includes it.

9. **Include path issues** — If headers can't be found, verify VPATH in rules.mk includes `$(TOP_DIR)/keyboards/keychron`.

### API migration checklist for copied files

After copying each file, run:
```bash
grep -n 'setPinOutput\|setPinInput\|writePinHigh\|writePinLow\|writePin([^)]*)\|readPin' <file>
```
If matches found, apply the migration table from the Important Notes section above.

The only file likely to need migration is none — all copied files are high-level logic. The `writePin` calls in q3_max.c were already migrated to `gpio_write_pin` in the main repo version.
