# Dynamic Leader Key Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add EEPROM-backed dynamic leader key sequences (8 slots x 5-key sequence + action keycode) with early termination to the Q3 Max firmware and a corresponding UI tab in the QMKata host tool.

**Architecture:** Mirror the existing `combo_eeprom` / `tap_dance_eeprom` pattern -- new `leader_eeprom.c/h` firmware module with data-driven matching engine, EEPROM chain extension in `eeconfig_kb.h`, SysEx command ID 13, early termination via `post_process_record_user()`, and `LeaderConfigTab` in `QMKata.py` reusing the existing `KeycodeResolver`.

**Tech Stack:** QMK firmware (C, ARM Cortex-M4), PySide6/Qt (Python), Firmata/SysEx HID protocol.

**Design doc:** `docs/plans/2026-04-10-dynamic-leader-design.md`

**Repos:**
- Firmware: `/home/user/qmk/keychron_qmk_firmware` branch `dynamic_combo_tapdance_leader`
- Host tool: `/home/user/qmk/qmk-tools` branch `opencode_dynamic_combo`

---

## Task 1: EEPROM constants header

**Files:**
- Create: `keyboards/keychron/common/leader/eeconfig_leader.h`

**Reference:** `keyboards/keychron/common/tap_dance/eeconfig_tap_dance.h` (7 lines)

**Step 1: Create the header**

```c
// keyboards/keychron/common/leader/eeconfig_leader.h
#pragma once

#define LEADER_DEF_MAX_SLOTS    8
#define LEADER_DEF_MAX_SEQ_LEN  5

// 1 byte magic + 8 slots x 12 bytes (5 x uint16_t sequence + 1 x uint16_t keycode) = 97 bytes
#define EECONFIG_SIZE_LEADER    (1 + LEADER_DEF_MAX_SLOTS * 12)
```

**Step 2: Commit**

```bash
git add keyboards/keychron/common/leader/eeconfig_leader.h
git commit -m "feat: add eeconfig_leader.h with slot/size constants"
```

---

## Task 2: Extend EEPROM chain

**Files:**
- Modify: `keyboards/keychron/common/eeconfig_kb.h:69-79`

**Reference:** The tap dance block at lines 69-79 follows the combo block. Add leader after tap dance using the same pattern.

**Step 1: Add leader block after the tap dance block**

After line 76 (`#define EECONFIG_END_TAP_DANCE ...`) and before line 78 (`#undef EECONFIG_KB_DATA_SIZE`), insert:

```c
#ifdef DYNAMIC_LEADER_ENABLE
#    include "eeconfig_leader.h"
#    define __EECONFIG_SIZE_LEADER EECONFIG_SIZE_LEADER
#else
#    define __EECONFIG_SIZE_LEADER 0
#endif
#define EECONFIG_BASE_LEADER EECONFIG_END_TAP_DANCE
#define EECONFIG_END_LEADER (EECONFIG_BASE_LEADER + __EECONFIG_SIZE_LEADER)
```

**Step 2: Update the final size macro**

Change line 79 from:
```c
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_TAP_DANCE - EECONFIG_BASE_LANGUAGE)
```
to:
```c
#define EECONFIG_KB_DATA_SIZE (EECONFIG_END_LEADER - EECONFIG_BASE_LANGUAGE)
```

**Step 3: Build to verify EEPROM chain compiles**

```bash
make keychron/q3_max/ansi_encoder:via -j$(nproc) 2>&1 | tail -5
```

Expected: Build succeeds (leader module not yet compiled, just header included).

**Step 4: Commit**

```bash
git add keyboards/keychron/common/eeconfig_kb.h
git commit -m "feat: extend EEPROM chain with leader block after tap dance"
```

---

## Task 3: Public API header

**Files:**
- Create: `keyboards/keychron/common/leader/leader_eeprom.h`

**Reference:** `keyboards/keychron/common/tap_dance/tap_dance_eeprom.h` (21 lines)

**Step 1: Create the header**

