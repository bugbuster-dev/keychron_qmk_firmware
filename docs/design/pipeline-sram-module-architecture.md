# Pipeline SRAM Module Architecture

This document explains the end-to-end architecture for state-machine
pipeline modules loaded into SRAM, using the `pipeline_sticky_combo`
example as the reference implementation.

## End-to-end flow

```
module source
  → qmk-tools ModuleBuild
  → MODL blob (header + hook table + code + SRAM data)
  → host relocates R_ARM_ABS32 against g_module_sram
  → Renode stages blob into g_emu_module_stage
  → firmware module_load(8, g_emu_module_stage, len)
  → module_sram_clear(8)
  → module_sram_write(g_module_sram, staged_bytes, len)
  → module_init(pipeline_env_t *env)
  → env->pipeline_register(&machine)
  → action_exec calls pipeline_process_pre_tap()
  → machine->handle(instance, event, record)
  → env->tap_code16()/register_code()/etc.
  → HID report
```

The important separation is **staging buffer vs. slot buffer**:

- `g_emu_module_stage` is a temporary Renode upload buffer.
- `g_module_sram` is the executable SRAM slot used by the loader.

Do not stage directly into `g_module_sram`. `module_load_sram()` clears
the destination slot to `0xFF` before copying, so staging in-place would
destroy the source bytes before `memcpy`.

## Module memory layout

The module blob is a normal byte array written to an SRAM slot:

```
[0..31]    module_header_t (32 bytes)
[32..159]  hook table (MODULE_HOOK_MAX * 4-byte offsets)
[160..]    .text + merged .rodata
           + SRAM-only .data/.bss when using build_sram_module.py
```

The loader treats hook-table entries as offsets from the slot base:

```c
void *func = (void *)(slot_addr + hook_table_data[i]);
```

Therefore hook-table relocations are intentionally **not** applied by
the host. The firmware adds `slot_addr` at call time.

Literal-pool and data references inside `.text` are different: those
must be absolute addresses at runtime, so the host applies
`R_ARM_ABS32` relocations by adding the target slot base.

## Relocation target

Always relocate against the firmware's actual `g_module_sram` symbol,
not a hardcoded default.

```bash
arm-none-eabi-nm .build/keychron_q3_max_ansi_encoder_keychron.elf \
  | grep g_module_sram
```

Historically, qmk-tools used `0x2000F000` as the default SRAM slot base
for a 4 KB top-of-RAM carve-out. In the Q3 Max firmware, however,
`g_module_sram` lives in `.bss` and moves whenever `.bss` layout changes.

`emulator/scripts/build_sram_module.py` resolves the current symbol and
records it in `.build/sticky_combo_module.json`. The interactive
scenario refuses stale binaries whose sidecar was relocated against a
different address.

## Stateful SRAM modules

The stock qmk-tools `ModuleBuild` rejects writable globals and the
default linker script discards `.data` and `.bss`. That is correct for
flash/XIP modules, where writable state inside flash would be invalid.

State-machine pipeline modules are different: they need persistent
state across callbacks, e.g.

```c
static sticky_state_t g_state;
static sm_machine_t g_machine;
```

For emulator SRAM builds, `build_sram_module.py` uses a generated linker
script (`.build/module_linker_sram.ld`) that keeps `.data` and `.bss`
inside the module blob. Since the blob executes from writable SRAM,
module-local state is valid.

This helper is intentionally SRAM-only. Do not use the generated
SRAM-linker output for flash module slots.

## Loader ABI

The current header version is `MODULE_HEADER_VERSION = 3`.

Version 3 changes module init to receive a `pipeline_env_t *`:

```c
```

The module must return:

```c
#define MODULE_INIT_MAGIC 0x600DBEEFu
```

The loader logs the init result:

```
mod load sram slot=8 init_fn=0x20003e55
mod load sram slot=8 init OK rc=0x600dbeef
```

If `env` is `NULL`, the firmware lacks pipeline support; modules should
return a non-magic error value.

## `pipeline_env_t`

Pipeline modules cannot link directly against arbitrary firmware
symbols. Instead, firmware passes a table of function pointers:

