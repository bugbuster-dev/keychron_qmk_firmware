# Module Runtime Relocations Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make `printf("literal string")` (and any other `.rodata` reference) work transparently from anywhere inside a loadable module, by adding a relocation table to the module format and having the firmware loader patch absolute addresses in the module's literal pool after copying the module bytes into Flash.

**Architecture:** Pass `ld -q` at module link time to retain `R_ARM_ABS32` relocations in the linked ELF. Extract them with pyelftools in `ModuleBuild.py`, pack as a compact little-endian table of 32-bit offsets into a new `.module_relocs` section appended after `.text`. Extend `module_header_t` with `reloc_off` + `reloc_count` fields. At load time, after the module is written to Flash, the loader walks the reloc list and, for each entry, reads the 32-bit word at `slot_addr + offset`, adds `slot_addr`, writes it back (via the same flash word-program mechanism already used by `module_flash.c`). Hook-table relocations are filtered out in `ModuleBuild.py` (the hook table stays offset-based because firmware dispatch already does `slot_addr + hook_off` arithmetic).

**Tech Stack:** Python 3 + pyelftools (host), C + STM32F4 HAL flash programming (firmware), arm-none-eabi-gcc 13.2.

---

## Pre-flight context

Reloc inspection already done manually (this session). Key facts the implementer can rely on:

1. `arm-none-eabi-gcc -Wl,-q` preserves relocations in the linked ELF. Without `-q`, only undefined externs' relocs survive and `R_ARM_ABS32` entries against `.text`/`.rodata` are consumed and discarded. `-q` keeps them all.
2. For a module that calls `printf("foo"); printf("bar=%u", x);`, the linked ELF contains exactly two `R_ARM_ABS32` entries in `.rel.text`, each pointing at a 4-byte literal-pool slot that already holds the link-time-absolute address of the corresponding string (e.g. `0x000000A8`, `0x000000BB` when `ORIGIN=0`).
3. `pyelftools` (already an optional dep — `GccToolchain.py` imports `elftools.elf.elffile.ELFFile`) exposes relocations via `elftools.elf.relocation.RelocationSection.iter_relocations()`. Each yields `r_offset`, `r_info_type` (2 = `R_ARM_ABS32`, 10 = `R_ARM_THM_CALL`, etc.), `r_info_sym`.
4. A relocation's target section is `elf.get_section(sec['sh_info'])`. We keep only those whose target section is `.text` (literal pool). We drop `.hook_table` targets (offset semantics preserved) and `.module_header` targets (none expected).
5. Relocation type filter: keep only `R_ARM_ABS32` (type 2). All other types (thm_call, prel31, etc.) are either already resolved at link time or will require separate handling — none of our current module patterns produce them against `.rodata`.
6. Firmware writes module Flash using `eflStart` / `flashProgram` in `module_flash.c`. Writes must be word-aligned; our reloc offsets are always 4-byte aligned (ARM ELF guarantees this for `R_ARM_ABS32`). The flash program primitive is "program one or more 32-bit words starting at a word-aligned address" — exactly what we need for patching.
7. STM32F401 Flash programming rule: a word must be erased (all 1s) before being programmed. Our literal pool words already hold a meaningful non-zero value (the link-time address) when copied into Flash; we CANNOT simply re-program them in-place without erasing. **Therefore relocations must be applied DURING the initial write**, not after — by mutating the source bytes in RAM before each flash word-program call. The flash write path must read the reloc table from the in-RAM module buffer, build a fix-up list, and for every offset-matching word in the write stream, add `slot_addr` before writing.
8. Current header layout uses the last 4 bytes of the 32-byte header for `crc32`. There are no spare slots. **Header extension requires bumping `MODULE_HEADER_VERSION` to 2 and growing the header.** Details in Task 1.
9. Existing `module_linker.ld` emits a fixed 32-byte `.module_header` at offset 0, a 64-byte `.hook_table` ending at 96, then `.text`. The reloc table goes after `.text`. `ModuleBuild.py` currently does not read any section past `.text`; the reloc table must be assembled explicitly and appended to the binary.
10. Existing `_validate_no_writable_sections()` in `ModuleBuild.py` rejects `.data`/`.bss`. Reloc table will be in a new read-only section `.module_relocs`, so the validator needs no change but the linker script must explicitly place it.

## Header format change (v1 → v2)

New layout (40 bytes, +8):

```
offset  field
  0     magic           uint32  "MODL"
  4     version         uint16  = 2
  6     flags           uint16
  8     code_size       uint32  total binary size including reloc table
 12     hook_bitmap     uint32
 16     hook_table_off  uint32
 20     init_off        uint32
 24     deinit_off      uint32
 28     reloc_off       uint32  offset to reloc table start (0 = none)
 32     reloc_count     uint32  number of 4-byte entries in reloc table
 36     crc32           uint32  moved from offset 28 to 36
```

Firmware must reject v1 modules explicitly (log "module version unsupported: %u") rather than silently mis-interpreting their `crc32` field as `reloc_off`. Host builder emits only v2. This is acceptable because the module system is not yet deployed in any released firmware.

Size of `.hook_table` padding stays 64 bytes (16 slots × 4). Hook table offset becomes 40, ending at 40 + 64 = 104. **Linker script line 20 changes: `. = 32 + 64;` → `. = 40 + 64;`**.

---

## Task 1: Host — header layout v2