```c
// keyboards/keychron/common/leader/leader_eeprom.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "eeconfig_leader.h"

typedef struct __attribute__((packed)) {
    uint16_t sequence[LEADER_DEF_MAX_SEQ_LEN];  // up to 5 keycodes, KC_NO (0) terminated
    uint16_t keycode;                            // action keycode when matched
} leader_def_t;

_Static_assert(sizeof(leader_def_t) == 12, "leader_def_t unexpected size");

void leader_eeprom_init(void);
void leader_eeprom_save(void);
void leader_eeprom_set(uint8_t slot, const leader_def_t *def);
void leader_eeprom_get(uint8_t slot, leader_def_t *out);
void leader_eeprom_clear(uint8_t slot);
void leader_eeprom_reset_defaults(void);

// Early termination: check current leader sequence against all slots.
// Call after each keypress during an active leader sequence.
// Returns true if a match was found and the action was fired.
bool leader_eeprom_try_match(const uint16_t *sequence, uint8_t seq_len);

// Returns true if at least one slot has `sequence` as a prefix (more keys possible).
bool leader_eeprom_has_prefix(const uint16_t *sequence, uint8_t seq_len);
```

**Step 2: Commit**

```bash
git add keyboards/keychron/common/leader/leader_eeprom.h
git commit -m "feat: add leader_eeprom.h public API and leader_def_t struct"
```

---

## Task 4: EEPROM logic and matching engine

**Files:**
- Create: `keyboards/keychron/common/leader/leader_eeprom.c`

**Reference:** `keyboards/keychron/common/tap_dance/tap_dance_eeprom.c` (138 lines)

**Key differences from tap dance:**
- No QMK runtime array to populate (no `apply()` function)
- Has a matching engine (`try_match()` and `has_prefix()`) instead
- Uses `tap_code16()` to fire the action keycode (send + release)

**Step 1: Create the implementation**

```c
// keyboards/keychron/common/leader/leader_eeprom.c
#include <string.h>
#include "leader_eeprom.h"
#include "eeconfig_kb.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "quantum.h"

#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)

#    define LDR_EEPROM_MAGIC      0xAE
#    define LDR_EEPROM_MAGIC_ADDR ((uint8_t *)EECONFIG_BASE_LEADER)
#    define LDR_EEPROM_DATA_ADDR  ((uint8_t *)EECONFIG_BASE_LEADER + 1)

// RAM mirror
static leader_def_t leader_defs[LEADER_DEF_MAX_SLOTS];

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void leader_eeprom_save(void) {
    eeprom_update_byte(LDR_EEPROM_MAGIC_ADDR, LDR_EEPROM_MAGIC);
    eeprom_update_block(leader_defs, LDR_EEPROM_DATA_ADDR, sizeof(leader_defs));
}

void leader_eeprom_init(void) {
    uint8_t magic = eeprom_read_byte(LDR_EEPROM_MAGIC_ADDR);
    if (magic == LDR_EEPROM_MAGIC) {
        eeprom_read_block(leader_defs, LDR_EEPROM_DATA_ADDR, sizeof(leader_defs));
    } else {
        leader_eeprom_reset_defaults();
    }
}

void leader_eeprom_set(uint8_t slot, const leader_def_t *def) {
    if (slot >= LEADER_DEF_MAX_SLOTS || !def) return;
    leader_defs[slot] = *def;
    leader_eeprom_save();
}

void leader_eeprom_get(uint8_t slot, leader_def_t *out) {
    if (slot >= LEADER_DEF_MAX_SLOTS || !out) return;
    *out = leader_defs[slot];
}

void leader_eeprom_clear(uint8_t slot) {
    if (slot >= LEADER_DEF_MAX_SLOTS) return;
    memset(&leader_defs[slot], 0, sizeof(leader_def_t));
    leader_eeprom_save();
}

void leader_eeprom_reset_defaults(void) {
    memset(leader_defs, 0, sizeof(leader_defs));
    leader_eeprom_save();
}

// ---------------------------------------------------------------------------
// Matching engine
// ---------------------------------------------------------------------------

bool leader_eeprom_try_match(const uint16_t *sequence, uint8_t seq_len) {
    if (seq_len == 0) return false;

    for (uint8_t i = 0; i < LEADER_DEF_MAX_SLOTS; i++) {
        if (leader_defs[i].sequence[0] == KC_NO) continue;  // slot disabled
        if (leader_defs[i].keycode == KC_NO) continue;       // no action

        // Check if current input matches this slot's sequence exactly
        bool match = true;
        for (uint8_t k = 0; k < seq_len; k++) {
            if (k >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[k] != sequence[k]) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        // Exact match: slot sequence ends right after our input
        // (next position is KC_NO or we've used all 5 slots)
        if (seq_len >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[seq_len] == KC_NO) {
            tap_code16(leader_defs[i].keycode);
            return true;
        }
    }

    return false;
}

bool leader_eeprom_has_prefix(const uint16_t *sequence, uint8_t seq_len) {
    if (seq_len == 0) return true;  // empty sequence is prefix of everything

    for (uint8_t i = 0; i < LEADER_DEF_MAX_SLOTS; i++) {
        if (leader_defs[i].sequence[0] == KC_NO) continue;
        if (leader_defs[i].keycode == KC_NO) continue;

        bool prefix_ok = true;
        for (uint8_t k = 0; k < seq_len; k++) {
            if (k >= LEADER_DEF_MAX_SEQ_LEN || leader_defs[i].sequence[k] != sequence[k]) {
                prefix_ok = false;
                break;
            }
        }
        if (!prefix_ok) continue;

        // This slot's sequence starts with our input -- it's a valid prefix
        // (could be an exact match too, but that's handled by try_match first)
        return true;
    }

    return false;
}

#endif // DYNAMIC_LEADER_ENABLE && LEADER_ENABLE
```

