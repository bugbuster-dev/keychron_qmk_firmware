# Holdseq — Implementation Plan

> Branch: `feature/kbsm-holdseq` (both repos)
> Design: [2026-05-24-holdseq-design.md](2026-05-24-holdseq-design.md)
> Status: ready to execute

---

## Summary

Two-commit firmware sequence + one-commit qmk-tools sequence. No firmware C changes needed (kbsm_env v5 already has `send_string`).

## Commit sequence

### Commit F1 (firmware) — `kbsm/holdseq: design doc + impl plan`

**Files:**
- `docs/plans/2026-05-24-holdseq-design.md`
- `docs/plans/2026-05-24-holdseq-impl.md` (this file)

### Commit Q1 (qmk-tools) — `kbsm_holdseq: add SRAM module example`

**Files (all new):**
- `qmk/QMKata/module_examples/kbsm_holdseq/holdseq.puml` — diagram
- `qmk/QMKata/module_examples/kbsm_holdseq/Holdseq.{c,h}` — generated SM (committed)
- `qmk/QMKata/module_examples/kbsm_holdseq/holdseq_def.h` — user-editable config
- `qmk/QMKata/module_examples/kbsm_holdseq/holdseq_module.c` — adapter
- `qmk/QMKata/module_examples/kbsm_holdseq/README.md` — docs

**Diagram generation:**
```bash
~/.local/bin/statesmith run --lang C99 --no-csx --no-ask \
    qmk-tools/qmk/QMKata/module_examples/kbsm_holdseq/holdseq.puml
```
Plus manual pragma guard per `docs/installing-statesmith.md`.

**Module source skeleton:**
```c
#include "module_api.h"
#include "Holdseq.h"
#include "Holdseq.c"
#include "holdseq_def.h"

typedef struct {
    Holdseq sm;  kbsm_env_t *env;
    uint16_t held_primary; char sequence[HOLDSEQ_MAX_SEQ_LEN]; uint8_t seq_len;
    bool primary_committed_to_host; bool firing;
} holdseq_state_t;

static holdseq_state_t g_state;
static kbsm_t g_machine;

// keycode_to_ascii(), char_to_keycode(), find_primary(), find_sequence()
// holdseq_handle() — per-state dispatch
// holdseq_reset(), machine_get(), module_init(), module_deinit()
// MODULE_HOOK_TABLE
```

Key patterns reused from prior modules:
- `keycode_to_char[]` lookup table (identical to autotext's)
- `firing` guard for self-reentry prevention
- Inline `char` arrays in `holdseq_def_t` (not pointers)
- Explicit `.bss` field init in `module_init()`
- `char_to_keycode()` reverse lookup for replay path

### Commit F2 (firmware) — `kbsm/holdseq: add to FEATURES registry and feature table`

**Files:**
- `emulator/scripts/build_sram_module.py` — add `holdseq` entry
- `quantum/features/README.md` — add row

**FEATURES entry:**
```python
"holdseq": {
    "dir":           "kbsm_holdseq",
    "sources":       ["Holdseq.c", "holdseq_module.c"],
    "headers":       ["Holdseq.h", "holdseq_def.h"],
    "strip_include": '#include "Holdseq.c"\n',
    "output_stem":   "kbsm_holdseq",
},
```

**Verification:**
1. `build_sram_module.py --feature holdseq` succeeds
2. Module ≤ 4096 bytes; hook bitmap `0x200018`
3. All three prior modules (sticky_combo, dyad, autotext) build byte-identical to baselines
4. ANSI + ISO firmware builds unaffected (no firmware C touched)

## Verification gates

| Gate | Expected |
|---|---|
| ANSI firmware | 109438 bytes (unchanged from autotext baseline) |
| ISO firmware | 107278 bytes (unchanged) |
| sticky_combo build | 1260 bytes, byte-identical to baseline |
| dyad build | 972 bytes, byte-identical |
| autotext build | 2020 bytes, byte-identical |
| holdseq build | ~2500 bytes, fit ≤ 4096 |
| Hook bitmap | `0x200018` (init + deinit + kbsm_get_machine) |
| Relocs | 0 ABS32 entries |

## Out of scope

- Renode scenario (deferred)
- EEPROM, QMKata sysex, Unicode, non-QWERTY
- Firmware-tree integration (SRAM module only)
- No firmware C changes (env v5 already satisfies all requirements)