**Files:**
- Modify: `qmk/QMKata/ModuleBuild.py` (lines 13-17, 290-356)
- Modify: `qmk/QMKata/module_linker.ld` (lines 6-21)
- Modify: `qmk/QMKata/test_module_build_integration.py` (add field-layout test)
- Modify: `keychron_qmk_firmware/keyboards/keychron/common/module/module_loader.h` (lines 13-65)

**Step 1: Write failing test for header layout v2**

Add to `test_module_build_integration.py`:

```python
def test_header_layout_v2(self):
    builder = ModuleBuild(
        GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT)),
        firmware_path=str(FIRMWARE_ROOT),
    )
    result = builder.build(str(EXAMPLE_MODULE))
    self.assertIsNotNone(result, builder.last_error)
    binary = result["binary"]
    # v2 header is 40 bytes, hook table starts at 40
    magic = int.from_bytes(binary[0:4], "little")
    version = int.from_bytes(binary[4:6], "little")
    hook_table_off = int.from_bytes(binary[16:20], "little")
    reloc_off = int.from_bytes(binary[28:32], "little")
    reloc_count = int.from_bytes(binary[32:36], "little")
    crc = int.from_bytes(binary[36:40], "little")
    self.assertEqual(0x4D4F444C, magic)
    self.assertEqual(2, version)
    self.assertEqual(40, hook_table_off)
    # combo_layer_filter has no string literals → reloc_count may be 0 or 1
    self.assertGreaterEqual(reloc_count, 0)
    self.assertNotEqual(0, crc)  # computed CRC should not be zero
```

**Step 2: Run test, verify it fails**

Run: `/home/user/.local/bin/pytest qmk/QMKata/test_module_build_integration.py::ModuleBuildIntegrationTest::test_header_layout_v2 -v`
Expected: FAIL — current header has hook_table_off=32, version=1, no reloc fields.

**Step 3: Update `module_linker.ld`**

Change the hook table padding target from `32 + 64` to `40 + 64`. Also pre-declare `.module_relocs` as an empty section (Task 3 will populate it, but having the section anchor now prevents layout surprises). Replace lines 6-21 with:

```ld
.module_header : {
    /* Emit 40 real bytes so objcopy keeps header padding in the raw binary. */
    LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
    LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
} > MODULE
.hook_table : ALIGN(4) {
    KEEP(*(.hook_table))
    . = ALIGN(4);
    . = 40 + 64;  /* Force hook table to end at offset 104 (40-byte header + 64-byte table) */
} > MODULE
```

**Step 4: Update `ModuleBuild.py` constants and `_assemble()`**

Change:
- `MODULE_HEADER_SIZE = 32` → `40`
- `MODULE_HOOK_TABLE_OFF = 32` → `40`
- `MODULE_HEADER_VERSION = 1` → `2`

In `_assemble()`, change the header struct packing from `"<I H H I I I I I I"` (32 bytes) to `"<I H H I I I I I I I I"` (40 bytes), inserting `reloc_off` and `reloc_count` (both `0` in this task, populated in Task 3) between `deinit_off` and `crc32`:

```python
header = struct.pack("<I H H I I I I I I I I",
    MODULE_HEADER_MAGIC,       # magic
    MODULE_HEADER_VERSION,     # version = 2
    0x0000,                    # flags
    len(raw_bin),              # code_size
    hook_bitmap,
    MODULE_HOOK_TABLE_OFF,     # = 40
    init_off,
    deinit_off,
    0,                         # reloc_off (filled in Task 3)
    0,                         # reloc_count (filled in Task 3)
    0,                         # crc32 placeholder
)
assert len(header) == MODULE_HEADER_SIZE  # 40
```

`crc_off` becomes `MODULE_HEADER_SIZE - 4 = 36`. The existing CRC computation logic stays correct because it uses `MODULE_HEADER_SIZE - 4`.

**Step 5: Update firmware `module_loader.h`**

In `module_header_t`, insert `reloc_off` and `reloc_count` before `crc32`:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t code_size;
    uint32_t hook_bitmap;
    uint32_t hook_table_off;
    uint32_t init_off;
    uint32_t deinit_off;
    uint32_t reloc_off;     /* offset to relocation table (0 = none) */
    uint32_t reloc_count;   /* number of 4-byte entries in reloc table */
    uint32_t crc32;
} module_header_t;

_Static_assert(sizeof(module_header_t) == 40, "module_header_t must be 40 bytes");
#define MODULE_HEADER_VERSION 2
```

**Step 6: Run test, verify it passes**

Run: `/home/user/.local/bin/pytest qmk/QMKata/test_module_build_integration.py -v`
Expected: all 3 tests pass (2 existing + new layout test).

**Step 7: Rebuild firmware, verify it still compiles**

Run: `qmk compile -kb keychron/q3_max/ansi_encoder -km keychron`
Expected: clean build. `_Static_assert` catches any layout mismatch at compile time.

**Step 8: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/ModuleBuild.py qmk/QMKata/module_linker.ld qmk/QMKata/test_module_build_integration.py
git commit -m "feat(module): header v2 adds reloc_off/reloc_count fields

Reserves slots in the module header for an upcoming relocation table.
Grows header from 32 to 40 bytes, bumps version to 2, shifts hook
table offset to 40. No behavioral change yet: reloc_off/count both
zero, firmware still dispatches purely via hook_table offsets.

Old v1 modules become incompatible. Module system is not yet deployed,
so no migration path is needed."

cd /home/user/qmk/keychron_qmk_firmware
git add keyboards/keychron/common/module/module_loader.h
git commit -m "feat(module): header v2 adds reloc_off/reloc_count fields

Matches host module_api.h. Grows module_header_t from 32 to 40 bytes,
bumps MODULE_HEADER_VERSION to 2. _Static_assert guards the size.
No runtime behavior change yet; relocation application comes in a
follow-up."
```

