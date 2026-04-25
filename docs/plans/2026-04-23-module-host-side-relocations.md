# Module Host-Side Relocations Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix cold-boot module hook loss by moving ABS32 literal-pool relocation from firmware to host, so flash bytes match the CRC that was computed over them and `module_boot_scan` re-installs hooks correctly on every boot.

**Architecture:** Host (QMKata) applies R_ARM_ABS32 rebasing at load time (when `slot_id` is already known), then CRCs the relocated image and ships it. Firmware writes the received bytes to flash verbatim, CRCs them once at load and again at boot scan — both matches, both succeed. Header v1 (reset on first prototype-with-compatibility release): drops `reloc_off`/`reloc_count` fields (dead once the firmware no longer walks them) and shrinks from 40 bytes to 32 bytes. Hook-table linker-script padding bug (40 unused bytes) is fixed in the same plan since both changes touch the same offsets; bundling avoids churning the same test assertions twice.

**Tech Stack:** C (STM32F4 HAL, QMK), Python 3 (PyQt6, pyelftools, zlib), arm-none-eabi-gcc/ld.

---

## Context summary

### The bug (already root-caused this session)

`module_load` in `keychron_qmk_firmware/keyboards/keychron/common/module/module_loader.c:260` validates the host-signed CRC against the **pre-reloc** RAM buffer, then `module_flash_write_with_relocs` (same file, called at line 310) **mutates the buffer in place** to rebase ABS32 targets from `ORIGIN=0` link-time addresses to absolute slot addresses, then flashes the mutated bytes. `module_boot_scan` at line 481 CRCs the post-reloc flash bytes against the pre-reloc CRC stored in the header — they cannot match when `reloc_count > 0`, so the slot is rejected with a silent `continue` (no `xprintf`). Hooks never re-install in SRAM and the module appears dead until the next `module_load`.

### Chosen fix — Option C ("host does the relocations")

Host gains `slot_addr` from the already-existing slot combo in `ModuleTab`, applies relocations to the binary before CRC'ing, ships the finished image. Firmware deletes the reloc walk, flashes verbatim, CRCs match on every boot.

Killer consequence: **the entire Task-4 firmware reloc-walk code (`module_flash_write_with_relocs` and friends) goes away**. Boot scan becomes correct without any boot-scan change — symmetry between load and scan is what fixes the bug.

### Decisions locked in this session

1. `_prepare_binary_for_load` owns the **only** CRC write; `_assemble` leaves the `crc32` field as zero. Simpler — one CRC, one owner.
2. `MODULE_HEADER_VERSION` resets to **1**. Prototype phase; device-firmware update wipes all modules; no compat burden.
3. Shared constants (`MODULE_FLASH_BASE`, `MODULE_FLASH_SLOT_SIZE`) stay duplicated across host/firmware with a "must match" comment. Existing pattern; no build-system change to introduce a shared header.
4. Single plan, not sliced. Bundles CRC fix + header shrink + `.hook_table` padding fix because all three touch the same offsets and tests.
5. Plan only for now. No code execution yet.

### What this plan does NOT touch

- QMKata `save_combo` action-combo ergonomics — user declined previously.
- Module sysex protocol framing — unchanged, still `[ID_MODULE, slot, off_lo, off_hi, declared_len, payload…]`.
- Two deferred Task-4 hardware checks from the previous plan (re-upload deinit, cold-boot first-upload) — they become trivially exercised by the new verification step.
- Any firmware flash-driver changes (erase/write primitives unchanged).

---

## Invariants that must hold after the change

