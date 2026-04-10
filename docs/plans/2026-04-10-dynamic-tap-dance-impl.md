# Dynamic Tap Dance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add EEPROM-backed dynamic tap dance (8 slots × tap×1/×2/×3/hold keycodes) to the Q3 Max firmware and a corresponding UI tab in QMKata host tool.

**Architecture:** Mirror the existing `combo_eeprom` pattern exactly — new `tap_dance_eeprom.c/h` firmware module with shared QMK tap dance callbacks, EEPROM chain extension in `eeconfig_kb.h`, SysEx command ID 12, and `TapDanceConfigTab` in `QMKata.py` reusing the existing `KeycodeResolver`.

**Tech Stack:** QMK firmware (C, ARM Cortex-M4), PySide6/Qt (Python), Firmata/SysEx HID protocol.

**Design doc:** `docs/plans/2026-04-10-dynamic-tap-dance-design.md`

---

## Task 1: EEPROM constants header

**Files:**
- Create: `keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h`

**Step 1: Create the header**

```c
// keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h
#pragma once

#define TAP_DANCE_DEF_MAX_SLOTS  8

// 1 byte magic + 8 slots × 8 bytes (4 × uint16_t) = 65 bytes
#define EECONFIG_SIZE_TAP_DANCE  (1 + TAP_DANCE_DEF_MAX_SLOTS * 8)
```

**Step 2: Verify it compiles in isolation**

```bash
echo "#include \"keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h\"" | \
  arm-none-eabi-gcc -x c -I. - -fsyntax-only 2>&1 || echo "header-only, syntax ok"
```

**Step 3: Commit**

```bash
git add keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h
git commit -m "feat: add eeconfig_tap_dance.h with slot/size constants"
```

---

## Task 2: Extend EEPROM chain

**Files:**
- Modify: `keyboards/keychron/common/eeconfig_kb.h`

**Step 1: Append tap dance chain entry after the existing combo block**

Find this block at the bottom of `eeconfig_kb.h`:
```c
#define EECONFIG_BASE_COMBO EECONFIG_END_WIRELESS_CONFIG
#define EECONFIG_END_COMBO (EECONFIG_BASE_COMBO + __EECONFIG_SIZE_COMBO)

#undef EECONFIG_KB_DATA_SIZE
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_COMBO - EECONFIG_BASE_LANGUAGE)
```

Replace with:
```c
#define EECONFIG_BASE_COMBO EECONFIG_END_WIRELESS_CONFIG
#define EECONFIG_END_COMBO (EECONFIG_BASE_COMBO + __EECONFIG_SIZE_COMBO)

#ifdef DYNAMIC_TAP_DANCE_ENABLE
#    include "eeconfig_tap_dance.h"
#    define __EECONFIG_SIZE_TAP_DANCE EECONFIG_SIZE_TAP_DANCE
#else
#    define __EECONFIG_SIZE_TAP_DANCE 0
#endif
#define EECONFIG_BASE_TAP_DANCE EECONFIG_END_COMBO
#define EECONFIG_END_TAP_DANCE  (EECONFIG_BASE_TAP_DANCE + __EECONFIG_SIZE_TAP_DANCE)

#undef EECONFIG_KB_DATA_SIZE
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_TAP_DANCE - EECONFIG_BASE_LANGUAGE)
```

**Step 2: Build firmware to verify no errors**

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km via 2>&1 | tail -10
```
Expected: `[OK]` on all lines, same flash size as before (tap dance not enabled yet).

**Step 3: Commit**

```bash
git add keyboards/keychron/common/eeconfig_kb.h
git commit -m "feat: extend EEPROM chain with tap dance slot"
```

---

## Task 3: Firmware module — `tap_dance_eeprom.h`

**Files:**
- Create: `keyboards/keychron/common/tap_dance/tap_dance_eeprom.h`

**Step 1: Create the header**

```c
// keyboards/keychron/common/tap_dance/tap_dance_eeprom.h
#pragma once

#include <stdint.h>
#include "eeconfig_tap_dance.h"

