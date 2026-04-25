# Module-Aware mprintf Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `mprintf(fmt, ...)` to firmware so loadable modules get a runtime-toggleable print function gated by a new `debug_config_user.module` bit, instead of all module `printf` calls being unconditional.

**Architecture:** Firmware exports a real C function `mprintf` that checks `debug_config_user.module` and forwards to `vprintf_` if set. Modules call `mprintf` instead of `printf`; the host's existing `.map`-based PROVIDE symbol resolver hooks them up at module link time, just like `printf` today. Single new firmware source/header pair, one new bit in the existing `debug_config_user_t` union, plus example-module migration. No host code changes (host already resolves any symbol the module references).

**Tech Stack:** C (Cortex-M4, Chibios, lib/printf), QMK, Python (QMKata `.map` resolution).

---

## Context summary

### Why this exists

Today, modules call `printf("[mod] …")`. That symbol resolves via QMKata's `.map` lookup (`ModuleBuild._resolve_symbols` → `PROVIDE(printf = 0x…)`) to QMK's `printf`, which is `xprintf`, which is the real `printf_` from `lib/printf`. There's no gate — if `CONSOLE_ENABLE = yes` and a console viewer is attached, every module print fires. There's no per-module-subsystem opt-in/out flag.

This plan adds `mprintf`: a firmware function with the same calling convention as `printf` but gated by a new `debug_config_user.module` bit (defaults off). Modules migrate from `printf` to `mprintf`; users toggle the bit via QMKata's existing `CONFIG_ID_DEBUG_USER` write path when they want module logs.

### Decisions locked in (from this-session brainstorming)

1. **Default `module` bit OFF.** Consistency with existing `qmkata`/`stats` defaults; matches `dprint` opt-in pattern; avoids surprise console spam.
2. **Real function, not a macro.** Modules link via `.map`-based `PROVIDE(symbol = addr)` which requires actual addresses. Macros have no address.
3. **Variadic forwarding via `vprintf_`.** Already exposed by `lib/printf/src/printf/printf.h:125`; same translation unit as `printf_` so guaranteed to land in the firmware `.map`.
4. **`CONSOLE_ENABLE=no` builds:** `mprintf` becomes an empty function body (still real symbol, still resolvable). Modules link cleanly, prints are no-ops.
5. **`printf` stays available** for module authors who want unconditional output (fatal-error paths). Convention: `mprintf` for diagnostics, `printf` for "user must see this even with debug off". Convention is documented; not enforced.
6. **No QMKata UI auto-discovery.** QMKata exposes debug bits via `CONFIG_ID_DEBUG_USER` raw u32 writes through sysex (`qmkata_sysex_handler.c:626`); users typically write a small Python script. We ship one in `kb_scripts/` to make turning the new bit on/off discoverable.
7. **`debug_config_user_t` is Keychron-Q3-Max-specific.** The new bit lives in `keyboards/keychron/q3_max/debug_user.h`. Other Keychron boards that someday want modules will add their own bit.

### What this plan does NOT touch

- The `debug_config.enable` master gate. `mprintf` only checks its own sub-bit; matches the existing `qmkata`/`stats` pattern at `qmkata_sysex_handler.c` (no master gate at call sites).
- `printf` symbol resolution. Modules can still call `printf` if they want unconditional output.
- Any module-loader / CRC / boot-scan logic from the prior plan.
- Other keyboards' `debug_user.h` (Q3 Max only).

---

## Invariants

- `mprintf` MUST be a real exported C function with a stable address. The host's `.map`-based resolution requires this.
- `mprintf` MUST accept the same calling convention as `printf` (`const char *fmt, ...`). Module authors swap `printf` → `mprintf` with no other changes.
- `mprintf` MUST be safe to call when `CONSOLE_ENABLE=no` (degrades to no-op, doesn't reference symbols that disappear).
- The new `module` bit MUST not change the size of `debug_config_user_t`'s `raw` field (it's a `uint32_t` overlay; a `STATIC_ASSERT_SIZEOF_STRUCT_RAW` already guards this at `debug_user.h:37`).
- Defaults MUST stay zero across `debug_config_user_t` (current state at `debug_user.c:3-5`: `.raw = 0`).

---

## File inventory

### Firmware (`/home/user/qmk/keychron_qmk_firmware`)