**Step 2: Commit**

```bash
git add keyboards/keychron/common/leader/leader_eeprom.c
git commit -m "feat: add leader_eeprom.c with EEPROM load/save and matching engine"
```

---

## Task 5: Build integration + keymap hooks + init

**Files:**
- Create: `keyboards/keychron/common/leader/leader_eeprom.mk`
- Modify: `keyboards/keychron/common/keychron_common.mk:37` (add leader include after tap dance)
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c:83-88,140` (add `leader_end_user()`, `post_process_record_user()`)
- Modify: `keyboards/keychron/q3_max/q3_max_user.c:24-29` (add `leader_eeprom_init()`)

**Step 1: Create the .mk file**

Reference: `keyboards/keychron/common/tap_dance/tap_dance_eeprom.mk` (7 lines)

```makefile
LEADER_EEPROM_DIR = common/leader
SRC += \
     $(LEADER_EEPROM_DIR)/leader_eeprom.c

VPATH += $(TOP_DIR)/keyboards/keychron/$(LEADER_EEPROM_DIR)

OPT_DEFS += -DDYNAMIC_LEADER_ENABLE
```

**Step 2: Add include to keychron_common.mk**

After the tap dance block (line 37: `endif`), add:

```makefile
ifeq ($(strip $(LEADER_ENABLE)), yes)
include $(TOP_DIR)/keyboards/keychron/$(KEYCHRON_COMMON_DIR)/leader/leader_eeprom.mk
endif
```

**Step 3: Add leader_eeprom_init() to q3_max_user.c**

In `keyboard_post_init_user()`, add before the tap dance init block:

```c
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
    extern void leader_eeprom_init(void);
    leader_eeprom_init();
#endif
```

**Step 4: Add leader_end_user() and post_process_record_user() to keymap.c**

After the tap dance section (line 140), add:

```c
////////////////////////////////////////////////////////////////////////////////
// LEADER KEY
////////////////////////////////////////////////////////////////////////////////
#ifdef LEADER_ENABLE
#    ifdef DYNAMIC_LEADER_ENABLE
#        include "leader_eeprom.h"
#        include "leader.h"

// Access QMK leader globals for early termination matching
extern uint16_t leader_sequence[5];
extern uint8_t  leader_sequence_size;

void leader_end_user(void) {
    // Timeout fallback: try one final exact match
    leader_eeprom_try_match(leader_sequence, leader_sequence_size);
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Early termination: after process_leader() adds the key, check for matches
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    // Check for exact match first
    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
        leader_end();
        return;
    }

    // No prefix matches remain -- end early
    if (!leader_eeprom_has_prefix(leader_sequence, leader_sequence_size)) {
        leader_end();
    }
}