typedef struct __attribute__((packed)) {
    uint16_t kc1;   // tap ×1  (0 = slot disabled)
    uint16_t kc2;   // tap ×2  (0 = fall through to kc1)
    uint16_t kc3;   // tap ×3  (0 = fall through to kc2)
    uint16_t hold;  // hold    (0 = do nothing)
} tap_dance_def_t;

_Static_assert(sizeof(tap_dance_def_t) == 8, "tap_dance_def_t unexpected size");

void tap_dance_eeprom_init(void);
void tap_dance_eeprom_set(uint8_t slot, const tap_dance_def_t *def);
void tap_dance_eeprom_get(uint8_t slot, tap_dance_def_t *out);
void tap_dance_eeprom_clear(uint8_t slot);
void tap_dance_eeprom_reset_defaults(void);
```

**Step 2: Commit**

```bash
git add keyboards/keychron/common/tap_dance/tap_dance_eeprom.h
git commit -m "feat: add tap_dance_eeprom.h public API and tap_dance_def_t struct"
```

---

## Task 4: Firmware module — `tap_dance_eeprom.c`

**Files:**
- Create: `keyboards/keychron/common/tap_dance/tap_dance_eeprom.c`

**Step 1: Create the implementation**

```c
// keyboards/keychron/common/tap_dance/tap_dance_eeprom.c
#include <string.h>
#include "tap_dance_eeprom.h"
#include "eeconfig_kb.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "process_tap_dance.h"
#include "quantum.h"

#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)

#define TD_EEPROM_MAGIC      0xAD
#define TD_EEPROM_MAGIC_ADDR ((uint8_t *)EECONFIG_BASE_TAP_DANCE)
#define TD_EEPROM_DATA_ADDR  ((uint8_t *)EECONFIG_BASE_TAP_DANCE + 1)

// RAM mirror — tap_dance_actions[i].user_data points into this array
static tap_dance_def_t td_defs[TAP_DANCE_DEF_MAX_SLOTS];

// Tracks the last keycode sent by _td_finished for each slot (needed by _td_reset)
static uint16_t td_last_kc[TAP_DANCE_DEF_MAX_SLOTS];

// Provided by keymap.c
extern tap_dance_action_t tap_dance_actions[];

// ---------------------------------------------------------------------------
// Shared callbacks (used by all 8 slots via user_data)
// ---------------------------------------------------------------------------

static void _td_finished(tap_dance_state_t *state, void *user_data) {
    tap_dance_def_t *def  = (tap_dance_def_t *)user_data;
    uint8_t          slot = (uint8_t)(def - td_defs);
    uint16_t         kc   = KC_NO;

    if (state->pressed && def->hold) {
        kc = def->hold;
    } else {
        switch (state->count) {
            case 1:
                kc = def->kc1;
                break;
            case 2:
                kc = def->kc2 ? def->kc2 : def->kc1;
                break;
            default:
                if (def->kc3)       kc = def->kc3;
                else if (def->kc2)  kc = def->kc2;
                else                kc = def->kc1;
                break;
        }
    }

    td_last_kc[slot] = kc;
    if (kc) register_code16(kc);
}

static void _td_reset(tap_dance_state_t *state, void *user_data) {
    tap_dance_def_t *def  = (tap_dance_def_t *)user_data;
    uint8_t          slot = (uint8_t)(def - td_defs);
    if (td_last_kc[slot]) {
        unregister_code16(td_last_kc[slot]);
        td_last_kc[slot] = KC_NO;
    }
}

// ---------------------------------------------------------------------------
// Populate tap_dance_actions[] from td_defs[]
// ---------------------------------------------------------------------------

