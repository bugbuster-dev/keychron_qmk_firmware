# Autotext — Implementation Plan

> Branch: `feature/kbsm-autotext` (both repos)
> Design: [2026-05-24-autotext-design.md](2026-05-24-autotext-design.md)
> Status: ready to execute after design approval

---

## Summary

Four commits across both repos:
1. Firmware: design doc + impl plan
2. Firmware: add `send_string` to `kbsm_env_t` + bump ABI to v5
3. qmk-tools: add autotext SRAM module example + sync ABI
4. Firmware: add autotext to FEATURES registry + feature table

## Commit sequence

### Commit F1 (firmware) — `kbsm/autotext: design doc + impl plan`

**Files:**
- `docs/plans/2026-05-24-autotext-design.md`
- `docs/plans/2026-05-24-autotext-impl.md` (this file)

**Verification:** none beyond `git diff --stat` showing exactly 2 added files.

### Commit F2 (firmware) — `kbsm/env: add send_string to kbsm_env_t, bump ABI to v5`

**Files:**
- `keyboards/keychron/common/module/kbsm_env.h` — add `send_string` field
- `keyboards/keychron/common/module/kbsm_env.c` — populate `send_string` with QMK's `send_string`
- `keyboards/keychron/common/module/module_loader.h` — bump `MODULE_HEADER_VERSION` from 4 to 5

**Changes:**

In `kbsm_env.h`:
```c
typedef struct kbsm_env {
    /* ... existing fields ... */
    int      (*xprintf)(const char *fmt, ...);
    void     (*send_string)(const char *str);  /* NEW in v5 */
    void     *extension;
    uintptr_t module_base;
} kbsm_env_t;
```

In `kbsm_env.c`:
```c
static kbsm_env_t g_kbsm_env = {
    /* ... existing fields ... */
    .xprintf       = xprintf,
    .send_string   = send_string,  /* NEW */
    .extension     = NULL,
    .module_base   = 0,
};
```

In `module_loader.h`:
```c
#define MODULE_HEADER_VERSION 5  /* 4→5: added send_string to kbsm_env_t */
```

**Verification:**
- ANSI + ISO firmware builds succeed
- No behavioral change to existing features (just added a new env field)

### Commit Q1 (qmk-tools) — `kbsm_autotext: add SRAM module example + sync ABI`

**Files (all new):**
- `qmk/QMKata/module_examples/kbsm_autotext/autotext.puml`
- `qmk/QMKata/module_examples/kbsm_autotext/Autotext.c` (generated; committed)
- `qmk/QMKata/module_examples/kbsm_autotext/Autotext.h` (generated)
- `qmk/QMKata/module_examples/kbsm_autotext/autotext_def.h`
- `qmk/QMKata/module_examples/kbsm_autotext/autotext_module.c`
- `qmk/QMKata/module_examples/kbsm_autotext/README.md`

**Files (modified):**
- `qmk/QMKata/module_api.h` — add `send_string` to `kbsm_env_t`, bump ABI version

**Diagram:** as specified in design doc.

**Generation procedure:**
```bash
~/.local/bin/statesmith run --lang C99 --no-csx --no-ask \
    qmk-tools/qmk/QMKata/module_examples/kbsm_autotext/autotext.puml
```

Plus manual pragma guard application (per `docs/installing-statesmith.md`).

**Module source skeleton (`autotext_module.c`):**

```c
#include "module_api.h"
#include "Autotext.h"
#include "Autotext.c"
#include "autotext_def.h"

typedef struct {
    Autotext sm;
    kbsm_env_t *env;
    char buffer[AUTOTEXT_MAX_TRIGGER_LEN];
    uint8_t buffer_len;
} autotext_state_t;

static autotext_state_t g_state;
static kbsm_t g_machine;

/* keycode_to_char lookup table (from autotext_def.h) */
/* find_trigger() — linear scan for exact/prefix match */
/* dyad_handle() — main dispatch logic */
/* autotext_reset() — clear buffer, reset SM */
/* machine_get() — return &g_machine */
/* module_init() — register with env, wire up g_machine */
/* module_deinit() — unregister from env */

MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT]              = module_init,
    [MODULE_HOOK_DEINIT]            = module_deinit,
    [MODULE_KBSM_HOOK_GET_MACHINE]  = machine_get,
};
```

**`autotext_def.h` skeleton:**

```c
#pragma once
#include <stdint.h>

#define AUTOTEXT_MAX_TRIGGER_LEN 16

typedef struct {
    const char *trigger;
    const char *expansion;
} autotext_def_t;

static const autotext_def_t module_autotext[] = {
    { "teh",    "the" },
    { "/email", "alice@example.com" },
    { "btw",    "by the way " },
};
#define MODULE_AUTOTEXT_COUNT (sizeof(module_autotext) / sizeof(module_autotext[0]))

/* Keycode-to-ASCII lookup table for QWERTY layout.
 * Users on non-QWERTY layouts should provide their own table. */
static const struct { uint16_t kc; char ch; } keycode_to_char[] = {
    { 0x0004, 'a' }, { 0x0005, 'b' }, { 0x0006, 'c' },
    { 0x0007, 'd' }, { 0x0008, 'e' }, { 0x0009, 'f' },
    { 0x000A, 'g' }, { 0x000B, 'h' }, { 0x000C, 'i' },
    { 0x000D, 'j' }, { 0x000E, 'k' }, { 0x000F, 'l' },
    { 0x0010, 'm' }, { 0x0011, 'n' }, { 0x0012, 'o' },
    { 0x0013, 'p' }, { 0x0014, 'q' }, { 0x0015, 'r' },
    { 0x0016, 's' }, { 0x0017, 't' }, { 0x0018, 'u' },
    { 0x0019, 'v' }, { 0x001A, 'w' }, { 0x001B, 'x' },
    { 0x001C, 'y' }, { 0x001D, 'z' },
    { 0x0027, '1' }, { 0x0028, '2' }, { 0x0029, '3' },
    { 0x002A, '4' }, { 0x002B, '5' }, { 0x002C, '6' },
    { 0x002D, '7' }, { 0x002E, '8' }, { 0x002F, '9' },
    { 0x0030, '0' },
    { 0x0024, ' ' },
    { 0x0034, '/' },
    { 0x0036, '.' },
    { 0x0037, '-' },
    { 0x0038, ',' },
    { 0x0033, ';' },
    { 0x0028, '\n' },
    { 0, 0 } /* sentinel */
};
```