#    endif // DYNAMIC_LEADER_ENABLE
#endif // LEADER_ENABLE
```

**Important note on `post_process_record_user`:** This function is called AFTER the entire `process_record_*` chain completes, including after `process_leader()` has already added the keycode to `leader_sequence`. So when we read `leader_sequence` and `leader_sequence_size`, they reflect the updated state. QMK call chain:
1. `process_record_kb()` -> `process_record_user()` (our existing code, returns true)
2. `process_leader()` -> adds key to buffer, returns false (consumes event)
3. `post_process_record_quantum()` -> `post_process_record_kb()` -> `post_process_record_user()` (our hook)

**Edge case:** When `process_leader()` detects buffer full (5 keys) it calls `leader_end()` which calls `leader_end_user()` which does our final match. Then `post_process_record_user()` runs but `leader_sequence_active()` returns false, so it exits immediately. Safe.

**Step 5: Build**

```bash
make keychron/q3_max/ansi_encoder:via -j$(nproc) 2>&1 | tail -5
```

Expected: Build succeeds.

**Step 6: Commit**

```bash
git add keyboards/keychron/common/leader/leader_eeprom.mk \
       keyboards/keychron/common/keychron_common.mk \
       keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c \
       keyboards/keychron/q3_max/q3_max_user.c
git commit -m "feat: enable DYNAMIC_LEADER_ENABLE, wire leader_eeprom_init and early termination hooks"
```

---

## Task 6: SysEx SET/GET handlers

**Files:**
- Modify: `keyboards/keychron/qmkata/QMKata.h:55` (add `QMKATA_ID_LEADER = 13`)
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c:30-36,151-169,912-944`

**Reference:** The tap dance SysEx handlers at lines 912-944 and dispatch entries at lines 154-155,167-168.

**Step 1: Add ID to QMKata.h**

After line 55 (`QMKATA_ID_TAP_DANCE = 12`), add:
```c
    QMKATA_ID_LEADER      = 13,  // EEPROM-backed leader sequences
```

**Step 2: Add include to qmkata_sysex_handler.c**

After line 36 (`#endif // DYNAMIC_TAP_DANCE_ENABLE`), add:
```c
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
#    include "leader_eeprom.h"
#endif
```

**Step 3: Add dispatch entries**

In the SET block (after line 156), add:
```c
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
        if (id == QMKATA_ID_LEADER) _QMKATA_HANDLE_CMD_SET_FN(leader)(cmd, seqnum, len, buf);
#endif
```

In the GET block (after line 169), add:
```c
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
        if (id == QMKATA_ID_LEADER) _QMKATA_HANDLE_CMD_GET_FN(leader)(cmd, seqnum, len, buf);
#endif
```

**Step 4: Add SET/GET handler implementations**

After the tap dance handler section (after line 944), add:

```c
//------------------------------------------------------------------------------
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)

// SET: buf[0]=slot, buf[1..12]=leader_def_t (5x uint16_t LE sequence + 1x uint16_t LE keycode)
// Short payload (len==1): clears slot.
_QMKATA_HANDLE_CMD_SET(leader) {
    if (len < 1) return;
    uint8_t slot = buf[0];
    if (slot >= LEADER_DEF_MAX_SLOTS) return;
    leader_def_t def = {};
    if (len >= 1 + sizeof(leader_def_t)) {
        memcpy(&def, &buf[1], sizeof(leader_def_t));
    }
    DBG_USR(qmkata, "leader:set slot=%u seq=[%04x,%04x,%04x,%04x,%04x] kc=%04x\n",
            slot, def.sequence[0], def.sequence[1], def.sequence[2],
            def.sequence[3], def.sequence[4], def.keycode);
    leader_eeprom_set(slot, &def);
}

// GET: buf[0]=slot -> response: [seqnum, QMKATA_ID_LEADER, slot, leader_def_t (12B)]
_QMKATA_HANDLE_CMD_GET(leader) {
    if (len < 1) return;
    uint8_t slot = buf[0];
    if (slot >= LEADER_DEF_MAX_SLOTS) return;
    DBG_USR(qmkata, "leader:get slot=%u\n", slot);
    leader_def_t def = {};
    leader_eeprom_get(slot, &def);
    uint8_t resp[3 + sizeof(leader_def_t)];
    resp[0] = seqnum;
    resp[1] = QMKATA_ID_LEADER;
    resp[2] = slot;
    memcpy(&resp[3], &def, sizeof(leader_def_t));
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
}

#endif // DYNAMIC_LEADER_ENABLE && LEADER_ENABLE
```