static void tap_dance_eeprom_apply(void) {
    for (uint8_t i = 0; i < TAP_DANCE_DEF_MAX_SLOTS; i++) {
        tap_dance_actions[i].fn.on_each_tap       = NULL;
        tap_dance_actions[i].fn.on_dance_finished  = _td_finished;
        tap_dance_actions[i].fn.on_reset           = _td_reset;
        tap_dance_actions[i].fn.on_each_release    = NULL;
        tap_dance_actions[i].user_data             = &td_defs[i];
        td_last_kc[i]                              = KC_NO;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void tap_dance_eeprom_init(void) {
    uint8_t magic = eeprom_read_byte(TD_EEPROM_MAGIC_ADDR);
    if (magic == TD_EEPROM_MAGIC) {
        eeprom_read_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
    } else {
        tap_dance_eeprom_reset_defaults();
        return; // reset_defaults calls apply
    }
    tap_dance_eeprom_apply();
}

void tap_dance_eeprom_set(uint8_t slot, const tap_dance_def_t *def) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS || !def) return;
    td_defs[slot] = *def;
    tap_dance_eeprom_apply();
    eeprom_update_byte(TD_EEPROM_MAGIC_ADDR, TD_EEPROM_MAGIC);
    eeprom_update_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
}

void tap_dance_eeprom_get(uint8_t slot, tap_dance_def_t *out) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS || !out) return;
    *out = td_defs[slot];
}

void tap_dance_eeprom_clear(uint8_t slot) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS) return;
    memset(&td_defs[slot], 0, sizeof(tap_dance_def_t));
    tap_dance_eeprom_apply();
    eeprom_update_byte(TD_EEPROM_MAGIC_ADDR, TD_EEPROM_MAGIC);
    eeprom_update_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
}

void tap_dance_eeprom_reset_defaults(void) {
    memset(td_defs,    0, sizeof(td_defs));
    memset(td_last_kc, 0, sizeof(td_last_kc));
    tap_dance_eeprom_apply();
    eeprom_update_byte(TD_EEPROM_MAGIC_ADDR, TD_EEPROM_MAGIC);
    eeprom_update_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
}

#endif // DYNAMIC_TAP_DANCE_ENABLE && TAP_DANCE_ENABLE
```

**Step 2: Commit**

```bash
git add keyboards/keychron/common/tap_dance/tap_dance_eeprom.c
git commit -m "feat: add tap_dance_eeprom.c with EEPROM load/save and shared callbacks"
```

---

## Task 5: Enable in rules.mk and wire into keymap.c

**Files:**
- Modify: `keyboards/keychron/q3_max/rules.mk`
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`

**Step 1: Add DYNAMIC_TAP_DANCE_ENABLE and module path to rules.mk**

Find the existing `TAP_DANCE_ENABLE = yes` line in `rules.mk`. Add after it:

```makefile
DYNAMIC_TAP_DANCE_ENABLE = yes
ifeq ($(strip $(DYNAMIC_TAP_DANCE_ENABLE)), yes)
    OPT_DEFS += -DDYNAMIC_TAP_DANCE_ENABLE
    VPATH    += keyboards/keychron/common/tap_dance
    SRC      += tap_dance_eeprom.c
endif
```

**Step 2: Update keymap.c tap dance section**

Find this block in `keymap.c`:
```c
#ifdef TAP_DANCE_ENABLE
// Tap Dance definitions
tap_dance_action_t tap_dance_actions[] = {
    // Tap once for Escape, twice for ...
    [TD_ESC] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, LCTL(LALT(KC_HOME))),
};
```

Replace with:
```c
#ifdef TAP_DANCE_ENABLE
#ifdef DYNAMIC_TAP_DANCE_ENABLE
#include "tap_dance_eeprom.h"
// Fixed-size array — QMK's tap dance uses sizeof(tap_dance_actions)
tap_dance_action_t tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS];
#else
// Static tap dance definitions (non-dynamic build)
tap_dance_action_t tap_dance_actions[] = {
    [TD_ESC] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, LCTL(LALT(KC_HOME))),
};
#endif
```

**Step 3: Call `tap_dance_eeprom_init()` from `keyboard_post_init_user()`**

Find `keyboard_post_init_user()` in `keymap.c` (or add it if absent). Add:
```c
void keyboard_post_init_user(void) {
#ifdef DYNAMIC_TAP_DANCE_ENABLE
    tap_dance_eeprom_init();
#endif
#ifdef DYNAMIC_COMBO_ENABLE
    combo_eeprom_init();
#endif
}
```
Note: if `combo_eeprom_init()` is already called here, just add the tap dance call alongside it.