- **Host and firmware CRC functions produce identical bytes for the same input.** `validate_module_crc` in `module_loader.c:98-131` and `zlib.crc32(bytes).& 0xFFFFFFFF` in `_prepare_binary_for_load` must stay bit-identical. Test: Task 2 asserts this directly.
- **Flash bytes == CRC'd bytes.** Firmware never mutates module data between load-CRC-check and boot-CRC-check. Erase+write is a byte-copy; no fixups.
- **Hook table layout unchanged semantically.** Firmware dispatch still adds `slot_addr` to hook-table offsets at call time — those stay as offsets, not absolute addresses, and no ABS32 relocs are emitted against `.hook_table` (existing host filter keeps this true).
- **Link-time `ORIGIN=0` for modules.** Host-side reloc apply depends on ABS32 targets initially holding a link-time address in `[0, code_size)`. No change to `module_linker.ld` MEMORY definition.
- **No flash write after the main image flash.** Header CRC is not patched after-the-fact (rules out Option B/B' we discussed and rejected). One erase, one write per load.

---

## File inventory

### Host (`/home/user/qmk/qmk-tools`, branch `feat/combo-modules`)

- Modify: `qmk/QMKata/ModuleBuild.py` — `_assemble` signature/body; drop reloc-table append; add `relocs` to result dict; add `apply_relocations_and_crc(binary, relocs, slot_addr)` helper; drop reloc-header fields; shrink `MODULE_HEADER_SIZE` / `MODULE_HOOK_TABLE_OFF`; bump `MODULE_HEADER_VERSION` to 1.
- Modify: `qmk/QMKata/ModuleTab.py` — `_prepare_binary_for_load(slot_id)` new signature; pass `slot_id` from `load_module`.
- Modify: `qmk/QMKata/module_linker.ld` — shrink `.module_header` from 40 to 32 bytes (LONG() count 10→8); drop `.hook_table`'s explicit `. = 40 + 64` padding (let `ALIGN(4)` close naturally at 64 bytes).
- Modify: `qmk/QMKata/test_module_relocations.py` — flip assertions from "reloc table appended" to "relocs returned in result dict"; add RED test for `apply_relocations_and_crc`.
- Modify: `qmk/QMKata/test_module_build_integration.py` — update hardcoded offsets (header 40→32; hook table end 104→96 if asserted).
- Modify: `qmk/QMKata/test_module_tab_hook_bitmap.py` — update `MODULE_HEADER_SIZE` / `MODULE_HOOK_TABLE_OFF` imports-consumers; add load-time CRC test covering relocated image.
- Modify: `qmk/QMKata/module_examples/null_module.c`, `hooks_template.c`, `combo_layer_filter.c` — no code change expected; these are linked against `module_linker.ld` and rebuild unchanged.

### Firmware (`/home/user/qmk/keychron_qmk_firmware`, branch `feat/combo-modules`)

- Modify: `keyboards/keychron/common/module/module_loader.h` — drop `reloc_off`/`reloc_count` from `module_header_t`; bump `MODULE_HEADER_VERSION` to 1; update `_Static_assert(sizeof(module_header_t) == …)` from 40 to 32.
- Modify: `keyboards/keychron/common/module/module_loader.c` — delete reloc structural sanity block (lines ~229-252); replace `module_flash_write_with_relocs(...)` call with `module_flash_write(...)`; leave CRC validation at line 260 and boot-scan CRC at line 481 untouched (they now work correctly by symmetry).
- Modify: `keyboards/keychron/common/module/module_flash.c` — delete `module_flash_write_with_relocs`. Keep `module_flash_write`, `module_flash_erase_sector`, `module_flash_is_sector_empty`.
- Modify: `keyboards/keychron/common/module/module_flash.h` — drop `module_flash_write_with_relocs` prototype.
- Modify: `keyboards/keychron/common/module/module_dispatch.c` — hook-table offset calculation references `MODULE_HOOK_TABLE_OFF` indirectly via `header->hook_table_off`, so no change needed; verify no hardcoded 40.

### Plan document

- Create: `/home/user/qmk/keychron_qmk_firmware/docs/plans/2026-04-23-module-host-side-relocations.md` (this file).

---

## Build sequence — task checklist

- [ ] Task 1: Host-side RED test — `apply_relocations_and_crc` produces a binary whose ABS32 targets equal link-time + slot_addr and whose stored CRC matches `zlib.crc32(binary-with-crc-zeroed)`.
- [ ] Task 2: Host-side GREEN — implement `apply_relocations_and_crc`; keep the reloc table still appended to binary and the reloc-off/count header fields still populated (so firmware is unchanged so far, still runs old path). Run all host tests.
- [ ] Task 3: Firmware — delete reloc walk. `module_flash_write_with_relocs` → `module_flash_write`. Drop reloc structural sanity block in `module_load`. Build firmware; flash to device; **manually verify cold-boot repro is fixed** with null module. This is the single observable behaviour change for the user.
- [ ] Task 4: Drop `reloc_off` / `reloc_count` from header on both sides. Bump version 2→1. Shrink `MODULE_HEADER_SIZE` 40→32 and `MODULE_HOOK_TABLE_OFF` 40→32. Update `_Static_assert`, `module_linker.ld`, host constants, and all host tests. No behaviour change — pure layout cleanup.
- [ ] Task 5: Fix `.hook_table` linker-script padding. Drop the `. = 40 + 64` forced-end line; let `ALIGN(4)` close naturally. Rebuild modules; confirm hook-table end moves from offset 104→96 (32-byte header + 64-byte table). Update any test that pins hook-table end offset.
- [ ] Task 6: Hardware verification — null module, template module, power cycle each, confirm boot-scan init prints appear.
- [ ] Task 7: Commit and push both repos (existing `feat/combo-modules` branches).

Each task has its own RED → GREEN → commit cycle below.

---

## Task 1: Host-side RED test for `apply_relocations_and_crc`

**Files:**
- Modify: `qmk/QMKata/test_module_relocations.py`

**Step 1: Write the failing test**

Add to `test_module_relocations.py`:

```python
def test_apply_relocations_and_crc_rebases_and_finalises(self):
    """Host must apply ABS32 relocs and write the final CRC over the
    relocated bytes. Firmware will CRC the same bytes on load and boot
    scan and must see the same value.

    Builds null_module, extracts the relocated image for slot 0, then:
      - at every reloc offset, reads the u32 and asserts it equals the
        original link-time address + slot_addr (0x08008000);
      - zeroes the crc32 field (last 4 bytes of header) in a local copy,
        computes zlib.crc32, asserts it equals the stored crc32.
    """
    import zlib
    from ModuleBuild import (
        ModuleBuild,
        MODULE_HEADER_SIZE,
        MODULE_FLASH_BASE,
    )

    with tempfile.TemporaryDirectory() as tc:
        builder = ModuleBuild(tc, firmware_path=str(FIRMWARE_ROOT))
        result = builder.build(str(NULL_MODULE_SRC))
        self.assertIsNotNone(result)
        self.assertIn('relocs', result)
        self.assertGreater(len(result['relocs']), 0,
                           "null_module has a literal pool entry; expect ≥1 reloc")

        slot_id = 0
        slot_addr = MODULE_FLASH_BASE + slot_id * 0x1000

        # Capture the pre-reloc u32 at each reloc offset.
        pre_reloc_targets = [
            struct.unpack_from("<I", result['binary'], off)[0]
            for off in result['relocs']
        ]

        prepared = builder.apply_relocations_and_crc(
            result['binary'], result['relocs'], slot_addr
        )

        # Each reloc target now holds original + slot_addr.
        for off, pre in zip(result['relocs'], pre_reloc_targets):
            post = struct.unpack_from("<I", prepared, off)[0]
            self.assertEqual(post, (pre + slot_addr) & 0xFFFFFFFF,
                             f"reloc at offset {off}: expected "
                             f"0x{(pre+slot_addr)&0xFFFFFFFF:08x}, got 0x{post:08x}")

        # Stored CRC matches zlib.crc32 of the prepared image with crc
        # field zeroed.
        crc_off = MODULE_HEADER_SIZE - 4
        stored_crc = struct.unpack_from("<I", prepared, crc_off)[0]
        verify = bytearray(prepared)
        struct.pack_into("<I", verify, crc_off, 0)
        computed_crc = zlib.crc32(bytes(verify)) & 0xFFFFFFFF
        self.assertEqual(stored_crc, computed_crc,
                         "prepared binary CRC does not match zlib.crc32 of "
                         "itself with crc field zeroed")
```

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest qmk.QMKata.test_module_relocations.ModuleRelocationsTest.test_apply_relocations_and_crc_rebases_and_finalises -v`

Expected: FAIL with `AttributeError: 'ModuleBuild' object has no attribute 'apply_relocations_and_crc'` (or `'relocs'` KeyError if `result` doesn't include it yet).

**Step 3: Commit the RED test**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/test_module_relocations.py
git commit -m "test(module): RED for host-side apply_relocations_and_crc"
```

---

## Task 2: Host-side GREEN — implement `apply_relocations_and_crc`

**Files:**
- Modify: `qmk/QMKata/ModuleBuild.py`
- Modify: `qmk/QMKata/ModuleTab.py`

**Step 1: Add `MODULE_FLASH_BASE` constant to `ModuleBuild.py`**

Near the other constants at the top of the file (after `MODULE_FLASH_SLOT_SIZE`):

```python
# Must match firmware module_flash.h. Duplicated here rather than shared
# via a generated header to keep the build system simple; a mismatch will
# surface as a CRC failure on the device, which is loud and localised.
MODULE_FLASH_BASE = 0x08008000
```

**Step 2: Change `_assemble` to return relocs in the result and leave crc field zeroed**

```python
# In _assemble, at the bottom, REPLACE the CRC-write block with:

# CRC is written by apply_relocations_and_crc at load time over the
# relocated bytes. _assemble leaves the crc field as zero so that
# _prepare_binary_for_load owns the single final CRC computation.
# See module_loader.c validate_module_crc for the firmware side.
final_bin = bytes(final_bin)

fits_slot = len(final_bin) <= MODULE_FLASH_SLOT_SIZE
if not fits_slot:
    self.last_error = (
        f"module binary exceeds slot size "
        f"({len(final_bin)} > {MODULE_FLASH_SLOT_SIZE})"
    )
    return None

return {
    'binary': final_bin,
    'hook_bitmap': hook_bitmap,
    'hooks': hooks,
    'size': len(final_bin),
    'fits_slot': fits_slot,
    'relocs': list(relocs),  # sorted ascending; consumed by apply_relocations_and_crc
}
```

Note: `_assemble` still appends the reloc table to the binary and still populates `reloc_off`/`reloc_count` in the header for this task. Firmware has not changed yet; we keep end-to-end flow working and only swap in the new CRC path. Those fields come out in Task 4.

**Step 3: Add `apply_relocations_and_crc`**

```python
def apply_relocations_and_crc(self, binary, relocs, slot_addr):
    """Rebase ABS32 targets from link-time (ORIGIN=0) to absolute slot
    addresses, then compute and embed the final CRC.

    Called at load time from ModuleTab._prepare_binary_for_load once the
    user has picked a slot. Firmware validates the embedded CRC on both
    module_load (RAM buffer) and module_boot_scan (flash XIP); flash
    bytes equal these bytes, so CRC matches on every boot.

    Arguments:
      binary:    bytes/bytearray from _assemble. CRC field must be zero.
      relocs:    sorted list of u32 offsets into `binary`, from
                 _extract_relocations. Empty list is valid (null module
                 with no literal pool still has at least the init_fn
                 literal; an empty list means there truly are no ABS32
                 targets to rebase).
      slot_addr: firmware MODULE_FLASH_GET_SLOT_ADDR(slot_id) equivalent,
                 i.e. MODULE_FLASH_BASE + slot_id * MODULE_FLASH_SLOT_SIZE.

    Returns a new bytes object with relocs applied and the crc32 header
    field filled in. Does not mutate the input.
    """
    out = bytearray(binary)
    for off in relocs:
        # Bounds — belt-and-braces; _extract_relocations already ensures
        # offsets fall inside .text, which is inside code_size, which is
        # inside the binary.
        if off + 4 > len(out):
            raise ValueError(
                f"reloc offset {off} out of range (binary {len(out)} bytes)"
            )
        val = struct.unpack_from("<I", out, off)[0]
        struct.pack_into("<I", out, off, (val + slot_addr) & 0xFFFFFFFF)

    crc_off = MODULE_HEADER_SIZE - 4
    if bytes(out[crc_off:crc_off + 4]) != b"\x00\x00\x00\x00":
        raise ValueError("crc field is not zero; _assemble must leave it zero")
    crc_value = zlib.crc32(bytes(out)) & 0xFFFFFFFF
    struct.pack_into("<I", out, crc_off, crc_value)
    return bytes(out)
```

**Step 4: Wire `_prepare_binary_for_load` through ModuleTab**

In `ModuleTab.py`, change signature and body:

```python
def _prepare_binary_for_load(self, slot_id):
    binary = bytearray(self.last_build_result['binary'])
    hook_bitmap = self._selected_hook_bitmap()
    struct.pack_into("<I", binary, 12, hook_bitmap)
    if not (hook_bitmap & (1 << 3)):
        struct.pack_into("<I", binary, 20, 0)
    if not (hook_bitmap & (1 << 4)):
        struct.pack_into("<I", binary, 24, 0)

    # Zero the crc field — apply_relocations_and_crc owns the final write.
    crc_off = MODULE_HEADER_SIZE - 4
    struct.pack_into("<I", binary, crc_off, 0)

    from ModuleBuild import MODULE_FLASH_BASE, MODULE_FLASH_SLOT_SIZE
    slot_addr = MODULE_FLASH_BASE + slot_id * MODULE_FLASH_SLOT_SIZE
    return self.module_build.apply_relocations_and_crc(
        bytes(binary),
        self.last_build_result['relocs'],
        slot_addr,
    )

def load_module(self):
    if self.last_build_result is None:
        self.log("Error: No module built yet")
        return
    slot_id = self.slot_combo.currentData()
    binary = self._prepare_binary_for_load(slot_id)
    self.log(f"Loading module to slot {slot_id} ({len(binary)} bytes)...")
    self.signal_load_module.emit(slot_id, binary)
```

**Step 5: Run tests — watch Task 1 test go GREEN**

Run: `python3 -m unittest discover qmk.QMKata -v`

Expected: all existing tests still pass, new test passes. If `test_module_tab_hook_bitmap` fails because of CRC mismatch — that test's "load-time CRC recompute" path already exists (line 179) but now lives inside `apply_relocations_and_crc`. Adjust the test to call `apply_relocations_and_crc` instead of recomputing CRC inline.

**Step 6: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/ModuleBuild.py qmk/QMKata/ModuleTab.py qmk/QMKata/test_module_tab_hook_bitmap.py
git commit -m "feat(module): host applies ABS32 relocs and final CRC at load time"
```

---

## Task 3: Firmware — delete reloc walk

**Files:**
- Modify: `keyboards/keychron/common/module/module_loader.c`
- Modify: `keyboards/keychron/common/module/module_flash.c`
- Modify: `keyboards/keychron/common/module/module_flash.h`

**Step 1: Replace `module_flash_write_with_relocs` call in `module_load`**

In `module_loader.c:310`, replace:

```c
if (!module_flash_write_with_relocs(slot_id, slot_addr, (uint8_t*)data,
                                    write_len, hdr->reloc_off,
                                    hdr->reloc_count)) {
    return false;
}
```

with:

```c
if (!module_flash_write(slot_addr, (uint8_t*)data, write_len)) {
    return false;
}
```

Also update the comment block above (currently lines 299-308 describe in-place mutation — now irrelevant). New comment:

```c
/* Write module data to flash verbatim. Host has already applied ABS32
   relocations and embedded the final CRC; flash bytes match the CRC
   the host signed, which is what module_boot_scan re-validates on
   every cold boot. */
```

**Step 2: Delete reloc structural sanity block**

Delete `module_loader.c:220-252` (the entire reloc_off/reloc_count sanity block). Those fields are going away in Task 4, and even before that they can just stay in the header as unused bytes.

**Step 3: Delete `module_flash_write_with_relocs` implementation and prototype**

In `module_flash.c`, delete the whole function body. In `module_flash.h`, delete the prototype.

**Step 4: Build firmware**

```bash
cd /home/user/qmk/keychron_qmk_firmware
make keychron/q3_max/ansi_encoder:keychron
```

Expected: clean build. If `module_loader.c` still references `hdr->reloc_off` anywhere after the block deletion, fix before proceeding.

**Step 5: Rebuild null module on host (existing contract still works — header v2 still has reloc fields, firmware just ignores them)**

In QMKata, rebuild `null_module.c`. Load to slot 0. Expected: `mod load slot=0 init OK` appears.

**Step 6: Power-cycle keyboard — the actual bug-fix verification**

Unplug USB, plug back in. Within ~1s of boot, expected: `mod boot slot=0 init_fn=…` and `mod boot slot=0 init OK` appear in console. **If this line appears, the cold-boot bug is fixed.** If not, stop and debug — do not proceed.

**Step 7: Commit**

```bash
cd /home/user/qmk/keychron_qmk_firmware
git add keyboards/keychron/common/module/
git commit -m "fix(module): flash bytes verbatim; host now owns relocations

Firmware no longer mutates module data between load-time and
flash-program, so module_boot_scan CRC matches flash contents on
every cold boot and hooks re-install correctly. Resolves the
symptom where module prints disappeared after power cycle."
```

---

## Task 4: Drop `reloc_off`/`reloc_count`; shrink header 40→32; bump version to 1

**Files:**
- Modify: `keyboards/keychron/common/module/module_loader.h`
- Modify: `qmk/QMKata/ModuleBuild.py`
- Modify: `qmk/QMKata/module_linker.ld`
- Modify: `qmk/QMKata/test_module_build_integration.py`
- Modify: `qmk/QMKata/test_module_tab_hook_bitmap.py`
- Modify: `qmk/QMKata/test_module_relocations.py`

**Step 1: Firmware header**

```c
// module_loader.h
#define MODULE_HEADER_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t code_size;
    uint32_t hook_bitmap;
    uint32_t hook_table_off;
    uint32_t init_off;
    uint32_t deinit_off;
    uint32_t crc32;
} module_header_t;

