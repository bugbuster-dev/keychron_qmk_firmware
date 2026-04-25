# Merge keychron_q3_max into keychron_q3_max_merge_6f5058f Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Merge the `keychron_q3_max` development branch (QMKata/Firmata features for Q3 Max) into the `keychron_q3_max_merge_6f5058f` branch (which has 60 additional Keychron upstream commits with new keyboards, bug fixes, and features).

**Architecture:** The `keychron_q3_max` branch diverged from a common ancestor (`e5e57f406e`) and made two kinds of changes: (1) deleted ~1487 keyboard files that weren't needed for Q3 Max development, and (2) added QMKata/Firmata protocol support with ~44 new/modified files. The merge branch has 60 upstream Keychron commits adding new keyboards and fixes. The merge strategy is to keep ALL files from the merge branch (preserving upstream keyboards) while incorporating the QMKata/Firmata feature changes from `keychron_q3_max`.

**Tech Stack:** QMK Firmware (C), ChibiOS, Git

---

## Branch Analysis Summary

| Metric | Value |
|--------|-------|
| Current branch | `keychron_q3_max_merge_6f5058f` |
| Source branch | `keychron_q3_max` |
| Common ancestor | `e5e57f406e4281f3a9e61afdb6c3e7d323c0cf7b` |
| Commits in source (to merge) | 63 |
| Commits unique to current | 60 |
| Total conflicted files | 139 |
| Delete-vs-modify conflicts | 136 (files deleted in q3_max, modified in current) |
| True content conflicts | 3 (both branches modified) |

### True Content Conflicts (3 files)

These files were modified on **both** branches and require manual resolution:

1. **`keyboards/keychron/common/keychron_common.c`** — q3_max added QMKata includes, DEVEL_BUILD keypress handling, and RAWHID_QMKATA_MSG case
2. **`keyboards/keychron/common/keychron_common.mk`** — q3_max disabled FACTORY_TEST by default and made it conditional
3. **`tmk_core/protocol/chibios/usb_main.c`** — q3_max added CONSOLE_QMKATA conditionals, virtser_send_nonblock; current branch **significantly refactored** this file (split into usb_driver, usb_endpoints, usb_report_handling)

### Delete-vs-Modify Conflicts (136 files)

The q3_max branch deleted these keyboard directories to reduce clutter during development. The current branch modified some of these files via upstream commits. **Resolution: Keep ALL of them (accept current branch version).**

Affected keyboard families: k10_pro, k10_max, k13_max, k13_pro, k1_max, k2_pro, k4_pro, k5_max, k6_pro, k7_max, q1_max, q5_max, q6_max, q10_max, v1_max, v3_max, v5_max, v6_max, v10_max, lemokey/common, lemokey/l3

### New Files from q3_max (to be added)

- `.devcontainer/devcontainer.json` — Dev container config
- `keyboards/keychron/qmkata/` — Full QMKata/Firmata library (16 files)
- `keyboards/keychron/q3_max/debug_user.c` and `.h`
- `keyboards/keychron/q3_max/dynld_func.h`
- `keyboards/keychron/q3_max/q3_max_user.c`
- `keyboards/keychron/q3_max/qmkata_rgb_matrix_user.c`
- `keyboards/keychron/q3_max/qmkata_sysex_handler.c`
- `keyboards/keychron/q3_max/rgb_matrix_user.inc`

### Modifications from q3_max (non-conflicting)

- `builddefs/common_rules.mk` — cpp file support
- `keyboards/keychron/common/keychron_task.c` — QMKata task call
- `keyboards/keychron/common/wireless/lkbt51.c` — debug include
- `keyboards/keychron/q3_max/config.h` — QMKata config additions
- `keyboards/keychron/q3_max/q3_max.c` — QMKata integration
- `keyboards/keychron/q3_max/rules.mk` — QMKata rules
- `quantum/debounce/sym_eager_pk.c` — debounce config
- `quantum/dip_switch.c` — debug trace
- `quantum/logging/debug.h` — debug user macro
- `quantum/virtser.h` — virtser_send_nonblock declaration
- `tmk_core/protocol.mk` — virtser build flag
- `tmk_core/protocol/usb_descriptor.c` — endpoint size
- `tmk_core/protocol/usb_descriptor.h` — endpoint size defines