**Step 4: Build and verify**

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km via 2>&1 | tail -15
```
Expected: all `[OK]`, flash size increases slightly (~200 bytes for the new module).

**Step 5: Commit**

```bash
git add keyboards/keychron/q3_max/rules.mk \
        keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c
git commit -m "feat: enable DYNAMIC_TAP_DANCE_ENABLE, wire tap_dance_eeprom_init"
```

---

## Task 6: SysEx command ID and firmware handler

**Files:**
- Modify: `keyboards/keychron/qmkata/QMKata.h`
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`

**Step 1: Add `QMKATA_ID_TAP_DANCE` to `QMKata.h`**

Find:
```c
QMKATA_ID_COMBO          = 11,  // EEPROM-backed combo definitions
```
Add after it:
```c
QMKATA_ID_TAP_DANCE      = 12,  // EEPROM-backed tap dance definitions
```

**Step 2: Include header and dispatch in sysex handler**

At the top of `qmkata_sysex_handler.c`, find the combo include guard:
```c
#    include "combo_eeprom.h"
```
Add alongside:
```c
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)
#    include "tap_dance_eeprom.h"
#endif
```

Find the SET dispatch block (around line 141):
```c
if (id == QMKATA_ID_COMBO) _QMKATA_HANDLE_CMD_SET_FN(combo)(cmd, seqnum, len, buf);
```
Add after it:
```c
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)
if (id == QMKATA_ID_TAP_DANCE) _QMKATA_HANDLE_CMD_SET_FN(tap_dance)(cmd, seqnum, len, buf);
#endif
```

Find the GET dispatch block (around line 151):
```c
if (id == QMKATA_ID_COMBO) _QMKATA_HANDLE_CMD_GET_FN(combo)(cmd, seqnum, len, buf);
```
Add after it:
```c
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)
if (id == QMKATA_ID_TAP_DANCE) _QMKATA_HANDLE_CMD_GET_FN(tap_dance)(cmd, seqnum, len, buf);
#endif
```

**Step 3: Add SET and GET handler implementations**

Append at the bottom of `qmkata_sysex_handler.c` (after the combo handlers):

```c
//------------------------------------------------------------------------------
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)

// SET: buf[0]=slot, buf[1..2]=kc1, buf[3..4]=kc2, buf[5..6]=kc3, buf[7..8]=hold
_QMKATA_HANDLE_CMD_SET(tap_dance) {
    if (len < 1) return;
    uint8_t slot = buf[0];
    tap_dance_def_t def = {};
    if (len >= 1 + sizeof(tap_dance_def_t)) {
        memcpy(&def, &buf[1], sizeof(tap_dance_def_t));
    }
    DBG_USR(qmkata, "tap_dance:set slot=%u kc1=%04x kc2=%04x kc3=%04x hold=%04x\n",
            slot, def.kc1, def.kc2, def.kc3, def.hold);
    tap_dance_eeprom_set(slot, &def);
}

// GET: buf[0]=slot -> response: [seqnum, QMKATA_ID_TAP_DANCE, slot, tap_dance_def_t (8B)]
_QMKATA_HANDLE_CMD_GET(tap_dance) {
    if (len < 1) return;
    uint8_t slot = buf[0];
    DBG_USR(qmkata, "tap_dance:get slot=%u\n", slot);
    tap_dance_def_t def = {};
    tap_dance_eeprom_get(slot, &def);
    uint8_t resp[3 + sizeof(tap_dance_def_t)];
    resp[0] = seqnum;
    resp[1] = QMKATA_ID_TAP_DANCE;
    resp[2] = slot;
    memcpy(&resp[3], &def, sizeof(tap_dance_def_t));
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
}

#endif // DYNAMIC_TAP_DANCE_ENABLE && TAP_DANCE_ENABLE
```