---

## Task 2: Host — extract R_ARM_ABS32 relocations from linked ELF

**Files:**
- Modify: `qmk/QMKata/GccToolchain.py` (link() — add `-Wl,-q` flag)
- Modify: `qmk/QMKata/ModuleBuild.py` (new private method `_extract_relocations()`)
- Create: `qmk/QMKata/test_module_relocations.py`

**Step 1: Write failing unit test for `_extract_relocations()`**

Create `qmk/QMKata/test_module_relocations.py`:

```python
import os, shutil, tempfile, unittest
from pathlib import Path

from GccToolchain import GccToolchain
from ModuleBuild import ModuleBuild
from keyboards.KeychronQ3Max import KeychronQ3Max

ROOT = Path(__file__).resolve().parent
FIRMWARE_ROOT = ROOT.parents[2] / "keychron_qmk_firmware"


@unittest.skipUnless(shutil.which("arm-none-eabi-gcc"), "requires ARM toolchain")
class ModuleRelocationsTest(unittest.TestCase):

    def _build_probe(self, tmpdir, source_lines):
        """Compile+link a probe module; return path to .elf."""
        src = '\n'.join([
            '#include "module_api.h"',
            'extern int printf(const char *fmt, ...);',
            *source_lines,
            'MODULE_HOOK_TABLE',
            'const void *module_hook_table[MODULE_HOOK_MAX] = {',
            '    [MODULE_HOOK_INIT] = module_init,',
            '};',
        ])
        src_path = Path(tmpdir) / "probe.c"
        src_path.write_text(src)
        tc = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT))
        builder = ModuleBuild(tc, firmware_path=str(FIRMWARE_ROOT))
        obj = str(Path(tmpdir) / "probe.o")
        elf = str(Path(tmpdir) / "probe.elf")
        self.assertTrue(builder._compile(str(src_path), obj))
        sym_ld = str(Path(tmpdir) / "sym.ld")
        self.assertTrue(builder._resolve_symbols(obj, sym_ld))
        self.assertTrue(tc.link(obj, builder.linker_script, elf, [sym_ld]))
        return elf

    def test_extract_relocations_for_two_printf_literals(self):
        with tempfile.TemporaryDirectory() as td:
            elf = self._build_probe(td, [
                'static uint32_t module_init(uint32_t b) {',
                '    (void)b;',
                '    printf("[mod] hello world\\n");',
                '    printf("value=%u\\n", 42);',
                '    return MODULE_INIT_MAGIC;',
                '}',
            ])
            tc = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT))
            builder = ModuleBuild(tc, firmware_path=str(FIRMWARE_ROOT))
            relocs = builder._extract_relocations(elf)
        # Exactly two R_ARM_ABS32 entries against .text (the two literal pool slots)
        self.assertEqual(2, len(relocs), f"expected 2 relocs, got {relocs}")
        # Offsets must be 4-byte aligned
        for off in relocs:
            self.assertEqual(0, off % 4, f"offset 0x{off:x} not word-aligned")
        # Offsets must fall within .text (i.e. >= hook_table_end = 104)
        for off in relocs:
            self.assertGreaterEqual(off, 104)

    def test_extract_relocations_empty_for_no_literals(self):
        with tempfile.TemporaryDirectory() as td:
            elf = self._build_probe(td, [
                'static uint32_t module_init(uint32_t b) {',
                '    (void)b;',
                '    return MODULE_INIT_MAGIC;',
                '}',
            ])
            tc = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT))
            builder = ModuleBuild(tc, firmware_path=str(FIRMWARE_ROOT))
            relocs = builder._extract_relocations(elf)
        self.assertEqual([], relocs)

    def test_extract_relocations_skips_hook_table(self):
        """Hook-table R_ARM_ABS32 entries must be filtered out — firmware
        still dispatches via offset + slot_addr, not via patched absolutes."""
        with tempfile.TemporaryDirectory() as td:
            elf = self._build_probe(td, [
                'static uint32_t module_init(uint32_t b) {',
                '    (void)b; return MODULE_INIT_MAGIC;',
                '}',
            ])
            tc = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT))
            builder = ModuleBuild(tc, firmware_path=str(FIRMWARE_ROOT))
            relocs = builder._extract_relocations(elf)
            # No relocs because the only ABS32 in this module is the
            # hook-table slot for module_init, which we must skip.
            self.assertEqual([], relocs)


if __name__ == "__main__":
    unittest.main()
```

**Step 2: Run tests, verify they fail**

Run: `/home/user/.local/bin/pytest qmk/QMKata/test_module_relocations.py -v`
Expected: FAIL — method `_extract_relocations` does not exist.

**Step 3: Add `-Wl,-q` to link command**

Modify `GccToolchain.link()` to include `-Wl,-q` so relocations are preserved in the linked ELF. Update `link_command` assembly (line 143):

```python
link_command = [gcc, "-nostdlib", "-nostartfiles", "-Wl,-q"]
```