- Create: `keyboards/keychron/common/module/module_log.h` — declares `void mprintf(const char *fmt, ...);`.
- Create: `keyboards/keychron/common/module/module_log.c` — defines `mprintf`. Has both `CONSOLE_ENABLE` and `!CONSOLE_ENABLE` branches.
- Modify: `keyboards/keychron/common/module/module_loader.mk` — add `module_log.c` to `SRC`.
- Modify: `keyboards/keychron/q3_max/debug_user.h` — add `bool module : 1;` to `debug_config_user_t`.

### Host (`/home/user/qmk/qmk-tools`)

- Modify: `qmk/QMKata/module_examples/null_module.c` — `printf` → `mprintf`.
- Modify: `qmk/QMKata/module_examples/hooks_template.c` — `printf` → `mprintf` for all the diagnostic prints.
- Modify: `qmk/QMKata/module_examples/combo_layer_filter.c` — only if it has any `printf` calls (verify; may not).
- Create: `qmk/QMKata/kb_scripts/kb_module_debug.py` — small script that flips `debug_config_user.module` on/off so users don't need to compute the raw u32.

No changes to `ModuleBuild.py`, `ModuleTab.py`, or any test (they care about hook tables and CRC, not which print function modules call).

### Plan document

- Create: `/home/user/qmk/keychron_qmk_firmware/docs/plans/2026-04-23-module-aware-mprintf.md` (this file).

---

## Build sequence — task checklist

- [ ] **Task 1:** Add `mprintf` firmware-side (header + source + .mk wire-up) and add `module` bit to `debug_config_user_t`. Build firmware clean.
- [ ] **Task 2:** Hardware spot-check that `mprintf("test")` from a module is silent (default off) and verbose (after flipping the bit). One-time manual test before migrating examples.
- [ ] **Task 3:** Migrate module examples from `printf` to `mprintf`. Rebuild + load each, confirm gated behaviour.
- [ ] **Task 4:** Ship `kb_scripts/kb_module_debug.py` so users can flip the bit by name.
- [ ] **Task 5:** Commit hygiene check (no push).

Tasks 1, 3, and 4 each get their own commit (firmware-only / host-only / host-only respectively). Task 2 is a verification-only step, no commit.

---

## Task 1: Add `mprintf` to firmware

**Files:**
- Create: `keyboards/keychron/common/module/module_log.h`
- Create: `keyboards/keychron/common/module/module_log.c`
- Modify: `keyboards/keychron/common/module/module_loader.mk`
- Modify: `keyboards/keychron/q3_max/debug_user.h`

### Step 1: Create `module_log.h`

```c
/*
   Module logging — mprintf and friends.

   mprintf(fmt, ...) is a printf-compatible function intended for use
   from loadable modules. It is gated by debug_config_user.module:
   when the bit is 0 (default) the call is a no-op; when 1 it forwards
   to vprintf_ (the lib/printf variadic entry point), which routes to
   the same console as xprintf.

   Modules call mprintf for routine diagnostics. They may still call
   printf for unconditional output (e.g. unrecoverable error paths
   where the user must see something regardless of the bit setting).

   The function exists as a real symbol with a stable address even
   when CONSOLE_ENABLE=no; in that build it is a body-less no-op.
   This guarantees the host-side .map-based PROVIDE symbol resolver
   in qmk-tools/qmk/QMKata/ModuleBuild.py can always link modules
   that reference mprintf, regardless of firmware build options.
*/

#pragma once

void mprintf(const char *fmt, ...);
```

### Step 2: Create `module_log.c`

```c
#include "module_log.h"

#include <stdarg.h>

#ifdef CONSOLE_ENABLE
#include "debug_user.h"
#include "printf.h"  /* lib/printf — exposes vprintf_ */

void mprintf(const char *fmt, ...) {
    if (!debug_config_user.module) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf_(fmt, ap);
    va_end(ap);
}
#else
void mprintf(const char *fmt, ...) {
    (void)fmt;
}
#endif
```

Note: `printf.h` is the lib/printf header and is on the firmware's include path because QMK already includes it for its own `xprintf` macro (`quantum/logging/print.h:55`). If the include can't be resolved, fall back to `#include "lib/printf/src/printf/printf.h"` with the path relative to the firmware root — but the unqualified `printf.h` should work because QMK pulls it onto the search path globally.