**Step 4: Build and verify**

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km via 2>&1 | tail -10
```
Expected: all `[OK]`.

**Step 5: Commit**

```bash
git add keyboards/keychron/qmkata/QMKata.h \
        keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "feat: add QMKATA_ID_TAP_DANCE SysEx SET/GET handlers"
```

---

## Task 7: Host tool — keyboard communication

**Files:**
- Modify: `qmk-tools/qmk/QMKata/QMKataKeyboard.py`

**Step 1: Add `ID_TAP_DANCE` constant**

Find:
```python
ID_COMBO = 11
```
Add after:
```python
ID_TAP_DANCE = 12
```

**Step 2: Add `signal_tap_dance`**

Find:
```python
signal_combo = pyqtSignal(int, object)
```
Add after:
```python
signal_tap_dance = pyqtSignal(int, object)  # (slot, tap_dance_def bytes)
```

**Step 3: Add response handler case**

Find the `ID_COMBO` case in `sysex_response_handler` (look for `if id == ID_COMBO`).
Add an analogous case after it:
```python
elif id == ID_TAP_DANCE:
    # resp: [seqnum, ID_TAP_DANCE, slot, kc1_lo, kc1_hi, kc2_lo, kc2_hi,
    #                                    kc3_lo, kc3_hi, hold_lo, hold_hi]
    if len(data) >= 11:
        slot = data[2]
        self.signal_tap_dance.emit(slot, bytes(data[3:11]))
```

**Step 4: Add `keyb_get_tap_dance` and `keyb_set_tap_dance`**

Find `keyb_get_combo` and `keyb_set_combo`. Add analogous methods after them:

```python
def keyb_get_tap_dance(self, slot):
    """Fire-and-forget GET for one tap dance slot. Response arrives via signal_tap_dance."""
    buf = bytes([slot])
    self.send_sysex(QMKATA_CMD_GET, bytes([ID_TAP_DANCE]) + buf)

def keyb_set_tap_dance(self, slot, kc1, kc2, kc3, hold):
    """SET tap dance slot and save to EEPROM."""
    import struct
    payload = struct.pack('<BHHHH', slot, kc1, kc2, kc3, hold)
    self.send_sysex(QMKATA_CMD_SET, bytes([ID_TAP_DANCE]) + payload)