---

## Execution Plan

### Task 1: Create a backup branch

**Files:** None (git operation)

**Step 1: Create safety backup of current state**

```bash
git branch backup/keychron_q3_max_merge_6f5058f_pre-merge
```

**Step 2: Verify backup exists**

```bash
git log --oneline -1 backup/keychron_q3_max_merge_6f5058f_pre-merge
```

Expected: Shows `6f5058f7d0 Add Lemokey L1 ISO`

---

### Task 2: Start the merge and resolve delete-vs-modify conflicts (136 files)

**Files:** 136 conflicted files that q3_max deleted but current branch has

**Step 1: Start the merge**

```bash
git merge --no-commit --no-ff keychron_q3_max
```

Expected: Will report 139+ conflicts. This is expected.

**Step 2: Resolve all delete-vs-modify conflicts by keeping current branch versions**

For all files that q3_max deleted but we want to keep, accept the current branch version:

```bash
# Get list of all files deleted in q3_max but present in our branch
git diff --name-only --diff-filter=U | while read f; do
  if [ -z "$(git ls-tree keychron_q3_max -- "$f")" ]; then
    echo "$f"
  fi
done > /tmp/keep_ours.txt

# Accept our version for all these files
while read f; do
  git checkout --ours "$f"
  git add "$f"
done < /tmp/keep_ours.txt
```

**Step 3: Verify only 3 content conflicts remain**

```bash
git diff --name-only --diff-filter=U
```

Expected output (exactly 3 files):
```
keyboards/keychron/common/keychron_common.c
keyboards/keychron/common/keychron_common.mk
tmk_core/protocol/chibios/usb_main.c
```

---

### Task 3: Resolve content conflict in `keychron_common.c`

**Files:**
- Modify: `keyboards/keychron/common/keychron_common.c`

**Context:** The q3_max branch added:
1. `#ifdef QMKATA_ENABLE` includes block (after the `lkbt51.h` include)
2. `#ifdef DEVEL_BUILD` keypress handling block (in the `default:` case of `process_record_keychron_common`)
3. `RAWHID_QMKATA_MSG` case in `via_command_kb`

The current branch also modified this file (snap click, per-key RGB, wireless config features).

**Step 1: Open the file and examine conflict markers**

```bash
grep -n "<<<<<<" keyboards/keychron/common/keychron_common.c
```

**Step 2: Resolve manually**

The resolution strategy is to **start from the current branch version (ours)** and **add the q3_max additions**:

1. After the `#include "lkbt51.h"` block, add:
```c
#ifdef QMKATA_ENABLE
#include "qmkata/QMKata.h"
#include "debug_user.h"
#endif
```

2. In `process_record_keychron_common`, in the `default:` case, wrap the `return true;` with the DEVEL_BUILD block:
```c
        default:
            {
#ifdef DEVEL_BUILD
                static keyevent_t backspace_press_event;
                static keyevent_t enter_press_event;
                if (keycode == KC_BACKSPACE) {
                    backspace_press_event = record->event;
                }
                if (keycode == KC_ESCAPE && backspace_press_event.pressed) {
                    devel_config.pub_keypress = 0;
                    devel_config.process_keypress = 1;

                    uint8_t data[16];
                    data[0] = QMKATA_ID_KEYEVENT;
                    backspace_press_event.pressed = false;
                    memcpy(&data[1], &backspace_press_event, sizeof(keyevent_t));
                    qmkata_send_sysex(QMKATA_CMD_PUB, data, sizeof(keyevent_t)+1);
                }
                if (devel_config.pub_keypress) {
                    uint8_t data[16];
                    data[0] = QMKATA_ID_KEYEVENT;
                    memcpy(&data[1], &record->event, sizeof(keyevent_t));
                    qmkata_send_sysex(QMKATA_CMD_PUB, data, sizeof(keyevent_t)+1);
                }
                if (devel_config.process_keypress == 0) {
                    if (keycode == KC_ENTER) {
                        if (enter_press_event.pressed && record->event.pressed == 0) {
                            enter_press_event = record->event;
                            return true;
                        }
                    }
                    return false;
                }

                if (keycode == KC_ENTER) {
                    enter_press_event = record->event;
                }
#endif
                return true;
            }
```

