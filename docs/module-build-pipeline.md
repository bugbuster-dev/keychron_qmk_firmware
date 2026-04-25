# Module Build Pipeline — Host-Side Compilation and Relocation

> **Audience:** Contributors adding module features, debugging build failures, or
> extending the host toolchain. Assumes familiarity with ARM bare-metal and the
> ELF object format.
>
> **Scope:** Host-side only — how a `.c` file becomes a flashed module binary.
> Firmware-side loading, dispatch, and boot scan are covered in
> `multi-module-plan.md`.

---

## Quick Reference

```
.c source
  │
  ▼ compile (arm-none-eabi-gcc, -Os, -ffreestanding, ORIGIN=0)
.o object
  │
  ▼ validate (no .data/.bss sections)
  ▼ resolve symbols (nm -u → firmware .map → PROVIDE() linker script)
  ▼ link (module_linker.ld → .elf)
  ▼ extract relocations (pyelftools: R_ARM_ABS32 on .text)
  ▼ objcopy (.elf → .bin)
  ▼ assemble (32-byte header + hook table + code)
  │
  ▼ apply_relocations_and_crc(slot_addr)    ← load time, slot known
  │   · rebase each ABS32 literal: val += slot_addr
  │   · compute CRC-32/ISO-HDLC, embed in header
  ▼ final binary (flashed verbatim)
```

## 1. Why Host-Side Relocation

Modules are linked at `ORIGIN = 0` so the same binary loads to any flash
slot. The compiler emits **literal pools** — 32-bit absolute addresses
referencing the module's own `.rodata` (string literals, hook table
addresses, etc.). At link time these hold addresses in `[0, code_size)`.
At runtime they must hold absolute XIP addresses in `[slot_addr,
slot_addr + code_size)`.

The host adds `slot_addr` to every literal-pool word before flashing.
The firmware writes the received bytes **verbatim** — no mutation, no
relocation walk. This guarantees the CRC embedded by the host matches
what `module_boot_scan` re-validates on every cold boot.

The alternative (firmware-side relocation) was rejected because it
mutated flash bytes after CRC validation, causing a cold-boot CRC
mismatch. See `docs/plans/2026-04-23-module-host-side-relocations.md`
for the full decision record.

## 2. Build Pipeline Stages

### 2.1 Compile (`_compile`)

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L170-202

Compiles `.c` → `.o` with ARM Cortex-M4 flags:

| Flag | Purpose |
|------|---------|
| `-mcpu=cortex-m4 -mthumb` | Target architecture, Thumb mode |
| `-mfloat-abi=hard -mfpu=fpv4-sp-d16` | Hardware FP (matches firmware) |
| `-Os` | Size optimization |
| `-ffreestanding` | No libc assumptions |
| `-ffunction-sections -fdata-sections` | Per-function `.text` sections |
| `-fno-unwind-tables -fno-asynchronous-unwind-tables` | Prevent dangling unwind refs |
| `-fno-common` | Prevent zero-initialized global merging |
| `-Wall -Werror` | Treat warnings as errors |

The module source includes `module_api.h` (host-side copy) for hook
constants, type stubs, and the `MODULE_HOOK_TABLE` attribute.

### 2.2 Validate No Writable Sections (`_validate_no_writable_sections`)

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L278-319

Modules execute from flash (XIP). Any writable section (`.data`, `.bss`,
`.sdata`, `.sbss`, `.tdata`, `.tbss`) would require RAM placement,
which the module system does not provide. `objdump -h` inspects the
object file; if any writable section has nonzero size, the build
fails with the section names and sizes.

Local `static` variables inside functions are fine — they live on the
stack, not in a named section. Only **global/static at file scope**
triggers rejection.

### 2.3 Resolve External Symbols (`_resolve_symbols`)

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L204-276

Modules reference firmware symbols (`mprintf`, `printf`, `layer_state`,
QMK core functions). The host resolves these by:

1. Running `nm -u` on the `.o` file to find undefined symbols
2. Looking up each symbol in the firmware's `.map` file
   (`GccMapfile`, parsed from the last firmware build)
3. Generating a linker script with `PROVIDE(symbol = 0xADDR)`
   directives

If a symbol cannot be resolved, the build fails. This prevents
link-time silence and runtime hard-faults from dangling pointers.

**Why `PROVIDE()`:** The linker treats these as weak definitions. If
the module defines the same symbol (e.g., a self-contained test
module), the module's definition wins.

### 2.4 Link

**File:** `qmk-tools/qmk/QMKata/module_linker.ld`

The linker script defines three output sections:

```
.module_header  — 32 zero bytes (overwritten by _assemble)
.hook_table     — KEEP(*(.hook_table)), 4-byte aligned
.text           — *(.text*) + *(.rodata*), 4-byte aligned
/DISCARD/       — .data, .bss, .ARM.*, .eh_frame, etc.
```