**Step 5: Build**

```bash
make keychron/q3_max/ansi_encoder:via -j$(nproc) 2>&1 | tail -5
```

Expected: Build succeeds.

**Step 6: Commit**

```bash
git add keyboards/keychron/qmkata/QMKata.h \
       keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "feat: add QMKATA_ID_LEADER SysEx SET/GET handlers"
```

---

## Task 7: Host tool -- signal, get/set methods, response handler

**Files:**
- Modify: `qmk/QMKata/QMKataKeyboard.py:147,171,797-803,1468-1488`

All work in `/home/user/qmk/qmk-tools` on branch `opencode_dynamic_combo`.

**Reference:** The tap dance signal/methods pattern at lines 147,171,797-803,1468-1488.

**Step 1: Add ID_LEADER to QMKataKeybCmd_v0_3**

After line 147 (`ID_TAP_DANCE = 12`), add:
```python
    ID_LEADER = 13
```

**Step 2: Add signal_leader to QMKataKeyboard**

After line 171 (`signal_tap_dance = Signal(int, object)`), add:
```python
    signal_leader = Signal(int, object)  # (slot, leader_def bytes)
```

**Step 3: Add response handler case**

After the tap dance response handler (after line 803), add:
```python
            if buf[0] == QMKataKeybCmd.ID_LEADER:
                # buf: [ID_LEADER, slot, seq0_lo, seq0_hi, ..., seq4_lo, seq4_hi, kc_lo, kc_hi]
                if len(buf) >= 14:  # 1 (id) + 1 (slot) + 12 (leader_def_t)
                    slot = buf[1]
                    self.signal_leader.emit(slot, bytes(buf[2:14]))
                return
```

**Step 4: Add keyb_get_leader and keyb_set_leader**

After `keyb_set_tap_dance` (after line 1488), add:

```python
    def keyb_get_leader(self, slot):
        """Fire-and-forget GET for one leader slot. Response arrives via signal_leader."""
        self.dbg.tr("SYSEX_COMMAND", "keyb_get_leader: slot={}", slot)
        buf = bytes([slot & 0xFF])
        self.send_sysex(QMKataKeybCmd.GET, bytes([QMKataKeybCmd.ID_LEADER]) + buf)

    def keyb_set_leader(self, slot, seq, keycode):
        """SET leader slot: seq is list of up to 5 keycodes, keycode is the action."""
        self.dbg.tr(
            "SYSEX_COMMAND",
            "keyb_set_leader: slot={}, seq={}, keycode={}",
            slot,
            seq,
            keycode,
        )
        payload = struct.pack(self.pack_endian + "B", slot)
        for i in range(5):
            kc = seq[i] if i < len(seq) else 0
            payload += struct.pack(self.pack_endian + "H", kc)
        payload += struct.pack(self.pack_endian + "H", keycode)
        self.send_sysex(
            QMKataKeybCmd.SET, bytes([QMKataKeybCmd.ID_LEADER]) + payload
        )
```

