# SRAM-Loadable Pipeline Modules

> Status: implemented in feat/sram-modules branch (Phases 1-6).
> Volatility: lost on reset. Use for iteration, not permanent features.

---

## Why

Flash modules (the existing path in `keyboards/keychron/common/module/`)
are great for shipping permanent features that survive power cycle, but
they cost a flash erase + program on every iteration. SRAM modules
solve the iteration-friction case: edit a `.c` source, build, upload to
SRAM, test, repeat — zero flash wear, sub-second turnaround.

Both targets share:

- The same `module_header_t` layout (32 bytes, MODL magic, CRC32).
- The same hook table format and one-owner-per-hook semantics.
- The same host-side relocation pass (`apply_relocations_and_crc`).
- The same QMKata upload protocol (slot ID alone discriminates target).

What's different in SRAM:

- The slot lives in a static `g_module_sram[]` buffer in `.bss`, sized
  by `MODULE_SRAM_TOTAL_SIZE` (4 KB default = one slot at ID 8).
- No erase / sector dance — `memcpy`, then `DSB`/`ISB` and jump.
- Volatile — `module_boot_scan()` does not look at SRAM (it's empty
  after reset by definition); modules must be re-uploaded after power-on.

## Memory budget

A 4 KB SRAM module on Q3 Max (STM32F401xC, 64 KB SRAM total) consumes
~4 KB of `.bss` and shrinks `.heap` correspondingly. After Phase 3 with
all features on (vim_modal + sticky_combo + module loader + pipeline env),
the build still has ~30 KB heap remaining. Growing `MODULE_SRAM_TOTAL_SIZE`
past what the keyboard can spare triggers a hard link error:

```
arm-none-eabi-ld: region 'ram0' overflowed by N bytes
```

This is by design — the linker is the budget enforcer. Don't shrink
`MODULE_SRAM_TOTAL_SIZE` below 4 KB without first verifying real heap
usage on hardware.

## Enabling SRAM modules

In the keymap's `rules.mk`:

```makefile
KEY_PROCESSING_SM_ENABLE = yes
MODULE_LOADER_ENABLE     = yes   # (already enabled on Q3 Max)
MODULE_SRAM_ENABLE       = yes
```

To override the SRAM budget (multiples of slot size):

```makefile
OPT_DEFS += -DMODULE_SRAM_TOTAL_SIZE=8192   # 2 slots × 4 KB
```

## Slot ID space

| Slot IDs | Target | Notes |
|----------|--------|-------|
| 0–7      | Flash  | Existing module slots in Sectors 2–3 |
| 8        | SRAM   | First (and only by default) SRAM slot |
| 9+       | SRAM   | Available with larger `MODULE_SRAM_TOTAL_SIZE` |

Host (`qmk-tools/QMKata/ModuleTab.py`) routes slot IDs ≥ 8 to the SRAM
address (`0x2000F000` default for STM32F401xC top-of-RAM carve-out).
Real builds should expose the exact `g_module_sram` address via
`keyboard.module_sram_layout()`.

## Authoring a pipeline module

The complete example lives at:
`qmk-tools/qmk/QMKata/module_examples/pipeline_sticky_combo/`.

Minimal skeleton:

```c
#include "module_api.h"

static sm_machine_t g_machine;
static struct { /* your state */ pipeline_env_t *env; } g_state;

static sm_result_t my_handle(void *self, keyevent_t *e, keyrecord_t *r) {
    /* … translate keys, call env->tap_code16, etc. … */
    return SM_PASS;
}

static uint32_t module_init(pipeline_env_t *env) {
    if (!env) return 0xDEADBEEFu;     /* firmware doesn't support pipeline */
    g_state.env = env;

    g_machine.instance = &g_state;
    g_machine.handle   = my_handle;
    g_machine.tick     = NULL;
    g_machine.reset    = NULL;
    g_machine.name     = "my_feature";
    g_machine.phase    = PHASE_PRE_TAP;
    g_machine.priority = 50;

    env->pipeline_register(&g_machine);
    return MODULE_INIT_MAGIC;
}

static uint32_t module_deinit(void) {
    if (g_state.env) g_state.env->pipeline_unregister(&g_machine);
    return 0;
}

static sm_machine_t *machine_get(void) { return &g_machine; }

MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT]                 = module_init,
    [MODULE_HOOK_DEINIT]               = module_deinit,
    [MODULE_PIPELINE_HOOK_GET_MACHINE] = machine_get,
};
```

## ABI version

Module header version is **3** (firmware: `MODULE_HEADER_VERSION = 3`).
The bump from v2 was triggered by adding the `pipeline_env_t *env`
argument to `module_init_fn_t`. v2 modules are rejected with a console
message and must be rebuilt.

When does the version bump again?

- ABI change to `module_init_fn_t` or `module_deinit_fn_t` signatures.
- Reordering or removing `module_header_t` fields.
- Changing the hook-table layout (size, alignment, ordering).

Adding fields to `pipeline_env_t` (callbacks at the end) does **not**
require a bump — old modules that never reference the new field continue
to work. Reordering or removing `pipeline_env_t` fields **does**.

## Debugging crashes

SRAM modules have no map/symbols on the device. When a hard-fault
happens, the firmware logs the PC. Reproduce by `addr2line`-ing against
the module ELF + load address (printed by `mod load sram slot=N
init_fn=0xXXXXXXXX` on the console):

```
arm-none-eabi-addr2line -e build/sticky_combo_module.elf 0x2000F1A4
```

Subtract `0x2000F020` (slot base + 32-byte header) from the crash PC
to get the offset into the module's `.text`.

## Volatility caveat — recap

```
Boot         → SRAM empty, no modules loaded.
Upload       → memcpy into slot, hooks claimed, init() runs.
Reset/Power  → SRAM contents lost. Hooks were already released by
               the reset-induced re-init of g_module_hooks[]. Re-upload
               to restore.
```

This is intentional. Permanent features go to flash modules or the
firmware build itself.

## See also

- `keyboards/keychron/common/module/multi-module-plan.md` — original
  multi-module hook plan (flash side).
- `keyboards/keychron/common/module/pipeline_env.h` — env table fields.
- `quantum/features/README.md` — when to use SM-driven pipeline features.
- `qmk-tools/qmk/QMKata/module_examples/pipeline_sticky_combo/README.md` —
  worked example.