**Critical invariant:** `.rodata*` is merged into `.text`. The
relocation extractor (Section 2.6) only scans `.text`-targeted
relocations. If `.rodata` were placed in a separate section, its
`R_ARM_ABS32` relocations would be silently dropped, leaving
unrebased literal-pool addresses at runtime.

Memory region: `ORIGIN = 0, LENGTH = 0x1000` (4 KB slot). The
`ORIGIN = 0` is intentional — it ensures all absolute addresses in
the linked ELF start from zero, making the host-side rebasing
calculation a simple addition of `slot_addr`.

### 2.5 Extract Relocations (`_extract_relocations`)

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L321-365

Using `pyelftools`, the host parses the ELF's relocation sections and
collects byte offsets where `R_ARM_ABS32` relocations target `.text`:

```python
offsets = []
for sec in elf.iter_sections():
    if not isinstance(sec, RelocationSection):
        continue
    target = elf.get_section(sec['sh_info'])
    if target.name != ".text":
        continue    # skip .hook_table, etc.
    for r in sec.iter_relocations():
        if r['r_info_type'] == R_ARM_ABS32:
            offsets.append(r['r_offset'])  # VMA = file offset for ET_EXEC
```

**Why `.text` only:**
- `.hook_table` entries are **offsets**, not absolute addresses.
  Firmware dispatch adds `slot_addr` at call time. Rebasing the hook
  table would double-relocate.
- `R_ARM_THM_CALL` targets are external symbols resolved at link time
  via `PROVIDE()`. They already hold absolute firmware addresses.
- `R_ARM_THM_JUMP24` is PC-relative — no rebasing needed.

**Return value:** sorted list of byte offsets into the final binary
where a 32-bit word needs `slot_addr` added.

### 2.6 Objcopy and Assemble (`_assemble`)

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L367-477

`objcopy -O binary` extracts the raw machine code from the ELF. The
host then assembles the final binary:

```
Offset  Size  Content
0       32    module_header_t (magic, version, flags, code_size,
                    hook_bitmap, hook_table_off, init_off, deinit_off, crc32)
32      128   .hook_table (MODULE_HOOK_MAX × 4 bytes, NULL = unclaimed)
160     *     .text + .rodata (code, literal pools, string constants)
```

The host scans the hook table to build `hook_bitmap` (bitmask of
non-NULL entries) and extracts `init_off` / `deinit_off` (indices 3
and 4). These are written into the header.

**CRC:** `_assemble` computes a provisional CRC-32 over the entire
binary with the `crc32` field zeroed. This CRC is **overwritten** by
`apply_relocations_and_crc` at load time after rebasing. The
provisional CRC exists so the binary is self-consistent if inspected
before load (e.g., in test assertions).

**Return dict:**
```python
{
    'binary': bytes,       # header + hook_table + code
    'hook_bitmap': int,    # bitmask of claimed hooks
    'hooks': [str],        # human-readable hook names
    'size': int,           # total bytes
    'fits_slot': bool,     # size <= MODULE_FLASH_SLOT_SIZE
    'relocs': [int],       # sorted ABS32 offsets (for load time)
}
```

Note: `relocs` is **not** written into the binary. It is a host-side
data structure consumed at load time.

## 3. Load-Time Relocation and CRC

### 3.1 `apply_relocations_and_crc`

**File:** `qmk-tools/qmk/QMKata/ModuleBuild.py` L479-515

Called from `ModuleTab._prepare_binary_for_load(slot_id)` once the
user has selected a flash slot. Two operations:

**1. Rebase ABS32 literal-pool entries:**
```python
for off in relocs:
    val = struct.unpack_from("<I", out, off)[0]
    struct.pack_into("<I", out, off, (val + slot_addr) & 0xFFFFFFFF)
```

Each offset points to a 32-bit word in the binary holding a link-time
address (e.g., `0x000000A0` for a string literal at offset 0xA0).
After rebasing, it holds the absolute XIP address
(e.g., `0x080080A0` for slot 0).

**2. Compute and embed final CRC:**
```python
struct.pack_into("<I", out, crc_off, 0)  # zero crc field
crc_value = zlib.crc32(bytes(out)) & 0xFFFFFFFF
struct.pack_into("<I", out, crc_off, crc_value)
```

The CRC covers the entire binary with the `crc32` field itself zeroed
(self-referential CRC convention). Algorithm: CRC-32/ISO-HDLC
(zlib-compatible), polynomial `0xEDB88320`, init `0xFFFFFFFF`,
input/output reflected, final XOR `0xFFFFFFFF`.

