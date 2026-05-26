#!/usr/bin/env python3
"""Build a dynld animation via ModuleBuild and output raw code for SysEx.

Uses the ModuleBuild pipeline (compile + link + resolve symbols) but
with a dynld-specific linker script that omits the kbsm header and
hook table. The output is a raw binary ready for SysEx upload.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
QMK_TOOLS = ROOT.parent / "qmk-tools" / "qmk" / "QMKata"

sys.path.insert(0, str(ROOT / "emulator/scenarios/lib"))
sys.path.insert(0, str(QMK_TOOLS))
sys.path.insert(0, str(QMK_TOOLS.parent))

from GccToolchain import GccToolchain
from ModuleBuild import ModuleBuild
from keyboards.KeychronQ3Max import KeychronQ3Max


DYNLD_ANIMATIONS = {
    "game_of_life": {
        "source": "dynld_animation_examples/kb_dynld_game_of_life.c",
        "output": "dynld_game_of_life",
    },
    "spiral": {
        "source": "dynld_animation_examples/kb_spiral_animation.c",
        "output": "dynld_spiral",
    },
}

DYNLD_LINKER = QMK_TOOLS / "dynld_linker.ld"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("animation", choices=list(DYNLD_ANIMATIONS.keys()),
                    help="Animation to build")
    ap.add_argument("--source", default=None, help="Override C source path")
    ap.add_argument("--output", default=None, help="Override output path")
    args = ap.parse_args()

    cfg = DYNLD_ANIMATIONS[args.animation]
    source = args.source or str(QMK_TOOLS / cfg["source"])
    output = args.output or str(ROOT / f".build/{cfg['output']}.bin")

    if not Path(source).exists():
        print(f"E: source not found: {source}")
        return 2

    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    if not elf.exists():
        print(f"E: firmware ELF not found at {elf}")
        return 2

    print(f"  animation: {args.animation}")
    print(f"  source: {source}")

    toolchain = GccToolchain(KeychronQ3Max.TOOLCHAIN, firmware_path=str(ROOT))
    # Dynld needs the keyboard's TOOLCHAIN includes plus .build/ for generated headers
    extra = list(KeychronQ3Max.TOOLCHAIN.get("includes", []))
    extra.append(str(ROOT / ".build/obj_keychron_q3_max_ansi_encoder_keychron/src"))
    builder = ModuleBuild(toolchain, firmware_path=str(ROOT),
                          extra_includes=[str(ROOT / d) if not d.startswith(str(ROOT)) else d
                                         for d in extra])

    # Use dynld linker script (no header, no hook table)
    builder.linker_script = str(DYNLD_LINKER)

    # Pre-seed libc symbols
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
                    "object_file": "(libc, nm)",
                    "symbol_name": sym,
                }

    # Build via ModuleBuild pipeline (compile + link + resolve)
    # Use build_dynld() — skips _assemble() which writes kbsm header.
    # dynld_linker.ld puts .text at offset 0, no header corruption.
    raw = builder.build_dynld(source)
    if raw is None:
        print(f"E: build failed: {builder.last_error}")
        return 1

    # Write just the code portion
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_bytes(raw)

    print(f"  code size: {len(raw)} bytes")
    if len(raw) > 1024:
        print(f"  WARNING: exceeds DYNLD_FUNC_SIZE (1024 bytes)")
    print(f"  → wrote {output} ({len(raw)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