```

**Step 5: Verify syntax**

```bash
python -m py_compile qmk/QMKata/QMKataKeyboard.py && echo "OK"
```
Expected: `OK`

**Step 6: Commit**

```bash
git -C /home/user/qmk/qmk-tools add qmk/QMKata/QMKataKeyboard.py
git -C /home/user/qmk/qmk-tools commit -m "feat: add tap dance signal, get/set methods and response handler"
```

---

## Task 8: Host tool — `TapDanceConfigTab` UI

**Files:**
- Modify: `qmk-tools/qmk/QMKata/QMKata.py`

**Step 1: Add `TapDanceConfigTab` class**

Add the class after `ComboConfigTab` (find `class ComboConfigTab` and place the new class directly below it). Full implementation:

```python
class TapDanceConfigTab(QWidget):
    COLUMNS = ['Slot', 'Tap×1', 'Tap×2', 'Tap×3', 'Hold']
    NUM_SLOTS = 8

    def __init__(self, keyboard, resolver, parent=None):
        super().__init__(parent)
        self.keyboard = keyboard
        self.resolver = resolver
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)

        self.table = QTableWidget(self.NUM_SLOTS, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.verticalHeader().setVisible(False)

        for row in range(self.NUM_SLOTS):
            # Slot number (read-only)
            slot_item = QTableWidgetItem(str(row))
            slot_item.setFlags(slot_item.flags() & ~Qt.ItemIsEditable)
            self.table.setItem(row, 0, slot_item)
            # Keycode fields
            for col in range(1, len(self.COLUMNS)):
                edit = QLineEdit()
                edit.setPlaceholderText('KC_NO')
                self.table.setCellWidget(row, col, edit)
            # Save button
            btn = QPushButton('Save')
            btn.clicked.connect(lambda checked, r=row: self.save_slot(r))
            self.table.setCellWidget(row, 0, btn)

        layout.addWidget(self.table)

        btn_row = QHBoxLayout()
        refresh_btn = QPushButton('Refresh All')
        refresh_btn.clicked.connect(self.refresh_all)
        btn_row.addWidget(refresh_btn)
        btn_row.addStretch()
        layout.addLayout(btn_row)

    def refresh_all(self):
        for slot in range(self.NUM_SLOTS):
            self.keyboard.keyb_get_tap_dance(slot)

    def update_slot(self, slot, data):
        """Called when signal_tap_dance fires. data is 8 raw bytes (4×uint16 LE)."""
        import struct
        if len(data) < 8:
            return
        kc1, kc2, kc3, hold = struct.unpack('<HHHH', data[:8])
        values = [kc1, kc2, kc3, hold]
        for col_idx, kc in enumerate(values):
            edit = self.table.cellWidget(slot, col_idx + 1)
            if edit:
                edit.setText(self.resolver.value_to_display(kc) if kc else '')

    def save_slot(self, row):
        """Resolve keycode fields and send SET command for this row."""
        kcs = []
        error = False
        for col in range(1, len(self.COLUMNS)):
            edit = self.table.cellWidget(row, col)
            text = edit.text().strip() if edit else ''
            if not text:
                kcs.append(0)
                continue
            try:
                kc = self.resolver.resolve(text)
                kcs.append(kc)
                edit.setStyleSheet('')
            except Exception as e:
                edit.setStyleSheet('background: #ffcccc')
                edit.setPlaceholderText(str(e))
                error = True
        if not error:
            self.keyboard.keyb_set_tap_dance(row, *kcs)
```

**Step 2: Wire into `MainWindow`**

Find where `ComboConfigTab` is instantiated and added to the tab widget. Add the tap dance tab alongside it:

```python
self.tap_dance_tab = TapDanceConfigTab(self.keyboard, self.resolver)
self.tab_widget.addTab(self.tap_dance_tab, 'Tap Dance')
self.keyboard.signal_tap_dance.connect(self.tap_dance_tab.update_slot)
```

**Step 3: Verify syntax**

```bash
python -m py_compile qmk/QMKata/QMKata.py && echo "OK"
```
Expected: `OK`

**Step 4: Commit**

```bash
git -C /home/user/qmk/qmk-tools add qmk/QMKata/QMKata.py
git -C /home/user/qmk/qmk-tools commit -m "feat: add TapDanceConfigTab with refresh and per-slot save"
```

---

## Task 9: End-to-end verification

**Step 1: Final firmware build**

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km via 2>&1 | tail -10
```
Expected: all `[OK]`. Note flash size — should be ~88.6 KB + ~300 bytes.

**Step 2: Verify host tool files compile clean**

```bash
python -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKata.py && \
python -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeyboard.py && \
echo "ALL OK"
```
Expected: `ALL OK`

**Step 3: Flash firmware and test manually**

1. Flash `.build/keychron_q3_max_ansi_encoder_via.bin` to keyboard
2. Launch QMKata: `python QMKata.py --firmware-path /home/user/qmk/keychron_qmk_firmware`
3. Open **Tap Dance** tab → click **Refresh All** → all 8 slots should show blank (empty defaults)
4. Set slot 0: Tap×1=`KC_A`, Tap×2=`KC_B`, Tap×3=`KC_C`, Hold=`KC_LSFT` → click **Save**
5. Click **Refresh All** → slot 0 should show back `KC_A`, `KC_B`, `KC_C`, `KC_LSFT`
6. Reboot keyboard → Refresh All again → values should persist (EEPROM confirmed)
7. In VIA, remap a key to `TD(0)` → test tap×1 sends A, tap×2 sends B, hold sends Shift

**Step 4: Final commits and push**

```bash
# Firmware repo
git -C /home/user/qmk/keychron_qmk_firmware push -u origin dynamic_combo_tapdance_leader

# Host tool repo
git -C /home/user/qmk/qmk-tools push
```