**CRC is NOT an authenticity check.** CRC-32 is trivially forgeable.
It detects accidental corruption (transmission error, interrupted
flash write, bit rot) with ~2^-32 false-negative probability.

### 3.2 Slot Address Calculation

```python
slot_addr = MODULE_FLASH_BASE + slot_id * MODULE_FLASH_SLOT_SIZE
#         = 0x08008000 + slot_id * 0x1000
```

| Slot | Address |
|------|---------|
| 0    | 0x08008000 |
| 1    | 0x08009000 |
| 2    | 0x0800A000 |
| 3    | 0x0800B000 |
| 4    | 0x0800C000 |
| 5    | 0x0800D000 |
| 6    | 0x0800E000 |
| 7    | 0x0800F000 |

These constants must match the firmware's `module_flash.h`
(`MODULE_FLASH_BASE`, `MODULE_FLASH_SLOT_SIZE`). A mismatch surfaces
as a CRC failure on the device.

### 3.3 Firmware Verification

The firmware validates the CRC twice:

1. **`module_load`** — after receiving the binary over SYSEX, before
   writing to flash. Validates the RAM buffer matches the embedded CRC.

2. **`module_boot_scan`** — on every cold boot, reads each slot from
   flash and validates. If the CRC matches, the module's hooks are
   registered in the global `g_module_hooks` table.

Because the host rebased before computing the CRC, and the firmware
writes the bytes verbatim, flash bytes == CRC'd bytes. Both checks
succeed.

## 4. Module Binary Format

### 4.1 Header (32 bytes)

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 0x4D4F444C ("MODL")
    uint16_t version;        // 2
    uint16_t flags;          // reserved, must be 0
    uint32_t code_size;      // total binary size
    uint32_t hook_bitmap;    // bitmask of claimed hooks
    uint32_t hook_table_off; // 32 (always)
    uint32_t init_off;       // offset to init function, 0 = none
    uint32_t deinit_off;     // offset to deinit function, 0 = none
    uint32_t crc32;          // CRC-32/ISO-HDLC
} module_header_t;
```

### 4.2 Hook Table (128 bytes)

Array of `MODULE_HOOK_MAX` (32) 32-bit offsets. Non-zero entries
point into `.text` at the function's entry point. The offset is
relative to address 0 (link-time address), which equals the file
offset in the binary. Firmware dispatch adds `slot_addr` before
calling.

**Important:** Hook table entries are **not** rebased by the host.
They remain as offsets. Firmware dispatch adds `slot_addr` at call
time:
```c
void* fn = hooks[index].func;  // e.g., 0x000000C8
// ...
((fn_ptr)(fn + slot_addr))();  // e.g., calls 0x080080C8
```

### 4.3 Code Section

Contains `.text` (machine code + literal pools) and `.rodata`
(string literals, constants). Both are merged by the linker script.
4-byte aligned. Literal pool entries at the offsets listed in the
`relocs` array have been rebased to absolute XIP addresses.

## 5. Module ABI

### 5.1 Init / Deinit

```c
uint32_t module_init(void);       // must return MODULE_INIT_MAGIC (0x600DBEEF)
uint32_t module_deinit(void);     // return value logged, not checked
```

No `module_base` parameter. Module code accesses its own data through
normal C references — the host has already rebased literal-pool
addresses. Passing a base address would invite double-relocation bugs.

### 5.2 Hook Function Signatures

| Hook | Signature |
|------|-----------|
| `combo_should_trigger` | `bool(uint16_t, combo_t*, uint16_t, keyrecord_t*)` |
| `process_combo_event` | `void(uint16_t, bool)` |
| `get_combo_term` | `uint16_t(uint16_t, combo_t*)` |
| `get_combo_must_hold` | `bool(uint16_t, combo_t*)` |
| `get_combo_must_tap` | `bool(uint16_t, combo_t*)` |
| `get_combo_must_press_in_order` | `bool(uint16_t, combo_t*)` |
| `process_combo_key_release` | `bool(uint16_t, combo_t*, uint8_t, uint16_t)` |
| `process_combo_key_repress` | `bool(uint16_t, combo_t*, uint8_t, uint16_t)` |
| `combo_ref_from_layer` | `uint8_t(uint8_t)` |
| `pre_process_record` | `bool(uint16_t, keyrecord_t*)` |
| `process_record` | `bool(uint16_t, keyrecord_t*)` |
| `layer_state_set` | `layer_state_t(layer_state_t)` |
| `housekeeping` | `void(void)` |
| `shutdown` | `bool(bool)` |

### 5.3 Hook Table Declaration

```c
MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT] = module_init,
    [MODULE_KEY_HOOK_PRE_PROCESS_RECORD] = pre_process_record,
};
```

`MODULE_HOOK_TABLE` expands to `__attribute__((section(".hook_table"), used))`,
placing the array in the `.hook_table` output section and preventing
linker garbage collection.

### 5.4 Available Symbols

Modules can reference any firmware symbol that appears in the `.map`
file. Commonly used:

| Symbol | Description |
|--------|-------------|
| `mprintf` | Gated diagnostic print (respects `debug_config_user.module`) |
| `printf` | Unconditional console output |
| `layer_state` | Current layer state |
| `default_layer_state` | Default layer state |
| QMK core functions | `get_highest_layer()`, `layer_on()`, etc. |

**Do not** `#include` firmware headers. They pull in platform-specific
definitions that break the host build. Use the type stubs in
`module_api.h` instead.