3. In `via_command_kb`, before the `default:` case, add:
```c
#ifdef QMKATA_ENABLE
        case RAWHID_QMKATA_MSG:
            qmkata_recv_data(data, length);
            break;
#endif
```

**Step 3: Mark as resolved**

```bash
git add keyboards/keychron/common/keychron_common.c
```

---

### Task 4: Resolve content conflict in `keychron_common.mk`

**Files:**
- Modify: `keyboards/keychron/common/keychron_common.mk`

**Context:** The q3_max branch:
1. Commented out `OPT_DEFS += -DFACTORY_TEST_ENABLE` (disabled by default)
2. Made `factory_test.c` conditional on `FACTORY_TEST_ENABLE` being in `OPT_DEFS`

The current branch didn't modify this file, so the conflict is simply a modify/delete mismatch. 

**Step 1: Accept the q3_max version since it's backward-compatible**

The q3_max version makes factory test conditional. Keyboards that define `FACTORY_TEST_ENABLE` will still work; the Q3 Max development just doesn't need it.

```bash
git checkout --theirs keyboards/keychron/common/keychron_common.mk
git add keyboards/keychron/common/keychron_common.mk
```

**IMPORTANT:** Verify this is actually a content conflict and not a delete conflict. If this is a delete conflict (q3_max deleted it), use `--ours` instead. Based on research, this IS a true content conflict — q3_max modified this file, not deleted it.

---

### Task 5: Resolve content conflict in `usb_main.c`

**Files:**
- Modify: `tmk_core/protocol/chibios/usb_main.c`

**Context:** This is the **most complex conflict**. The q3_max branch made targeted additions:
1. `CONSOLE_QMKATA` conditionals (replace `CONSOLE_ENABLE` with `defined(CONSOLE_ENABLE) && !defined(CONSOLE_QMKATA)`)
2. `#ifdef CONSOLE_QMKATA` stub for `console_task()`
3. `virtser_send_nonblock()` function

BUT the current branch **significantly refactored** `usb_main.c`:
- Split into `usb_driver.c`, `usb_endpoints.c`, `usb_report_handling.c`
- The old monolithic code structure that q3_max patched no longer exists in its original form

**Step 1: Accept the current branch version as base**

```bash
git checkout --ours tmk_core/protocol/chibios/usb_main.c
```

**Step 2: Manually port the q3_max changes into the refactored structure**

Find where the equivalent code now lives and apply q3_max's changes:

1. **CONSOLE_QMKATA conditionals**: Search for `CONSOLE_ENABLE` in the refactored `usb_main.c` and the new split files (`usb_driver.c`, `usb_endpoints.c`). Wherever a USB console driver/endpoint is configured, add the `&& !defined(CONSOLE_QMKATA)` condition.

2. **console_task stub**: Add to `usb_main.c` (or wherever `console_task` is now defined):
```c
#ifdef CONSOLE_QMKATA
void console_task(void) {}
#else
// ... existing console_task implementation ...
#endif
```

3. **virtser_send_nonblock**: Add near the existing `virtser_send()`:
```c
void virtser_send_nonblock(const uint8_t c) {
    static bool virtser_timed_out = false;
    const sysinterval_t timeout = virtser_timed_out ? TIME_IMMEDIATE : TIME_MS2I(5);
    const size_t        result  = chnWriteTimeout(&drivers.serial_driver.driver, &c, 1, timeout);
    virtser_timed_out   = (result == 0);
}
```

**Step 3: Search the new file structure to locate exact insertion points**

```bash
grep -rn "CONSOLE_ENABLE" tmk_core/protocol/chibios/
grep -rn "console_task" tmk_core/protocol/chibios/
grep -rn "virtser_send" tmk_core/protocol/chibios/
```