**Step 4: Implement `_extract_relocations()` in `ModuleBuild.py`**

Add at end of class (before `_assemble`):

```python
def _extract_relocations(self, elf_file):
    """Extract R_ARM_ABS32 relocations targeting .text (literal pool).

    Returns a sorted list of byte offsets (into the final binary) where
    a 32-bit absolute address sits that must be rebased from link-time
    (ORIGIN=0) to runtime (slot_addr) by adding slot_addr.

    Relocations against .hook_table are filtered out because firmware
    dispatch already adds slot_addr to those offsets at call time.
    Other reloc types (R_ARM_THM_CALL etc.) are ignored because
    external symbols like printf are resolved at link time via the
    PROVIDE() symbol map.

    NOTE: the .text-only filter assumes module_linker.ld merges
    .rodata* into .text. If that changes, .rodata targets must be
    added or literal-pool rebasing will silently break.
    """
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
    from elftools.elf.enums import ENUM_RELOC_TYPE_ARM

    R_ARM_ABS32 = ENUM_RELOC_TYPE_ARM['R_ARM_ABS32']
    offsets = []
    with open(elf_file, "rb") as f:
        elf = ELFFile(f)
        # For ET_EXEC (our case — module_linker.ld produces an executable
        # with ORIGIN=0), r_offset is already the VMA, which for us
        # equals the file offset into the raw binary. For ET_REL we'd
        # add sh_addr, but we never link modules with -r.
        is_exec = elf['e_type'] == 'ET_EXEC'
        for sec in elf.iter_sections():
            if not isinstance(sec, RelocationSection):
                continue
            target = elf.get_section(sec['sh_info'])
            if target.name != ".text":
                continue
            base = 0 if is_exec else target['sh_addr']
            for r in sec.iter_relocations():
                if r['r_info_type'] != R_ARM_ABS32:
                    continue
                offsets.append(base + r['r_offset'])
    return sorted(offsets)
```

**Correctness note (caught in Task 2 code review):** earlier drafts of this
plan added `target['sh_addr'] + r['r_offset']` unconditionally. That
double-counts on ET_EXEC output — `r_offset` is already the VMA, so adding
`sh_addr` produces offsets past the end of the binary. The test must
verify offsets both fall within the binary AND dereference to plausible
link-time string addresses; a mere `>= 104` range check is insufficient.

**Step 5: Run tests, verify they pass**

Run: `/home/user/.local/bin/pytest qmk/QMKata/test_module_relocations.py -v`
Expected: all 3 pass.

**Step 6: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/GccToolchain.py qmk/QMKata/ModuleBuild.py qmk/QMKata/test_module_relocations.py
git commit -m "feat(module): extract R_ARM_ABS32 relocations from linked ELF

Adds ModuleBuild._extract_relocations() which reads R_ARM_ABS32
entries from the linked ELF (with -Wl,-q preserving them) and
returns byte offsets of literal-pool words that need slot_addr
added at load time. Filters to .text targets only; hook-table
relocations are skipped because dispatch already adds slot_addr
at call time. Foundation for runtime PIC string access."
```

---

## Task 3: Host — emit reloc table into binary, populate header fields

**Files:**
- Modify: `qmk/QMKata/ModuleBuild.py` (`build()` to pass elf to `_assemble()`, `_assemble()` to append reloc table)
- Modify: `qmk/QMKata/test_module_build_integration.py` (test reloc table contents for null_module)

**Carried forward from Task 1 review (I3):** Extend `test_header_layout_v2` in the
same test file to assert specific `init_off` and `deinit_off` values for the null_module
build, not just ranges. This is the mechanical field-order guard between Python
`struct.pack` and the C `module_header_t` — size alone (`_Static_assert`) cannot catch
a swapped field. Do it as part of this task's test update since the invariant becomes
meaningful once a real module with known init/deinit is built.

**Step 1: Write failing test for reloc table placement**

Add to `test_module_build_integration.py`:

```python
def test_null_module_has_one_reloc_for_init_string(self):
    """null_module calls printf on one string literal. After the build,
    reloc_count should be 1 and the pointed-to word must match the
    link-time address of that string."""
    builder = ModuleBuild(
        GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(FIRMWARE_ROOT)),
        firmware_path=str(FIRMWARE_ROOT),
    )
    null_src = ROOT / "module_examples" / "null_module.c"
    result = builder.build(str(null_src))
    self.assertIsNotNone(result, builder.last_error)
    b = result["binary"]
    reloc_off = int.from_bytes(b[28:32], "little")
    reloc_count = int.from_bytes(b[32:36], "little")
    self.assertEqual(1, reloc_count, f"expected 1 reloc, got {reloc_count}")
    self.assertGreater(reloc_off, 104)  # past hook table
    self.assertLessEqual(reloc_off + 4, len(b))
    target_offset = int.from_bytes(b[reloc_off:reloc_off+4], "little")
    # The target offset points at a literal-pool word. That word holds
    # the link-time-absolute address of the "[mod] null_module init" string.
    word = int.from_bytes(b[target_offset:target_offset+4], "little")
    # String must exist at `word` offset and begin with "[mod]"
    self.assertLess(word, len(b))
    self.assertEqual(b"[mod]", b[word:word+5])
