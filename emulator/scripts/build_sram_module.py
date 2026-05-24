#!/usr/bin/env python3
"""Build the sticky_combo SRAM behavior module and stage it for Renode.

Workflow:
  1. Resolve the g_module_sram address from the firmware ELF (built by
     `qmk compile -kb keychron/q3_max/ansi_encoder -km keychron`).
  2. Invoke qmk-tools ModuleBuild on
     qmk-tools/qmk/QMKata/module_examples/kbsm_sticky_combo/sticky_combo_module.c.
  3. Apply R_ARM_ABS32 relocations against the slot base, recompute CRC.
  4. Write the result to .build/kbsm_sticky_combo.bin.
  5. Write .build/kbsm_sticky_combo.json with the slot_addr it was
     relocated against (the scenario uses this to detect staleness).

Re-run after either the firmware OR the module source has changed.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
QMK_TOOLS = ROOT.parent / "qmk-tools" / "qmk" / "QMKata"

sys.path.insert(0, str(ROOT / "emulator/scenarios/lib"))
sys.path.insert(0, str(QMK_TOOLS))
sys.path.insert(0, str(QMK_TOOLS.parent))  # for `from keyboards.KeychronQ3Max import ...`

from GccToolchain import GccToolchain  # noqa: E402
from ModuleBuild import ModuleBuild  # noqa: E402
from keyboards.KeychronQ3Max import KeychronQ3Max  # noqa: E402


def resolve_g_module_sram(elf_path):
    """Return the absolute address of g_module_sram from the ELF."""
    out = subprocess.run(
        ["arm-none-eabi-nm", str(elf_path)],
        capture_output=True, text=True, check=True,
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == "g_module_sram":
            return int(parts[0], 16)
    raise RuntimeError(f"g_module_sram not found in {elf_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--source",
        default=str(QMK_TOOLS / "module_examples/kbsm_sticky_combo/sticky_combo_module.c"),
        help="Module C source",
    )
    ap.add_argument(
        "--output",
        default=str(ROOT / ".build/kbsm_sticky_combo.bin"),
        help="Output binary path",
    )
    args = ap.parse_args()

    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    if not elf.exists():
        print(f"E: firmware ELF not found at {elf}")
        print("   Run: qmk compile -kb keychron/q3_max/ansi_encoder -km keychron")
        return 2

    slot_addr = resolve_g_module_sram(elf)
    print(f"  firmware ELF: {elf}")
    print(f"  g_module_sram resolved: 0x{slot_addr:08x}")

    # ModuleBuild only compiles a single .c. The kbsm_sticky_combo
    # example has two source files (StickyCombo.c + sticky_combo_module.c)
    # plus two headers. We concatenate the C sources into a single
    # translation unit and copy the headers to .build/ so the
    # `#include "StickyCombo.h"` etc. resolve. Then point ModuleBuild at
    # the combined file. The example source dir is added to include
    # paths via the toolchain config.
    example_dir = QMK_TOOLS / "module_examples/kbsm_sticky_combo"
    combined = ROOT / ".build/kbsm_sticky_combo_combined.c"
    combined.parent.mkdir(parents=True, exist_ok=True)
    combined.write_bytes(
        (example_dir / "StickyCombo.c").read_bytes()
        + b"\n"
        + (example_dir / "sticky_combo_module.c").read_bytes()
    )
    # Copy headers next to the combined source so the #include "..."
    # statements in the original sources resolve.
    for hdr in ["StickyCombo.h", "combos_def.h"]:
        (ROOT / ".build" / hdr).write_bytes((example_dir / hdr).read_bytes())
    args.source = str(combined)
    print(f"  source: {args.source} (concatenated)")

    # Build module.
    toolchain = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(ROOT))
    builder = ModuleBuild(toolchain, firmware_path=str(ROOT))

    # The mapfile parser only catches symbols emitted as ".text.NAME"
    # in the GCC linker map (i.e. compiled with -ffunction-sections).
    # libc/libgcc routines like memset come from prebuilt static libs
    # that don't use per-function sections, so they land as plain ".text"
    # entries and the parser misses them. Pre-seed the mapfile's function
    # table with libc symbols we know the module needs, resolved via nm.
    libc_symbols = ["memset", "memcpy", "memcmp", "strcmp", "strlen"]
    nm_out = subprocess.run(
        ["arm-none-eabi-nm", str(elf)], capture_output=True, text=True
    ).stdout
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] in libc_symbols:
            sym = parts[-1]
            addr = int(parts[0], 16)
            if builder.mapfile and sym not in builder.mapfile.functions:
                builder.mapfile.functions[sym] = {
                    "address": addr, "size": 0,
                    "object_file": "(libc, resolved via nm)",
                    "symbol_name": sym,
                }
                print(f"  pre-seeded libc symbol: {sym} -> 0x{addr:08x}")

    # SRAM-modules-only override: place .data and .bss into the MODULE
    # region instead of discarding them, so writable globals are kept.
    # The kbsm_sticky_combo example (and any behavior module that
    # holds state across calls) needs this. Flash modules can't tolerate
    # writable globals, but we're targeting SRAM where the slot is
    # writable RAM.
    custom_ld = ROOT / ".build/module_linker_sram.ld"
    custom_ld.parent.mkdir(parents=True, exist_ok=True)
    with open(builder.linker_script) as f:
        orig_ld = f.read()
    new_ld = orig_ld.replace(
        "/DISCARD/ : {\n        *(.data*)\n        *(.bss*)\n",
        "    .data : ALIGN(4) {\n"
        "        *(.data*)\n"
        "        . = ALIGN(4);\n"
        "    } > MODULE\n"
        "    .bss : ALIGN(4) {\n"
        "        *(.bss*)\n"
        "        . = ALIGN(4);\n"
        "    } > MODULE\n"
        "    /DISCARD/ : {\n",
    )
    if new_ld == orig_ld:
        print("E: failed to patch linker script — DISCARD pattern not found")
        return 1
    custom_ld.write_text(new_ld)
    builder.linker_script = str(custom_ld)
    print(f"  custom linker script: {custom_ld}")

    # Silence the writable-section validator (we just made writable sections legal).
    builder._validate_no_writable_sections = lambda obj_file: True

    result = builder.build(args.source)
    if result is None:
        print(f"E: module build failed: {builder.last_error}")
        return 1

    print(f"  hooks: {result['hooks']}")
    print(f"  hook_bitmap: 0x{result['hook_bitmap']:x}")
    print(f"  binary size (pre-reloc): {result['size']} bytes")
    print(f"  relocs: {len(result['relocs'])} ABS32 entries")

    # Apply relocations.
    final_bin = builder.apply_relocations_and_crc(
        result["binary"], result["relocs"], slot_addr
    )
    print(f"  binary size (post-reloc): {len(final_bin)} bytes")

    if len(final_bin) > 0x1000:
        print(f"E: module too large for slot ({len(final_bin)} > 4096)")
        return 1

    # Write output.
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(final_bin)
    print(f"  → wrote {output_path} ({len(final_bin)} bytes)")

    # Write sidecar JSON for staleness detection.
    sidecar = output_path.with_suffix(".json")
    sidecar.write_text(
        json.dumps(
            {
                "slot_addr": f"0x{slot_addr:08x}",
                "module_len": len(final_bin),
                "module_source": str(Path(args.source).resolve()),
                "firmware_elf": str(elf.resolve()),
                "hook_bitmap": result["hook_bitmap"],
                "hooks": list(result["hooks"]),
            },
            indent=2,
        )
    )
    print(f"  → wrote {sidecar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