_Static_assert(sizeof(module_header_t) == 32, "module_header_t must be 32 bytes");
```

**Step 2: Host constants and `_assemble`**

```python
# ModuleBuild.py
MODULE_HEADER_VERSION = 1
MODULE_HEADER_SIZE    = 32
MODULE_HOOK_TABLE_OFF = 32
```

In `_assemble`, update the `struct.pack` format to drop the two reloc fields. Header layout:

```python
# uint32_t magic;          // offset 0
# uint16_t version;        // offset 4
# uint16_t flags;          // offset 6
# uint32_t code_size;      // offset 8
# uint32_t hook_bitmap;    // offset 12
# uint32_t hook_table_off; // offset 16
# uint32_t init_off;       // offset 20
# uint32_t deinit_off;     // offset 24
# uint32_t crc32;          // offset 28 (zero; apply_relocations_and_crc writes)
header = struct.pack("<I H H I I I I I I",
    MODULE_HEADER_MAGIC,
    MODULE_HEADER_VERSION,
    0x0000,
    len(raw_bin),
    hook_bitmap,
    MODULE_HOOK_TABLE_OFF,
    init_off,
    deinit_off,
    0,                     # crc32 placeholder
)
assert len(header) == MODULE_HEADER_SIZE
```

Also: **remove the reloc-table append** (the block at `ModuleBuild.py:399-406`). Relocs are now returned in `result['relocs']` and consumed at load time; there is no reason to ship them in the binary.

Drop the 4-byte alignment assert above the deleted append (still worth keeping — `raw_bin` must be word-aligned before the header is overlaid, because the hook table immediately follows). Actually no, the alignment assert is about what comes *after* `raw_bin`; since we're not appending anything after now, drop it.

**Step 3: Linker script — shrink header from 40 to 32 bytes**

```ld
/* module_linker.ld */
.module_header : {
    /* Emit 32 real bytes so objcopy keeps header padding in the raw binary. */
    LONG(0) LONG(0) LONG(0) LONG(0)
    LONG(0) LONG(0) LONG(0) LONG(0)
} > MODULE
```

(8 `LONG(0)`s instead of 10.)

**Step 4: Update host tests**

- `test_module_build_integration.py`: any hardcoded 40 → 32. Any hardcoded 104 (hook-table end) remains 104 for this task; Task 5 flips it to 96.
- `test_module_tab_hook_bitmap.py`: the `MODULE_HEADER_SIZE`/`MODULE_HOOK_TABLE_OFF` imports already parametrise the tests, so once the constants change the tests should still pass. Any test that constructs a fake `raw_bin` by absolute size needs inspection.
- `test_module_relocations.py`: the test added in Task 1 uses `MODULE_HEADER_SIZE`, so it'll update automatically. The `test_null_module_has_two_ABS32_relocs` test (if it hardcodes offsets) may need tweaking.

**Step 5: Build and run all host tests**

```bash
cd /home/user/qmk/qmk-tools
python3 -m unittest discover qmk.QMKata -v
```

Expected: all pass. Fix any test that hardcodes 40 or 10-longs.

**Step 6: Build firmware**

```bash
cd /home/user/qmk/keychron_qmk_firmware
make keychron/q3_max/ansi_encoder:keychron
```

Expected: `_Static_assert` passes with 32. Clean build.

**Step 7: Hardware sanity — rebuild null module, load, power cycle, still works**

Same procedure as Task 3 Step 5-6. Confirms header-size change didn't regress the fix.

**Step 8: Commit both repos**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/
git commit -m "refactor(module): header v1; drop reloc fields; shrink to 32 bytes"
```