```

This requires `null_module.c` to be rewritten to use the new ergonomic `printf("[mod] null_module init\n")` form. Do that rewrite now (Task 5 formalises it but the test needs it):

```c
// null_module.c (simplified)
#include "module_api.h"
extern int printf(const char *fmt, ...);
static uint32_t module_init(uint32_t module_base) {
    (void)module_base;
    printf("[mod] null_module init\n");
    return MODULE_INIT_MAGIC;
}
MODULE_HOOK_TABLE
const void *module_hook_table[MODULE_HOOK_MAX] = {
    [MODULE_HOOK_INIT] = module_init,
};
```

**Step 2: Run tests, verify they fail**

Run: `/home/user/.local/bin/pytest qmk/QMKata/test_module_build_integration.py::ModuleBuildIntegrationTest::test_null_module_has_one_reloc_for_init_string -v`
Expected: FAIL — `reloc_count` is 0 because `_assemble()` doesn't append the table.

**Step 3: Thread ELF path through `build()` → `_assemble()`**

In `build()`, capture `elf_file` (already exists as local var), call `relocs = self._extract_relocations(elf_file)` after step 3 (linking), and pass to `_assemble(raw_bin, relocs)`:

```python
# After link step, before elf2bin
relocs = self._extract_relocations(elf_file)
...
return self._assemble(raw_bin, relocs)
```

In `_assemble(self, raw_bin, relocs)`:

Before building the header, append the reloc table to the raw binary:

```python
if relocs:
    reloc_off = len(raw_bin)
    reloc_count = len(relocs)
    reloc_bytes = b"".join(struct.pack("<I", off) for off in relocs)
    raw_bin = raw_bin + reloc_bytes
else:
    reloc_off = 0
    reloc_count = 0
```

Then use `reloc_off` and `reloc_count` in the header pack call (replacing the two zeros placeholder from Task 1).

Note: `code_size` in header must equal `len(raw_bin)` AFTER appending reloc table, since CRC covers the full binary and firmware will read `code_size` bytes from Flash. The existing `len(raw_bin)` call in `_assemble` already uses the post-append length if we append before the pack.

**Step 4: Run tests, verify they pass**

Run: `/home/user/.local/bin/pytest qmk/QMKata/ --ignore=qmk/QMKata/test_module_tab_hook_bitmap.py -v`
Expected: all tests pass (module_build_integration, module_relocations, others).

**Step 5: Manually verify binary layout**

Rebuild null_module and inspect:

```bash
cd /home/user/qmk/qmk-tools/qmk/QMKata
/home/user/qmk/venv/bin/python3 -c "
import sys; sys.path.insert(0,'.')
from GccToolchain import GccToolchain
from ModuleBuild import ModuleBuild
from keyboards.KeychronQ3Max import KeychronQ3Max
FW='/home/user/qmk/keychron_qmk_firmware'
b = ModuleBuild(GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=FW), firmware_path=FW)
r = b.build('module_examples/null_module.c')
open('/tmp/null_v2.bin','wb').write(r['binary'])
print('size:', r['size'], 'hooks:', r['hooks'])
"
xxd /tmp/null_v2.bin
```

Expected: header at 0x00-0x27 has version=2, reloc_off ≠ 0, reloc_count = 1. Reloc table at end of binary holds one 4-byte offset. That offset points at a word in the literal pool holding the address of the `[mod]` string.

**Step 6: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/ModuleBuild.py qmk/QMKata/test_module_build_integration.py \
        qmk/QMKata/module_examples/null_module.c
git commit -m "feat(module): emit relocation table after .text

ModuleBuild now appends a compact reloc table (4 bytes per entry,
little-endian offset) to every module binary and populates
header.reloc_off + header.reloc_count. Firmware still ignores
these fields; application comes in the next firmware commit.

Simplifies null_module.c to use plain printf() literal — the
relocation machinery will fix up the literal-pool address at
load time. static const init_msg[] + module_base+offset pattern
is no longer needed."
```

---

## Task 4: Firmware — apply relocations during flash write

**Carried forward from Task 3 code-quality review:**
- **I5 (header field-swap protection):** The Python↔C header pack currently
  pins `init_off`/`deinit_off` values but not `reloc_off`/`reloc_count`. Add a
  firmware-side empirical check: during `module_load()` verify that
  `reloc_off == 0 iff reloc_count == 0` and that `reloc_off + reloc_count*4 <= code_size`.
  A `struct.pack` field swap between these two would manifest as
  `reloc_off < MODULE_HEADER_SIZE` or `reloc_count` absurdly large, both
  caught by these bounds. xprintf on failure at all three call sites
  (`module_load`, `module_unload`, `module_boot_scan`) — same pattern as the
  version-mismatch log from Task 1 I4.
- **I2 (linker-script/extractor coupling comment):** Add a comment to
  `qmk/QMKata/module_linker.ld` above `*(.rodata*)` noting that
  `_extract_relocations` in `ModuleBuild.py` filters ABS32s to `.text` only
  and relies on `.rodata` being merged here. Edit the linker script in the
  same commit that touches firmware so the coupling is visible from both
  ends. (Trivial one-line comment; not worth its own task.)

**Files:**
- Modify: `keyboards/keychron/common/module/module_flash.h` (declare reloc-aware write helper if needed)
- Modify: `keyboards/keychron/common/module/module_flash.c` (add `module_flash_write_with_relocs()` or extend existing writer)
- Modify: `keyboards/keychron/common/module/module_loader.c` (call reloc-aware writer in `module_load()`, check `version == 2`)

**Step 1: Read current flash write path**