### Step 3: Wire `module_log.c` into the build

`keyboards/keychron/common/module/module_loader.mk` — find the `SRC +=` lines and add `module_log.c` to the list:

```bash
grep -n "module_loader.c\|module_flash.c\|module_dispatch.c" keyboards/keychron/common/module/module_loader.mk
```

Append `module_log.c` in the same form as the existing entries. Order doesn't matter; pick consistent style.

### Step 4: Add `module` bit to `debug_config_user_t`

`keyboards/keychron/q3_max/debug_user.h` — find this block:

```c
typedef union {
    struct {
        bool    qmkata : 1;
        bool    stats : 1;
        bool    user_anim : 1;
        //bool    via : 1;
        //uint32_t reserved : ..;
    };
    uint32_t raw;
} debug_config_user_t;
```

Add `bool module : 1;` after `user_anim`:

```c
typedef union {
    struct {
        bool    qmkata : 1;
        bool    stats : 1;
        bool    user_anim : 1;
        bool    module : 1;
        //bool    via : 1;
        //uint32_t reserved : ..;
    };
    uint32_t raw;
} debug_config_user_t;
```

The `STATIC_ASSERT_SIZEOF_STRUCT_RAW(debug_config_user_t, ...)` at `debug_user.h:37` will verify the union still fits in 4 bytes (it does — we have plenty of bits left).

### Step 5: Verify default is OFF

`keyboards/keychron/q3_max/debug_user.c` already initialises with `.raw = 0`, so the new bit defaults to 0 automatically. **Do NOT change debug_user.c.** Verify by reading it:

```bash
cat keyboards/keychron/q3_max/debug_user.c
```

Expected: `debug_config_user.raw = 0` initialiser unchanged.

### Step 6: Build firmware

```bash
cd /home/user/qmk/keychron_qmk_firmware
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -15
```

Expected: clean build. New file compiles, no warnings, links cleanly. Binary size grows by ~50-100 bytes (function body + the va_list dance + a `printf_`-resolution pull, but `printf_` is already in the binary so that's free).

### Step 7: Verify `mprintf` made it into the .map

```bash
grep -n "^[^[:space:]]*mprintf\|.text.*mprintf" .build/keychron_q3_max_ansi_encoder_keychron.map | head -5
```

Expected: at least one match showing `mprintf` at some `0x0800xxxx` address. If absent, the symbol got DCE'd by the linker (because no firmware-side caller uses it yet); add a `KEEP(*(.text.mprintf))` to `module_loader.mk`'s LD flags or — easier — make `module_loader.c` reference `&mprintf` once via a static pointer to anchor it. Actually simpler: declare `mprintf` `__attribute__((used))` in `module_log.c`:

```c
void __attribute__((used)) mprintf(const char *fmt, ...) {
    ...
```

This is the canonical way to defeat DCE for a symbol that has no firmware-side caller. Apply this attribute regardless — it's the right thing for an export.

### Step 8: Commit

```bash
cd /home/user/qmk/keychron_qmk_firmware
git add keyboards/keychron/common/module/module_log.h \
        keyboards/keychron/common/module/module_log.c \
        keyboards/keychron/common/module/module_loader.mk \
        keyboards/keychron/q3_max/debug_user.h
git status  # confirm only those 4 files
git commit -m "feat(module): add mprintf gated by debug_config_user.module

Modules now have a runtime-toggleable print function. Default off;
flip debug_config_user.module = 1 (raw bit 3) via the QMKata
CONFIG_ID_DEBUG_USER write path to enable. Forwards to vprintf_;
routes to the same console as xprintf.

Modules can still call printf directly for unconditional output
(error paths). mprintf becomes the convention for routine diagnostics.

Symbol is __attribute__((used)) so the linker keeps it even though
no firmware-side caller exists; modules resolve it via host-side
.map-based PROVIDE symbol resolution at build time."
```

Verify:
```bash
git log -1 --stat
```

Expected: 4 files changed, mostly insertions.

### Step 9: Self-review

- [ ] `mprintf` declared in header with C linkage (no `extern "C"` wrapper needed — header is C-only).
- [ ] Both branches of `#ifdef CONSOLE_ENABLE` produce a real function body.
- [ ] `__attribute__((used))` applied so linker keeps the symbol.
- [ ] `debug_user.h`'s `STATIC_ASSERT_SIZEOF_STRUCT_RAW` still passes (4 bits used out of 32, comfortably fits).
- [ ] `debug_user.c` left alone; `.raw = 0` default zeroes the new bit.
- [ ] Build clean, mprintf appears in `.map`.
- [ ] No host changes in this commit.
- [ ] Did not push.

---

## Task 2: Hardware spot-check (no commit)

This task is a one-shot verification before migrating examples. Do NOT commit anything from this task.

### Step 1: Flash the new firmware

Flash `.build/keychron_q3_max_ansi_encoder_keychron.bin` to the keyboard.

### Step 2: Build a test module that calls only `mprintf`

Edit `qmk/QMKata/module_examples/null_module.c` temporarily — change its single `printf` to `mprintf`. Rebuild via QMKata.

If `mprintf` is undefined when the host tries to compile the module, that's expected: the host adds `extern void mprintf(const char *, ...);` if the module uses it (or the example file declares it inline). For this spot-check, add the declaration manually at the top of the module:

```c
extern void mprintf(const char *fmt, ...);
```

(In Task 3 we'll move this declaration to a shared `module_api.h` for cleanliness.)

### Step 3: Verify gated behaviour

a) Load the modified null_module to slot 0. Expected console output:
```
mod load slot=0 init_fn=0x…
mod load slot=0 init OK rc=0x600dbeef
```
**No `[mod] null_module init` line.** That's `mprintf` being silent because `debug_config_user.module` defaults to 0.

b) Flip the bit. Easiest path: use QMKata's debug-write sysex with a quick script. From the QMKata interactive console (or write a temp `kb_scripts/` file):