```bash
cd /home/user/qmk/keychron_qmk_firmware
git add keyboards/keychron/common/module/
git commit -m "refactor(module): header v1; drop reloc fields; shrink to 32 bytes"
```

---

## Task 5: Fix `.hook_table` linker-script padding (40-byte overhead)

**Files:**
- Modify: `qmk/QMKata/module_linker.ld`
- Modify: any host test pinning hook-table end offset (likely `test_module_build_integration.py`).

**Step 1: Drop the forced-end pad in linker script**

```ld
/* module_linker.ld */
.hook_table : ALIGN(4) {
    KEEP(*(.hook_table))
    . = ALIGN(4);
    /* Hook table is exactly MODULE_HOOK_MAX * 4 = 64 bytes; the
       previous `. = 40 + 64;` line was a workaround from the 40-byte
       header era that left 40 bytes of padding after shrinking. */
} > MODULE
```

**Step 2: Rebuild a sample module, inspect layout**

Build null_module with the updated linker script. Check `init_off` in the header — it should drop by 40. Previous value was 145 (with 40-byte header + 40-byte padding + init_fn at start of .text); new value ≈ 105 (32-byte header + 0 padding + init_fn at 32+64+4-alignment).

**Step 3: Update any test that pins `init_off`**

`test_module_build_integration.py` likely has a `self.assertEqual(init_off, 145)` or similar. Update to new value.