**Step 5: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/QMKataKeyboard.py
git commit -m "feat: add leader signal, get/set methods and response handler"
```

---

## Task 8: Host tool -- LeaderConfigTab UI

**Files:**
- Modify: `qmk/QMKata/QMKata.py`

**Reference:** `TapDanceConfigTab` class at lines 403-478 (same file).

**Step 1: Add LeaderConfigTab class**

Add after the `TapDanceConfigTab` class (after line 478), before `KeybStatusTab`:

```python
# -------------------------------------------------------------------------------
class LeaderConfigTab(QWidget):
    QK_LEADER = 0x7C00
    COLUMNS = ["Slot", "Seq 1", "Seq 2", "Seq 3", "Seq 4", "Seq 5", "Action"]
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
            btn = QPushButton(f"Save")
            btn.setToolTip(
                f"Leader slot {row}\n"
                f"Assign QK_LEADER (0x{self.QK_LEADER:04X}) to a key in VIA"
            )
            btn.clicked.connect(lambda checked=False, r=row: self.save_slot(r))
            self.table.setCellWidget(row, 0, btn)
            for col in range(1, len(self.COLUMNS)):
                edit = QLineEdit()
                edit.setPlaceholderText("KC_NO")
                self.table.setCellWidget(row, col, edit)

        layout.addWidget(self.table)

        btn_row = QHBoxLayout()
        refresh_btn = QPushButton("Refresh All")
        refresh_btn.clicked.connect(self.refresh_all)
        btn_row.addWidget(refresh_btn)
        btn_row.addStretch()
        hint = QLabel(
            f"assign QK_LEADER (0x{self.QK_LEADER:04X}) to a key in VIA, "
            f"or use a combo/tap dance slot"
        )
        hint.setStyleSheet("color: gray; font-style: italic;")
        btn_row.addWidget(hint)
        layout.addLayout(btn_row)

    def refresh_all(self):
        for slot in range(self.NUM_SLOTS):
            self.keyboard.keyb_get_leader(slot)

    def update_slot(self, slot, data):
        """Called when signal_leader fires. data is 12 raw bytes (5x uint16 seq + 1x uint16 kc)."""
        if slot >= self.NUM_SLOTS:
            return
        if len(data) < 12:
            return
        values = struct.unpack(self.keyboard.pack_endian + "HHHHHH", data[:12])
        # values = (seq0, seq1, seq2, seq3, seq4, keycode)
        for col_idx, kc in enumerate(values):
            edit = self.table.cellWidget(slot, col_idx + 1)
            if edit:
                edit.setText(self.resolver.value_to_display(kc) if kc else "")

    def save_slot(self, row):
        """Resolve keycode fields and send SET command for this row."""
        seq = []
        keycode = 0
        error = False
        # Columns 1-5 are sequence, column 6 is action keycode
        for col in range(1, len(self.COLUMNS)):
            edit = self.table.cellWidget(row, col)
            text = edit.text().strip() if edit else ""
            if not text:
                if col <= 5:
                    seq.append(0)
                # keycode stays 0
                continue
            try:
                kc = self.resolver.resolve(text)
                if col <= 5:
                    seq.append(kc)
                else:
                    keycode = kc
                edit.setStyleSheet("")
            except Exception as e:
                edit.setStyleSheet("background: #ffcccc")
                edit.setPlaceholderText(str(e))
                error = True
        if not error:
            self.keyboard.keyb_set_leader(row, seq, keycode)
```

**Step 2: Add tab and signal wiring in MainWindow**

In `MainWindow.init_gui()`:

After the tap dance tab instantiation (line ~598):
```python
        self.leader_tab = LeaderConfigTab(self.keyboard, resolver)
```

After the tap dance addTab (line ~609):
```python
        tab_widget.addTab(self.leader_tab, "leader config")
```

After the tap dance signal connection (line ~667):
```python
        self.keyboard.signal_leader.connect(self.leader_tab.update_slot)
```

**Step 3: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/QMKata.py
git commit -m "feat: add LeaderConfigTab with refresh, per-slot save, and VIA hints"
```

---

## Task 9: End-to-end verification and push

**Step 1: Build firmware**

```bash
cd /home/user/qmk/keychron_qmk_firmware
make keychron/q3_max/ansi_encoder:via -j$(nproc) 2>&1 | tail -5
```

Expected: Build succeeds, binary size reasonable.

**Step 2: Check for non-ASCII characters**

```bash
LC_ALL=C grep -rPn '[^\x00-\x7F]' keyboards/keychron/common/leader/
```

Expected: No output (clean ASCII).

**Step 3: Verify EEPROM budget**

The EEPROM chain should end at EECONFIG_END_LEADER = 398 + 97 = 495.
VIA starts at 495, keymap at 499, encoder at 1723, macros at 1747.
Remaining: 2048 - 1747 = 301 bytes for macros. Acceptable.

**Step 4: Push both repos**

```bash
cd /home/user/qmk/keychron_qmk_firmware
git push origin dynamic_combo_tapdance_leader

cd /home/user/qmk/qmk-tools
git push origin opencode_dynamic_combo
```

**Step 5: Commit tracking note**

Log the final commit SHAs for both repos.