**Verification:** see Commit F3 below — the build verification happens after
the firmware-side dict entry lands.

### Commit F3 (firmware) — `kbsm/autotext: add autotext to FEATURES registry and feature table`

**Files:**
- `emulator/scripts/build_sram_module.py` (add autotext entry)
- `quantum/features/README.md` (add row to "Current features" table)

**Changes to `build_sram_module.py`:**

```python
FEATURES = {
    "sticky_combo": { ... },
    "dyad": { ... },
    "autotext": {
        "dir":           "kbsm_autotext",
        "sources":       ["Autotext.c", "autotext_module.c"],
        "headers":       ["Autotext.h", "autotext_def.h"],
        "strip_include": '#include "Autotext.c"\n',
        "output_stem":   "kbsm_autotext",
    },
}
```

**Changes to `quantum/features/README.md`:**

Add to the "Current features" table:

```markdown
| Autotext | SRAM-module-only; see `qmk-tools/.../kbsm_autotext/` | PRE_TAP | SM (2 states) | ❌ (SRAM module only) |
```

**Verification:**
1. `python3 emulator/scripts/build_sram_module.py --feature sticky_combo`
   succeeds; output byte-identical to baseline (commit F2 verification).
2. `python3 emulator/scripts/build_sram_module.py --feature dyad` succeeds.
3. `python3 emulator/scripts/build_sram_module.py --feature autotext` succeeds.
4. Module size ≤ 4096 bytes.
5. Hook bitmap includes init, deinit, kbsm_get_machine (matches sticky-combo
   bitmap structure: `0x200018`).
6. ANSI + ISO firmware builds unchanged (no firmware C code touched beyond
   the env extension).

## Build/run pre-reqs

- Firmware ELF must exist at `.build/keychron_q3_max_ansi_encoder_keychron.elf`
  for symbol resolution (`g_module_sram`).
- StateSmith CLI installed for `.puml` regeneration. Generated `.c`/`.h`
  files are committed, so end users don't need StateSmith installed.

## Order rationale

- **F1 first** (docs only): reviewers have design context for all subsequent commits
- **F2 second** (env extension): adds `send_string` to `kbsm_env_t` — prerequisite for autotext module
- **Q1 third** (module example): self-contained in qmk-tools repo; can be built standalone after F2 lands
- **F3 last** (dict entry + feature table): activates autotext in the build script; trivial diff that "turns on" the feature

This ordering means commit F2 is reviewable in isolation (pure env extension),
commit Q1 is reviewable in isolation (new module + diagram + README), and
commit F3 is the "wire it together" commit with minimal logic.

## Rollback plan

Each commit is independently revertable:
- F1: drops the docs only; no functional impact
- F2: reverts env extension; sticky-combo and dyad unaffected (they don't use send_string)
- Q1: drops the entire kbsm_autotext example directory
- F3: removes autotext from registry; back to sticky-combo + dyad only

## Out of scope for this plan

- Renode emulator scenario for autotext (deferred)
- EEPROM persistence, QMKata sysex management
- Unicode expansions, case-insensitive matching
- Non-QWERTY layout support
- Dynamic trigger registration at runtime
- Trigger groups / contexts
- Capture groups

## Verification gates (cumulative, after F3)

1. `git diff --stat` per commit shows the expected scope (no accidental edits)
2. Both ANSI and ISO firmware builds succeed and produce byte-identical
   output to pre-branch builds (no firmware C touched beyond env extension)
3. `build_sram_module.py --feature sticky_combo` produces byte-identical
   output to pre-refactor (`kbsm_sticky_combo.bin`)
4. `build_sram_module.py --feature dyad` produces byte-identical
   output to pre-refactor (`kbsm_dyad.bin`)
5. `build_sram_module.py --feature autotext` produces a valid module: size
   ≤ 4096, hook bitmap as expected, sidecar JSON correct
6. No `pipeline` references reintroduced (kbsm grep gate stays clean)
7. No `__pycache__` or other build detritus committed

## Open items for design review (from design doc)

These need answers before Q1 source files are finalized:

1. Keycode-to-ASCII table — hard-coded or user-editable?
2. Backspace handling — truncate or reset?
3. Trigger table size / count limits?
4. The 2-state SM critique — acceptable?
5. Should the design doc explicitly warn about keycode-to-ASCII layer fragility?

All five can be settled during impl execution if not addressed in review;
the design doc lists my recommendations.