```python
# debug_config_user is offset 0 for our purposes — write raw u32 with bit 3 set.
import struct
kb.write_config(CONFIG_ID_DEBUG_USER, struct.pack("<I", 0x08))  # bit 3 = module
```

Actually the QMKata API exposes this differently — check `QMKataKeyboard.write_config` or the `kb` interactive-shell builtins. If unsure, write a one-line script:

```python
# kb_scripts/_temp_enable_module_debug.py
kb.write_config(CONFIG_ID_DEBUG_USER, bytes([0x08, 0x00, 0x00, 0x00]))
```

Whatever the exact API call, the goal is to write the raw u32 `0x00000008` (bit 3 set) to `CONFIG_ID_DEBUG_USER`.

c) Re-load the same module (or trigger any module hook that calls `mprintf`). Expected: `[mod] null_module init` now appears in console.

### Step 4: Revert the temporary edit

```bash
cd /home/user/qmk/qmk-tools
git checkout qmk/QMKata/module_examples/null_module.c
```

Cleans up the local edit so Task 3 has a clean baseline.

### Step 5: Decision gate

If gated behaviour works as expected, proceed to Task 3.

If anything is wrong:
- `mprintf` undefined when host tries to compile the modified module → host's `.map` doesn't have it. Re-check Task 1 Step 7. Most likely fix: `__attribute__((used))` was missed.
- Module loads but `mprintf` always silent regardless of bit → check `debug_user.h` field order; bit 3 might not be `module`.
- Module loads but `mprintf` always loud → `debug_config_user.module` might default to 1 (check `debug_user.c`).
- Hardfault on `mprintf` call → most likely `vprintf_` resolution failed and the module called null. Check map.

Stop and report whichever applies.

---

## Task 3: Migrate module examples

**Files:**
- Modify: `qmk/QMKata/module_examples/null_module.c`
- Modify: `qmk/QMKata/module_examples/hooks_template.c`
- Modify: `qmk/QMKata/module_examples/combo_layer_filter.c` (only if it contains `printf`)
- Modify: `qmk/QMKata/module_api.h` — add `extern void mprintf(const char *fmt, ...);` to the public ABI

### Step 1: Add `mprintf` declaration to `module_api.h`

Find a good location — somewhere near the existing comment about printf/diagnostic paths. There isn't currently a `printf` declaration in `module_api.h` (modules `extern` it themselves), but adding `mprintf` here gives module authors a discoverable surface. Insert near the `MODULE_INIT_MAGIC` block:

```c
/* mprintf: gated diagnostic print. No-op unless debug_config_user.module
   is set on the device. Use this for routine module diagnostics. Modules
   can still extern-declare printf() for unconditional output (error
   paths where the user must see the message regardless). */
extern void mprintf(const char *fmt, ...);
```

### Step 2: Migrate `null_module.c`

```bash
grep -n "printf\|extern.*printf" qmk/QMKata/module_examples/null_module.c
```

Find every `printf("[mod] …")` call and rewrite to `mprintf("[mod] …")`. If there's an `extern int printf(...)` declaration at the top, leave it alone (or remove it if `printf` is no longer used after migration).

If you remove the local `extern int printf(...)`, also drop `#include "module_api.h"` requirements — `module_api.h` now declares `mprintf`, which is what we use.

### Step 3: Migrate `hooks_template.c`

Same pattern. Multiple hook bodies use `printf("[mod] some_hook fired\n")` style — change each to `mprintf`. Drop the local `extern int printf` if it exists and is no longer needed.

### Step 4: Check `combo_layer_filter.c`

```bash
grep -n "printf" qmk/QMKata/module_examples/combo_layer_filter.c
```

If no matches, no change. If matches, migrate.

### Step 5: Build each example to confirm linkage

Easiest path: run the existing build-integration tests, which build every example:

```bash
cd /home/user/qmk/qmk-tools/qmk/QMKata
python3 -m unittest test_module_build_integration -v 2>&1 | tail -10
```

Expected: 5 tests pass. If `_resolve_symbols` complains about `mprintf` being undefined, the firmware build's `.map` doesn't have it — go back to Task 1 Step 7. If `mprintf` resolves cleanly, the test passes.

### Step 6: Hardware spot-check

Load `null_module` → no `[mod]` lines (default off). Flip bit → `[mod] null_module init` appears.

Load `hooks_template` → no per-hook `[mod]` lines until bit set; with bit set every hook fires its line.

### Step 7: Commit

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/module_api.h qmk/QMKata/module_examples/
git status
git commit -m "feat(module): migrate examples from printf to mprintf

Module diagnostic prints now respect the new device-side
debug_config_user.module bit. Default behaviour: silent until the
user enables module debugging via CONFIG_ID_DEBUG_USER write.

Module authors who want unconditional output (e.g. fatal error
paths) can still extern-declare and call printf directly."
```

Verify:
```bash
git log -1 --stat
```

Expected: 2-4 files changed depending on whether `combo_layer_filter.c` had any prints.

---

## Task 4: Ship a `kb_scripts/kb_module_debug.py` helper

**Files:**
- Create: `qmk/QMKata/kb_scripts/kb_module_debug.py`

### Step 1: Find the QMKata config-write API

Read enough of `qmk/QMKata/QMKataKeyboard.py` to find the actual method name for writing `CONFIG_ID_DEBUG_USER`. Likely candidates: `kb.write_config`, `kb.set_debug_user`, or similar. If none exists at the right granularity, write the bytes via a lower-level sysex helper.

```bash
grep -n "DEBUG_USER\|write_config\|set_debug" qmk/QMKata/QMKataKeyboard.py | head
```

If no clean API exists, the script becomes more verbose — accept that for this task; the script is documentation as much as automation.

### Step 2: Create the script

Skeleton (adjust API call based on Step 1 findings):

```python
"""kb_module_debug.py — toggle device-side module debug logging.

Sets debug_config_user.module = 1 (or 0) on the connected keyboard.
When set, mprintf() calls in loadable modules emit to the console;
when clear, they are no-ops.

Usage from QMKata script runner:
    enable = True   # or False to disable
    # then run this script
"""

# Default: enable
enable = True

# Read current value
current = kb.read_config(CONFIG_ID_DEBUG_USER)
current_u32 = int.from_bytes(current, "little") if current else 0

MODULE_BIT = 1 << 3   # debug_config_user.module is bit 3 (after qmkata, stats, user_anim)

if enable:
    new_u32 = current_u32 | MODULE_BIT
else:
    new_u32 = current_u32 & ~MODULE_BIT