**Step 4: Mark as resolved**

```bash
git add tmk_core/protocol/chibios/usb_main.c
# Also add any other files modified during the port:
# git add tmk_core/protocol/chibios/usb_driver.c  (if modified)
# git add tmk_core/protocol/chibios/usb_endpoints.c  (if modified)
```

---

### Task 6: Verify all conflicts are resolved and complete the merge

**Files:** None (git operations)

**Step 1: Verify no remaining conflicts**

```bash
git diff --name-only --diff-filter=U
```

Expected: No output (empty = no conflicts)

**Step 2: Verify the merge includes all expected new files**

```bash
git diff --cached --name-only --diff-filter=A | grep -E "(qmkata|debug_user|dynld_func|q3_max_user|qmkata_rgb|qmkata_sysex|rgb_matrix_user)" | sort
```

Expected: Should list the new QMKata files.

**Step 3: Verify no keyboard directories were accidentally deleted**

```bash
# Spot-check some keyboard dirs that q3_max had deleted
ls keyboards/keychron/k10_pro/ > /dev/null && echo "k10_pro: OK"
ls keyboards/keychron/k1_max/ > /dev/null && echo "k1_max: OK"
ls keyboards/keychron/v3_max/ > /dev/null && echo "v3_max: OK"
ls keyboards/lemokey/l3/ > /dev/null && echo "l3: OK"
ls keyboards/lemokey/common/ > /dev/null && echo "lemokey_common: OK"
```

Expected: All should print "OK"

**Step 4: Complete the merge commit**

```bash
git commit -m "Merge branch 'keychron_q3_max' into keychron_q3_max_merge_6f5058f

Merges QMKata/Firmata protocol support from keychron_q3_max development branch
while preserving all upstream Keychron keyboard additions and bug fixes.

Key changes from keychron_q3_max:
- QMKata library (Firmata-based protocol for keyboard communication)
- Q3 Max user customizations (debug, dynamic loading, RGB, sysex handler)
- CONSOLE_QMKATA support in USB stack
- virtser_send_nonblock for non-blocking virtual serial
- Conditional factory test compilation

Conflict resolution:
- 136 delete-vs-modify: kept current branch versions (upstream keyboards)
- keychron_common.c: merged QMKata hooks into upstream version
- keychron_common.mk: accepted conditional factory test from q3_max
- usb_main.c: ported q3_max changes into refactored USB stack"
```

---

### Task 7: Build verification

**Step 1: Compile the Q3 Max firmware to verify the merge**

```bash
make keychron/q3_max/ansi_encoder:via
```

Expected: Successful compilation with no errors.

**Step 2: Spot-check a couple other keyboards still compile**

```bash
make keychron/v3_max/ansi_encoder:via
```

Expected: Successful compilation (verifies upstream keyboards weren't broken).

**Step 3: Compare firmware binary**

If you have the pre-merge `.bin` and `.md5`, compare:

```bash
md5sum keychron_q3_max_ansi_encoder_via.bin
cat keychron_q3_max_ansi_encoder_via.6f5058f7.md5
```

Note: The binary will likely differ since we're merging in upstream changes, but compilation success is the key validation.

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| usb_main.c port breaks USB communication | Medium | High | Build + test on actual Q3 Max hardware |
| Factory test disable breaks other keyboards | Low | Medium | Conditional compile; keyboards that define FACTORY_TEST_ENABLE still get it |
| Upstream keyboard files conflict with QMKata | Very Low | Low | QMKata is self-contained in q3_max-specific files |
| Missing QMKata dependency in merged code | Low | Medium | Build verification in Task 7 catches this |

## Notes

- The `keychron_q3_max` branch made a bulk deletion of ~1487 files (non-Q3-Max keyboards) as a development convenience. These deletions should NOT be carried over — the whole point of this merge branch is to keep everything.
- The `usb_main.c` conflict is the trickiest because the current branch significantly refactored the USB stack. The q3_max changes need to be carefully ported into the new file structure.
- The `.devcontainer/devcontainer.json` is a new file from q3_max that will merge cleanly.
