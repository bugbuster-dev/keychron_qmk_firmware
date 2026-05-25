# SRAM-Loadable Behavior Modules

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
module loader + kbsm env, the build still has ~30 KB heap remaining.
Growing `MODULE_SRAM_TOTAL_SIZE`
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
KEY_BEHAVIOR_SM_ENABLE = yes
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

Host tooling must relocate modules against the firmware's **actual**
`g_module_sram` address. `0x2000F000` remains the historical default
for an STM32F401xC 4 KB top-of-RAM carve-out, but Q3 Max firmware
builds place `g_module_sram` in `.bss`, so the address moves when
`.bss` changes. Use `arm-none-eabi-nm .build/...elf | grep
g_module_sram` or `emulator/scripts/build_sram_module.py`, which
resolves it automatically.

## Authoring a behavior module

The complete example lives at:
`qmk-tools/qmk/QMKata/module_examples/kbsm_sticky_combo/`.

The stock `ModuleBuild` rejects writable globals and the default module
linker script discards `.data`/`.bss`. That is safe for flash/XIP
modules but not enough for stateful SRAM behavior modules like
`kbsm_sticky_combo`, which need `g_machine` and feature state to
persist across callbacks. The emulator helper
`emulator/scripts/build_sram_module.py` uses a SRAM-only linker-script
variant that keeps `.data` and `.bss` inside the 4 KB module blob, then
relocates + CRCs the result against the resolved `g_module_sram`.

Minimal skeleton:

```c
#include "module_api.h"

static kbsm_t g_machine;
static struct { /* your state */ kbsm_env_t *env; } g_state;

static kbsm_result_t my_handle(void *self, keyevent_t *e, keyrecord_t *r) {
    /* … translate keys, call env->tap_code16, etc. … */
    return KBSM_PASS;
}

static uint32_t module_init(kbsm_env_t *env) {
    if (!env) return 0xDEADBEEFu;     /* firmware doesn't support kbsm */
    g_state.env = env;

    g_machine.instance = &g_state;
    g_machine.handle   = my_handle;
    g_machine.tick     = NULL;
    g_machine.reset    = NULL;
    g_machine.name     = "my_feature";
    g_machine.phase    = KBSM_PHASE_PRE_TAP;
    g_machine.priority = 50;

    env->kbsm_register(&g_machine);
    return MODULE_INIT_MAGIC;
}

static uint32_t module_deinit(void) {
    if (g_state.env) g_state.env->kbsm_unregister(&g_machine);
    return 0;
}

static kbsm_t *machine_get(void) { return &g_machine; }

MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT]                 = module_init,
    [MODULE_HOOK_DEINIT]               = module_deinit,
    [MODULE_KBSM_HOOK_GET_MACHINE] = machine_get,
};
```

## ABI version

Module header version is **5** (firmware: `MODULE_HEADER_VERSION = 5`).
v4→v5 added `send_string` to `kbsm_env_t` for multi-character string
output (used by the autotext module). v4 modules continue to work — the
new field is at the end of the struct and old modules never reference it.

When does the version bump again?

- ABI change to `module_init_fn_t` or `module_deinit_fn_t` signatures.
- Reordering or removing `module_header_t` fields.
- Changing the hook-table layout (size, alignment, ordering).

Adding fields to `kbsm_env_t` (callbacks at the end) does **not**
require a bump — old modules that never reference the new field continue
to work. Reordering or removing `kbsm_env_t` fields **does**.

## Debugging crashes

SRAM modules have no map/symbols on the device. When a hard-fault
happens, the firmware logs the PC. Reproduce by `addr2line`-ing against
the module ELF + load address (printed by `mod load sram slot=N
init_fn=0xXXXXXXXX` on the console):

```
arm-none-eabi-addr2line -e build/sticky_combo_module.elf 0x2000F1A4
```

Subtract the resolved `g_module_sram` address from the crash PC to get
the offset into the module blob. The current module binary layout is:

```
[0..31]    module_header_t (32 bytes)
[32..159]  hook table (MODULE_HOOK_MAX * 4 bytes)
[160..]    .text + merged .rodata + SRAM-only .data/.bss when using
           emulator/scripts/build_sram_module.py
```

So, if `init_fn=0x20003e55` and `g_module_sram=0x20003d68`, the init
function lives at blob offset `0xed`.

## Renode emulator workflow

For interactive debugging in the emulator:

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
python3 emulator/scripts/sync_addrs.py
python3 emulator/scripts/build_sram_module.py
python3 emulator/scenarios/sram_sticky_combo.py
```

The scenario stages the module into `g_emu_module_stage` after boot,
writes `g_emu_module_stage_len`, then writes command `1` to
`g_emu_module_cmd`. Firmware polls that byte from `matrix_scan_kb` and
calls `module_load(8, g_emu_module_stage, len)`. The separate staging
buffer is required because `module_sram_clear()` clears `g_module_sram`
before copying into it.

Runtime controls from the Renode monitor:

```
sysbus WriteByte <g_emu_module_cmd> 1   # load slot 8
sysbus WriteByte <g_emu_module_cmd> 2   # unload slot 8
sysbus WriteByte <g_emu_module_cmd> 0   # clear edge detector
```

Regression coverage:

```bash
python3 emulator/scenarios/test_sram_sticky_combo.py
```

This currently verifies combo arm/consume, timeout, non-combo passthrough,
KC_UP/KC_DOWN tap actions, and unload/reload.

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
- `docs/design/keybehavior-sm-sram-module-architecture.md` — end-to-end
  architecture for SRAM-loaded behavior modules.
- `keyboards/keychron/common/module/kbsm_env.h` — env table fields.
- `quantum/features/README.md` — when to use SM-driven behavior features.
- `qmk-tools/qmk/QMKata/module_examples/kbsm_sticky_combo/README.md` —
  worked example.