Read: `keyboards/keychron/common/module/module_flash.c` to understand `module_flash_write_slot()` (or equivalent). Identify:
- How word-aligned writes are issued (HAL function name, word buffer)
- Whether writes happen in one pass over the input buffer or chunked
- Where to inject the reloc patch logic

**Step 2: Design the patch approach**

Because Flash words must be erased (all 1s) before programming, we cannot write the original bytes then patch. The fix must happen **in the source buffer before each word write**:

```c
/* Walk the reloc table once, build a sorted in-memory list of
   (offset, fixup_delta) pairs. During the write loop, for each
   output word whose offset matches a reloc entry, add slot_addr
   to the 32-bit value in RAM before passing it to flashProgram. */
```

Since the reloc list is already sorted ascending (ModuleBuild guarantees this), a single cursor walking both the write stream and the reloc list in lockstep is O(n + m).

Add a helper to `module_flash.c`:

```c
/* Apply ABS32 fix-ups in place on a RAM buffer, then call the normal
   flash write routine. buf points to the full module image starting
   at the module header; len is total bytes including reloc table.
   reloc_off and reloc_count come from the module header. */
bool module_flash_write_with_relocs(uint32_t slot_addr,
                                    uint8_t *buf, size_t len,
                                    uint32_t reloc_off, uint32_t reloc_count) {
    if (reloc_count > 0) {
        /* Bounds-check the reloc table itself lives in buf */
        if (reloc_off + reloc_count * 4u > len) return false;
        const uint32_t *table = (const uint32_t *)(buf + reloc_off);
        for (uint32_t i = 0; i < reloc_count; i++) {
            uint32_t patch_off = table[i];
            /* Every patch site must be 4-aligned, within buf, and must
               not overlap the reloc table itself. */
            if ((patch_off & 3u) != 0) return false;
            if (patch_off + 4u > reloc_off) {
                /* Patch site must lie before the reloc table. */
                return false;
            }
            uint32_t *word = (uint32_t *)(buf + patch_off);
            *word += slot_addr;
        }
    }
    return module_flash_write_slot(slot_addr, buf, len);
}
```

Call sites: wherever `module_flash_write_slot()` is invoked today with the full image, replace with the new helper, passing `header->reloc_off` and `header->reloc_count`. If the existing writer is the only one and it already takes the buffer + length, this is a two-line change in `module_loader.c::module_load()`.

**Step 3: Version gate in `module_load()`**

Before any reloc processing, reject v1 modules. **Carried forward from Task 1 review
(I4):** the current codebase has three version-mismatch return paths in `module_loader.c`
(`module_load`, `module_unload`, `module_boot_scan`) that silently return false; the plan's
line 47 promised an xprintf log. Add the log line to all three call sites, not just
`module_load`:

```c
if (header->version != MODULE_HEADER_VERSION) {
    xprintf("mod load slot=%u rejected: version %u != %u\n",
            slot_id, header->version, MODULE_HEADER_VERSION);
    return false;
}
```

**Step 4: Build, flash, verify (the real test)**

Full firmware rebuild + flash + module upload is the integration test. There is no pure-host way to validate STM32 flash programming — it must run on hardware.

```bash
cd /home/user/qmk/keychron_qmk_firmware
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
```

Expected: clean build. Then flash via DFU, upload null_module via QMKata GUI, observe console.

Expected console output on first upload:

```
[mod] null_module init
mod load slot=0 init OK rc=0x600dbeef
```

(Message printed BEFORE the init-OK line because init runs `printf` before returning.)

No garbled characters. No repeated corruption pattern. The string is clean.

**Step 5: Bound the failure modes**

Things that can go wrong and how to recognize them:

| Symptom | Likely cause |
|---|---|
| Console shows garbage bytes instead of `[mod] null_module init` | Reloc not applied; patch site address computed wrong |
| `mod load slot=0 rejected: version X != 2` | Host built v1 or header packing mismatch |
| Firmware hangs on module_load | Reloc offset out of bounds or writing to wrong flash address; add xprintf traces |
| `init BAD rc=0x...` but string prints fine | Non-reloc issue; ABI or `MODULE_INIT_MAGIC` mismatch |
| `xxd` of `/tmp/null_v2.bin` shows `reloc_count=0` | Task 3 regression; reloc extraction or append broke |

If garbage: add an `xprintf("mod reloc[%u]: off=0x%lx before=0x%lx after=0x%lx\n", ...)` trace inside the reloc application loop to confirm each patch site is correct. Expected for null_module at slot 0x08008000: one reloc, patch site somewhere around 0x094, before=0xA8, after=0x080080A8.

**Step 6: Commit firmware change**

Only commit after hardware verification succeeds. Per user preference (verification-before-completion), do not commit unverified firmware changes.

```bash
cd /home/user/qmk/keychron_qmk_firmware
git add keyboards/keychron/common/module/module_flash.c \
        keyboards/keychron/common/module/module_flash.h \
        keyboards/keychron/common/module/module_loader.c
git commit -m "feat(module): apply ABS32 relocations during flash write

The module loader now walks the reloc table embedded in each v2
module binary and, for every 4-byte offset, adds slot_addr to the
32-bit word in RAM before passing it to flashProgram. This rebases
literal-pool entries from link-time (ORIGIN=0) to actual slot
addresses, making plain printf(\"literal\") work transparently from
any function in any module.

Rejects v1 modules with a clear log line; existing modules must be
rebuilt against the new host builder."
```