**Step 4: Run all host tests; rebuild firmware (unchanged); hardware sanity**

Same as Task 4 Step 5-7.

**Step 5: Commit**

```bash
cd /home/user/qmk/qmk-tools
git add qmk/QMKata/module_linker.ld qmk/QMKata/test_*.py
git commit -m "fix(module): drop .hook_table padding (saves 40 bytes per module)"
```

---

## Task 6: Hardware verification — full matrix

**Step 1: Null module cold-boot**

- Build null module in QMKata.
- Load to slot 0. See `init OK` in console.
- Power cycle. See `mod boot slot=0 init OK` within ~1s.

**Step 2: Hooks template cold-boot**

- Build hooks_template. Load to slot 1. See 7 combo hooks + init log.
- Power cycle. See boot-scan logs restore all 7 hooks (`mod boot slot=1 init OK`).
- Type a combo (keys that map to a live combo). Confirm hook-side prints fire (proves SRAM hook table is re-populated from flash).

**Step 3: Re-upload deinit path**

- Load null_module to slot 0 (replaces existing). See `mod load slot=0` deinit log for prior module (if template was there) followed by fresh init.

**Step 4: Sibling-sector erase**

- Load module to slot 0, then slot 1. Slots 0-3 share a sector, so loading to 1 erases 0 *and* 1 then re-flashes 1. Confirm module at slot 0 is gone and a subsequent load re-populates it.