kb.write_config(CONFIG_ID_DEBUG_USER, new_u32.to_bytes(4, "little"))
print(f"debug_config_user: 0x{current_u32:08x} -> 0x{new_u32:08x} (module={'on' if enable else 'off'})")
```

If the actual API method names differ, adapt — the goal is "user runs one script, bit flips."

### Step 3: Smoke-test the script

Run it via QMKata's script runner. Expected output:
```
debug_config_user: 0x00000000 -> 0x00000008 (module=on)
```

Trigger any loaded module's `mprintf` and confirm it now prints. Then change `enable = False` at the top, re-run, confirm prints stop.

### Step 4: Commit

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/kb_scripts/kb_module_debug.py
git commit -m "feat(qmkata): script to toggle device-side module debug bit

One-shot helper for flipping debug_config_user.module on/off without
manually computing raw u32 values. Module mprintf() output is gated
by this bit on the firmware side."
```

Verify:
```bash
git log -1 --stat
```

Expected: 1 new file.

---

## Task 5: Commit hygiene check (no push)

### Step 1: Inspect both repos

```bash
cd /home/user/qmk/keychron_qmk_firmware
git status
git log --oneline -3

cd /home/user/qmk/qmk-tools
git status
git log --oneline -5
```

Expected:
- Both clean (untracked-only).
- Firmware HEAD: `feat(module): add mprintf gated by debug_config_user.module`.
- Host HEAD: `feat(qmkata): script to toggle device-side module debug bit`.
- Host `HEAD~1`: `feat(module): migrate examples from printf to mprintf`.

### Step 2: Confirm nothing pushed

```bash
cd /home/user/qmk/keychron_qmk_firmware
git log @{upstream}..HEAD --oneline 2>&1 || echo "(no upstream — local-only branch, expected)"

cd /home/user/qmk/qmk-tools
git log @{upstream}..HEAD --oneline 2>&1 || echo "(no upstream — local-only branch, expected)"
```

Expected: either commits listed (if upstream tracking exists) or a "no upstream" message. Either is acceptable; the absence of pushed commits is what matters.

### Step 3: Stop here

Wait for user instruction on what to do next (push, PR, hold).

---

## Failure modes and fallbacks

- **Task 1 Step 6 fails to build with "undefined reference to vprintf_"** — `lib/printf` symbols may not be exposed by default in this firmware build. Check if the lib/printf source is being compiled in (look for `.build/.../lib/printf/...`). If not, add `SRC += $(QUANTUM_PATH)/util.c lib/printf/src/printf/printf.c` to the .mk, or use `vfprintf` from libc if available, or fall back to a single `xprintf` call (less efficient but works).

- **Task 1 Step 7: `mprintf` not in .map** — `__attribute__((used))` not honoured (unlikely but possible with some optimization flags). Workaround: add an unused reference in `module_loader.c` — e.g. `static const void *_mprintf_anchor __attribute__((used)) = (const void *)&mprintf;`.

- **Task 2 finds gated behaviour broken** — see Task 2 Step 5 diagnostic table. Stop and re-enter systematic-debugging.

- **Task 3 `_resolve_symbols` complains about `mprintf` undefined** — the most likely cause is Task 1 succeeded but the firmware on the device wasn't reflashed. Reflash the firmware built in Task 1 and rerun.

- **Task 4 QMKata API doesn't have `read_config`/`write_config`** — read `QMKataKeyboard.py` source to find the actual method names. If even raw sysex helpers are absent at the right level, the script becomes a smaller demonstration that simply documents the byte sequence to send manually.

---

## Out of scope (carry forward to future plans)

- Per-module log-level granularity (only one bit; no INFO/WARN/ERROR levels).
- Per-slot module debug bits (one bit covers all loaded modules).
- Logging format conventions (e.g. mandatory `[mod-N]` prefix). Module authors decide their own prefixes.
- Porting `mprintf` / the `module` bit to non-Q3-Max keyboards. Each board's `debug_user.h` adds the bit when needed.
- A QMKata GUI tab for debug bits (script-based control is current pattern).
- Unit tests on the firmware side. `mprintf` is a tiny variadic forwarder; covered by the hardware spot-check in Task 2.
- Migrating ALL existing module diagnostics across every Keychron module that exists today — only the example modules ship with this plan; user's own modules migrate at their own pace (and `printf` continues to work as before).