---

## Task 5: Clean up example modules — scope 5A (strip `module_base` from both init AND deinit)

**Why 5A (not the originally-planned deinit-only revert):** Hardware test (this session) showed `hooks_template.c` hangs on load because its `module_init` body does `module_base + (uintptr_t)init_msg` — which double-relocates the string (Option A already rebased the literal pool entry, so `init_msg` is already the absolute runtime address; adding `module_base` again corrupts it). Null module loads cleanly because it uses plain `printf("literal")`. Root cause: the `module_base` parameter advertises a usage pattern that is now actively wrong under Option A. Every copy-paste of the template's old init body will silently hang any keyboard it runs on. The strongest fix is to **delete the parameter entirely from both init and deinit ABIs**, so the broken pattern becomes a compile error. Modules that genuinely need their load address can declare `extern char __module_start__;` (or similar linker-provided symbol) — deliberately ugly to discourage casual use.

**Files:**
- Modify: `qmk/QMKata/module_examples/null_module.c` (signature → `void`, drop `(void)module_base;`)
- Modify: `qmk/QMKata/module_examples/hooks_template.c` (strip `init_msg`/`deinit_msg` statics + `module_base + ...` plumbing; both init and deinit signatures → `void`; drop the comment on line ~48 about resolving addresses relative to module_base)
- Modify: `qmk/QMKata/module_api.h` (revert init AND deinit signature doc comments to `uint32_t(void)`)
- Modify: `keyboards/keychron/common/module/module_loader.h` (both `module_init_fn_t` AND `module_deinit_fn_t` → `uint32_t(*)(void)`; update the surrounding ABI comment at line ~28 to state the rationale for taking no parameter)
- Modify: `keyboards/keychron/common/module/module_loader.c` (both call sites — lines 354 and 533 for init, 410 for deinit — drop the `slot_addr` arg)
- Modify: `qmk/QMKata/test_module_build_integration.py` if it pins the init/deinit signature (check; skip if it doesn't).

**Step 1: Strip PIC boilerplate from hooks_template.c and simplify both lifecycle signatures**

Target for both example modules' lifecycle hooks:

```c
static uint32_t module_init(void) {
    printf("[mod] init\n");
    return MODULE_INIT_MAGIC;
}
static uint32_t module_deinit(void) {
    printf("[mod] deinit\n");
    return 0;
}
```

Delete `static const char init_msg[]` and `static const char deinit_msg[]` entirely from `hooks_template.c`. Delete the comment block above them that describes the PIC pattern. Delete the `static uint32_t module_init(uint32_t module_base);` / `module_deinit(uint32_t module_base);` forward declarations' `uint32_t module_base` param.

In `null_module.c`, change `static uint32_t module_init(uint32_t module_base) { (void)module_base; ... }` to `static uint32_t module_init(void) { ... }`. Delete the `(void)module_base;` line.

**Step 2: Revert ABI in module_api.h and firmware**

- `module_api.h`: both signature doc-comments → `uint32_t module_init(void)` and `uint32_t module_deinit(void)`. If the current text still mentions "module_base" as a parameter, remove those references. The Option A rationale belongs in a one-sentence note: "Module code accesses its own .rodata through normal C references; the loader has already rebased literal-pool addresses to the slot's runtime location."
- `module_loader.h`: 
  ```c
  typedef uint32_t (*module_init_fn_t)(void);
  typedef uint32_t (*module_deinit_fn_t)(void);
  ```
  Update the preceding comment to explain that runtime relocation makes the module load address invisible to module code; it's still available to the firmware via `slot_addr` for logging/validation but is not passed into the module.
- `module_loader.c` line 354: `uint32_t rc = init_fn();`
- `module_loader.c` line 410: `uint32_t rc = deinit_fn();`
- `module_loader.c` line 533: `uint32_t rc = init_fn();`

**Step 3: Rebuild, run full test suite, flash, verify**

```bash
cd /home/user/qmk/qmk-tools
/home/user/.local/bin/pytest qmk/QMKata/ --ignore=qmk/QMKata/test_module_tab_hook_bitmap.py -v
```

Expected: all tests pass.

```bash
cd /home/user/qmk/keychron_qmk_firmware
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
```

Flash, upload `hooks_template`, trigger a module deinit (re-upload to same slot or explicit unload), confirm `[mod] deinit` prints cleanly on console.

**Step 4: Commit (two commits, one per repo)**

Host (qmk-tools, feat/combo-modules):
```
refactor(module): drop module_base param from init/deinit ABI

Runtime ABS32 relocation (Option A) rebases literal-pool addresses
to the slot's runtime location at load time, so module code can
reference its own .rodata through plain C without arithmetic on a
load-address parameter. Keeping module_base in the init/deinit
signature was actively dangerous: hooks_template.c used the old
PIC pattern `module_base + (uintptr_t)sym` which now
double-relocates — the literal pool already holds the rebased
address, so adding module_base again points into never-never-land
and hard-faults the MCU during module_load. Removing the parameter
turns the broken pattern into a compile error.

Also strips the static const char init_msg[]/deinit_msg[]
scaffolding from hooks_template and simplifies null_module's
init to use plain printf("literal").
```

Firmware (keychron_qmk_firmware, feat/combo-modules):
```
refactor(module): drop module_base from init/deinit ABI

Mirror host ABI change: module_init_fn_t and module_deinit_fn_t
now take void and are called without slot_addr. Module code no
longer needs its load address because runtime relocations rebase
literal-pool entries at flash-program time.
```

---

## Out of scope (follow-ups)

- `R_ARM_REL32` and other reloc types — not produced by current module patterns; add handling only if a real module needs it.
- Relocations across the hook table (would require changing firmware dispatch to treat hook entries as absolute pointers; deferred, purely cosmetic).
- Size optimization (varint encoding of reloc offsets, delta encoding) — only if module sizes become a real constraint.
- Signing / authenticity — orthogonal; CRC only detects corruption. A separate task.
- **Boot-scan reloc-integrity check (Task 4 I2):** `module_boot_scan` trusts
  that a slot's flashed bytes are already patched. If something corrupts the
  literal pool post-flash, we'd crash on first hook dispatch. A cheap guard
  would be to verify `*(uint32_t*)(slot_addr + table[i]) - slot_addr < code_size`
  for each reloc entry during boot scan — patched words should point inside
  the slot. Defer until we see a real corruption case.
- **Host unit test for reloc patch math (Task 4 I3):** The reloc fix-up loop
  in `module_flash_write_with_relocs` is pure arithmetic + bounds checks and
  could be unit-tested on the host with a synthetic buffer. Today it is only
  exercised by on-hardware flash of null_module and combo_layer_filter (the
  latter being the `reloc_count == 0` fast path). Factor out the patch loop
  into a pure-C helper that tests can link against, or mirror the logic in
  a Python prototype that the host test suite cross-validates against the
  emitted reloc table. Defer until we have a second ABS32-using module.
- **Linker script `.hook_table` size bug (discovered Task 3):** `module_linker.ld`
  uses `. = 40 + 64;` inside `.hook_table`, which is interpreted as a
  section-relative location assignment (104 bytes INTO the section, not 104 bytes
  from binary start). Because the section is placed at VMA 40, the hook table
  ends up occupying bytes 40..144 with 40 bytes of unused padding between the
  real hook slots (40..104) and `.text` (starts at 144 = 0x90). Firmware
  dispatch is unaffected — it reads `init_off` from the header — but each module
  binary carries 40 wasted bytes. Fix would be to change the linker script to
  `. = 64;` (section-relative, producing a 64-byte section) AND update the comment;
  this changes `.text` VMA to 104 and will break the `init_off=145` pin in
  `test_header_field_order_for_null_module`. Defer to a dedicated commit that
  lands both changes together.
- **Remaining hardware verifications (Task 5 post-hoc):**
  Two checklist items are not yet exercised — deinit on re-upload, and cold
  boot + first upload no-hang regression. Both paths are structurally identical
  to the load paths already exercised (same `module_flash_write_with_relocs`,
  same literal-pool rebasing, same dispatcher), so risk is low; drive them
  opportunistically rather than blocking on a formal session.
- **`process_combo_event` hook exercise:** The template's
  `process_combo_event` hook is live in the dispatch table but was not observed
  firing during hardware test because the test keymap's triggered combos all
  had real keycodes — QMK core only calls `process_combo_event` for
  keycode-less / action-type combos (those using `COMBO_ACTION(name)` or
  equivalent `{..., COMBO_END}` without a final keycode). To exercise the
  hook, add one action-type combo to the keymap under test, trigger it, and
  confirm `[mod] process_combo_event idx=N pressed=1` + `pressed=0` appear
  on console. Purely a completeness check, not blocking.

## Verification checklist

After all tasks complete:

- [x] `pytest qmk/QMKata/ --ignore=qmk/QMKata/test_module_tab_hook_bitmap.py` all green — 20 passed (Task 5A implementer run)
- [x] `qmk compile` clean — `keychron_q3_max_ansi_encoder_keychron.bin` builds, 106886 bytes with Task 4+5 applied
- [x] `null_module.c` contains **zero** occurrences of `module_base + (uintptr_t)` or `static const char` — verified post-Task-5A
- [x] `hooks_template.c` same — verified post-Task-5A; init/deinit are bare `printf("[mod] init\n")` / `printf("[mod] deinit\n")`, no statics, no forward-decl params
- [x] Hardware: null_module upload prints `[mod] null_module init` cleanly (verified by user before Task 5)
- [x] Hardware: hooks_template upload after Task 5 loads cleanly — all combo-path hooks fire and print correctly (`combo_should_trigger`, `get_combo_term`, `get_combo_must_hold`, `get_combo_must_tap`, `get_combo_must_press_in_order`, `process_combo_key_release`, `combo_ref_from_layer`); no hang, no hardfault. Confirms the double-relocation root cause was correctly diagnosed and the Option A fixup path handles 13 R_ARM_ABS32 entries across .text and literal-pool entries pointing into merged .rodata correctly.
- [x] Hardware: `process_combo_event` absence on triggered combos confirmed as expected QMK-core behaviour (core only calls `process_combo_event` for keycode-less / action-type combos — combos with a real `keycode` go through `process_record`/`action_tapping_process` instead). Not a module-system defect. Documented below under Out-of-scope follow-ups if a future action-combo test is desired.
- [ ] Hardware: re-upload (triggers unload → load) prints deinit message cleanly — not yet exercised; requires uploading a second module to the same slot or triggering explicit unload through QMKata.
- [ ] Hardware: second upload after reboot still works (no first-upload-hang regression) — not yet exercised.