**Step 5: Document results in the plan**

Append a "Verification results" section with dated evidence (console excerpts) for each step.

---

## Task 7: Final commit hygiene, no push

Check `git status` on both repos. Branches `feat/combo-modules` should contain, in order:
- host: `test(module): RED...`, `feat(module): host applies...`, `refactor(module): header v1...`, `fix(module): drop .hook_table padding`.
- firmware: `fix(module): flash bytes verbatim...`, `refactor(module): header v1...`.

Do not push. Wait for user sign-off.

---

## Failure modes and fallbacks

- **Task 3 hardware check fails** (prints still don't appear after power cycle): hypothesis was wrong. Stop. Add `xprintf` to the CRC-reject branch at `module_loader.c:481` to get evidence. Re-enter Phase 1 of systematic-debugging.
- **Task 4 breaks host tests in a non-trivial way**: a hardcoded 40 is hiding somewhere. Grep both repos for literal `40` near `header`/`hook_table`; most likely culprit is `test_module_tab_hook_bitmap.py` body-size math.
- **Task 5 init_off change breaks test assertions the subagent can't easily compute**: the exact new `init_off` is determined by the linker (depends on literal pool size). Build once, read the header, pin the observed value in the test with a comment explaining derivation.
- **Anything goes sideways on hardware**: firmware rolls back cleanly (`git revert` the last firmware commit); host rolls back cleanly. Both repos are still on `feat/combo-modules`, nothing pushed.

---

## Out of scope (carry forward to future plans)

- Save-and-restore siblings during sector erase in `module_load` (TODO at `module_loader.c:270`). Independent of this plan.
- Authenticity/signature check on modules (CRC is corruption detection only). Requires key-management design out of scope here.
- Runtime hook enable/disable. Header has a `flags` field reserved for it; no activation path designed.
- QMKata `save_combo` accepting empty Result as action-combo. User declined previously.