```c
typedef struct pipeline_env {
    void     (*pipeline_register)(sm_machine_t *machine);
    void     (*pipeline_unregister)(sm_machine_t *machine);
    void     (*tap_code16)(uint16_t kc);
    void     (*register_code16)(uint16_t kc);
    void     (*unregister_code16)(uint16_t kc);
    void     (*tap_code)(uint8_t kc);
    void     (*register_code)(uint8_t kc);
    void     (*unregister_code)(uint8_t kc);
    uint16_t (*timer_read)(void);
    uint16_t (*timer_elapsed)(uint16_t since);
    uint16_t (*get_record_keycode)(keyrecord_t *r, bool update_layer_cache);
    int      (*xprintf)(const char *fmt, ...);
    void     *extension;
    uintptr_t module_base;
} pipeline_env_t;

`module_base` is set by the loader before calling `init()` so the module
can rebase its own internal pointers (compiled at ORIGIN=0) to the actual
runtime load address.

The module stores `env` in its local state and routes all firmware calls
through it. Adding new fields at the end is ABI-compatible for old
modules. Removing or reordering fields requires a header-version bump.

`env->xprintf` is implemented by firmware and routes through the same
QMKata `sendchar()` path that is tee'd to USART2 in Renode, so module
diagnostics can appear in the `usart2` analyzer.

## Pipeline registration

A pipeline module sets up an `sm_machine_t` in `module_init`:

```c
g_machine.instance = &g_state;
g_machine.handle   = sticky_handle;
g_machine.tick     = sticky_tick;
g_machine.reset    = sticky_reset;
g_machine.name     = "sticky_combo_sram";
g_machine.phase    = PHASE_PRE_TAP;
g_machine.priority = 40;

env->pipeline_register(&g_machine);
return MODULE_INIT_MAGIC;
```

The firmware dispatch path is:

```
action_exec()
  → pipeline_process_pre_tap(event, record)
  → machine->handle(instance, event, record)
```

Return values:

- `SM_PASS` — continue normal QMK processing.
- `SM_CONSUME` — skip later QMK processing for this event.

The sticky combo module uses `SM_CONSUME` for combo-arm and handled tap
events, and `SM_PASS` for unrelated keys.

## Sticky combo reference behaviour

Default definition in `combos_def.h`:

```c
{ KC_J, KC_K, KC_NO, KC_UP, KC_DOWN }
```

Meaning:

- Press `J`, then `K` within the module's window → combo arms.
- `combo_action = KC_NO`, so arming is silent.
- Timeout path: if `K` arrives after the window, both `J` and `K` pass
  through normally.
- After arming:
  - ARMED_FOR_KEY1 tap fires `KC_UP` (`REG 52`).
  - ARMED_FOR_KEY2 tap fires `KC_DOWN` (`REG 51`).

In Renode, QMK's timer advances slower than virtual time, so the
regression test uses `500ms` Renode virtual time to exceed the module's
`50ms` QMK timer window.

## Renode staging and control

Firmware exposes three emulator-only symbols:

| Symbol | Purpose |
|--------|---------|
| `g_emu_module_stage` | Host staging buffer for module bytes |
| `g_emu_module_stage_len` | Length of staged bytes |
| `g_emu_module_cmd` | Runtime command byte |

Manual sequence:

```
sysbus LoadBinary @.build/sticky_combo_module.bin <g_emu_module_stage>
sysbus WriteWord <g_emu_module_stage_len> 0x0410
sysbus WriteByte <g_emu_module_cmd> 1     # load
```

Commands:

```
0 = clear edge detector
1 = load slot 8 from g_emu_module_stage
2 = unload slot 8
```

The command byte is edge-triggered. To issue the same command twice,
write `0` first.

## Verification

Interactive:

```bash
python3 emulator/scenarios/sram_sticky_combo.py
```

Headless regression:

```bash
python3 emulator/scenarios/test_sram_sticky_combo.py
```

Current coverage:

- Combo arm consumes the second key.
- Timeout does not arm.
- Non-combo key passthrough.
- Non-combo key passthrough while a combo is active.
- `KC_UP` and `KC_DOWN` tap actions.
- Unload/reload.

## Debugging

USART2 output is enabled by `q3_max.resc`:

```
sysbus WriteDoubleWord 0x40023840 0x00020000  # USART2 clock
sysbus WriteDoubleWord 0x4000440c 0x0000200c  # UE + TE
```

Firmware `dbg_putc()` writes to USART2 DR (`0x40004404`). QMKata's
`sendchar()` tees every `xprintf`/`dprintf` byte to `dbg_putc`, so both
firmware and module diagnostics appear in Renode's `usart2` analyzer.

Useful lines:

```
emu: load slot 8 len=0410
mod load sram slot=8 init OK rc=0x600dbeef
MAT 80 00 00 00
REG 52
```