## 6. Example Module

See `qmk-tools/qmk/QMKata/module_examples/pre_record_logger.c`:

```c
#include "module_api.h"

static uint32_t module_init(void) {
    mprintf("pre_record_logger loaded\n");
    return MODULE_INIT_MAGIC;
}

static bool pre_process_record(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        mprintf("key: %04X press\n", keycode);
    } else {
        mprintf("key: %04X release\n", keycode);
    }
    return true;  /* let processing continue */
}

MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT] = module_init,
    [MODULE_KEY_HOOK_PRE_PROCESS_RECORD] = pre_process_record,
};
```

This module builds to 288 bytes, claims hooks `['init',
'pre_process_record']`, and works without any keymap changes.

## 7. Key Invariants

1. **Flash bytes == CRC'd bytes.** The firmware never mutates module
   data between load and boot scan. Host rebases before CRC'ing.

2. **Hook table entries are offsets, not absolute addresses.** Firmware
   dispatch adds `slot_addr` at call time. The host does NOT rebase
   hook table entries.

3. **`.rodata` merged into `.text`.** Relocation extractor only scans
   `.text`-targeted relocs. Moving `.rodata` to a separate section
   breaks literal-pool rebasing.

4. **`ORIGIN = 0` for modules.** Host-side rebasing depends on ABS32
   targets initially holding link-time addresses in `[0, code_size)`.

5. **No writable sections.** Modules are pure XIP — no `.data`, no
   `.bss`. Stack-local `static` variables are fine.

## 8. Debugging

### Module loads but crashes on first hook call

Likely cause: unrebased literal-pool address. Check:
1. Does `_extract_relocations` find relocs? Print `len(result['relocs'])`.
2. Does `apply_relocations_and_crc` rebase them? Compare pre/post values.
3. Does the hook table entry look correct? It should be an offset
   (`< code_size`), not an absolute address.

### CRC mismatch on cold boot

This should not happen with the current architecture. If it does:
1. Verify firmware writes bytes verbatim (no in-place mutation).
2. Verify `module_boot_scan` CRC algorithm matches host's
   `zlib.crc32(bytes) & 0xFFFFFFFF`.

### Symbol resolution fails

The firmware `.map` file must be from a recent build of the target
keyboard. Rebuild firmware, then rebuild the module. The host
auto-discovers the `.map` file from `.build/obj_keychron*/src/`.

### "Module contains writable sections" error

Check for `static` or global variables at file scope. Move them
inside functions (stack-allocated) or remove them. Modules cannot
have persistent state — use EEPROM or flash for persistence.

## 9. File Index

| File | Role |
|------|------|
| `qmk-tools/.../ModuleBuild.py` | Build pipeline, relocation extraction, CRC |
| `qmk-tools/.../ModuleTab.py` | UI integration, `_prepare_binary_for_load` |
| `qmk-tools/.../module_api.h` | Host-side header for module source |
| `qmk-tools/.../module_linker.ld` | Linker script (`ORIGIN=0`, section layout) |
| `qmk-tools/.../GccToolchain.py` | ARM GCC wrapper (compile, link, objcopy) |
| `qmk-tools/.../GccMapfile.py` | Firmware `.map` parser for symbol resolution |
| `firmware/.../module_loader.h` | Firmware header struct, hook indices, ABI |
| `firmware/.../module_loader.c` | Load, boot scan, hook registration |
| `firmware/.../module_dispatch.c` | Dispatcher implementations |
| `firmware/.../module_flash.c` | Flash erase/write primitives |

## 10. Related Documents

| Document | Location |
|----------|----------|
| Multi-module plan (hook indices, dispatchers, testing) | `firmware/.../module/multi-module-plan.md` |
| Host-side relocation decision record | `firmware/docs/plans/2026-04-23-module-host-side-relocations.md` |
| ARM AAELF32 relocation spec research | `qmk-tools/.../.copilot-tracking/research/20260330-elf-relocation-design-corrections-research.md` |
