#!/usr/bin/env python3
"""sram_sticky_combo.py — interactive Renode debug of the SRAM pipeline module.

Workflow:
  1. Optionally auto-build firmware and module if missing.
  2. Cross-check that the staged module bin was relocated against the
     current firmware's g_module_sram address (sidecar JSON).
  3. Launch Renode interactively with the module staged at slot 8.
  4. Print a usage banner with the exact `sysbus WriteWord` gestures
     for the J+K combo and the runtime load/unload commands.

The scenario does NOT call `quit` — it drops the user into Renode's
monitor so they can drive the combo gesture by hand and observe
USART2 output (`MAT ...`, `REG ...`, `emu: load/unload ...`) in the
analyzer window.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
sys.path.insert(0, str(ROOT / "emulator/scenarios/lib"))

from symbol_resolver import resolve_symbols  # noqa: E402
from layout import Layout  # noqa: E402


def resolve_symbol(elf, name):
    out = subprocess.run(
        ["arm-none-eabi-nm", str(elf)], capture_output=True, text=True, check=True
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == name:
            return int(parts[0], 16)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true",
                    help="Skip rebuilding firmware/module if missing or stale")
    ap.add_argument("--no-load", action="store_true",
                    help="Boot firmware without staging the module")
    args = ap.parse_args()

    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    mod_bin = ROOT / ".build/sticky_combo_module.bin"
    mod_json = ROOT / ".build/sticky_combo_module.json"
    resc = ROOT / "emulator/renode/q3_max.resc"

    # ------------------------------------------------------------------
    # Auto-build firmware if missing.
    if not elf.exists():
        if args.no_build:
            print(f"E: firmware ELF missing at {elf} and --no-build set")
            return 2
        print("Building firmware...")
        r = subprocess.run(
            ["qmk", "compile", "-kb", "keychron/q3_max/ansi_encoder", "-km", "emu"],
            cwd=str(ROOT),
        )
        if r.returncode != 0:
            print("E: firmware build failed")
            return r.returncode
        # Sync addresses after rebuild.
        subprocess.run(
            [sys.executable, str(ROOT / "emulator/scripts/sync_addrs.py")],
            cwd=str(ROOT),
            check=True,
        )

    # ------------------------------------------------------------------
    # Auto-build module if missing or stale.
    g_module_sram = resolve_symbol(elf, "g_module_sram")
    if g_module_sram is None:
        print("E: g_module_sram not found in firmware ELF")
        return 1

    needs_build = not args.no_load
    if needs_build and (not mod_bin.exists() or not mod_json.exists()):
        needs_build = True
    elif needs_build:
        meta = json.loads(mod_json.read_text())
        baked = int(meta["slot_addr"], 16)
        if baked != g_module_sram:
            print(f"  module bin was relocated against 0x{baked:08x} but firmware "
                  f"places g_module_sram at 0x{g_module_sram:08x} — rebuilding")
            needs_build = True
        elif mod_bin.stat().st_mtime < elf.stat().st_mtime:
            print("  module bin older than firmware — rebuilding")
            needs_build = True
        else:
            needs_build = False

    if needs_build and not args.no_load:
        if args.no_build:
            print("E: module bin stale/missing and --no-build set")
            return 2
        print("Building SRAM module...")
        r = subprocess.run(
            [sys.executable, str(ROOT / "emulator/scripts/build_sram_module.py")],
            cwd=str(ROOT),
        )
        if r.returncode != 0:
            return r.returncode

    # ------------------------------------------------------------------
    # Resolve runtime addresses.
    syms = resolve_symbols(str(elf))
    matrix_addr = resolve_symbol(elf, "matrix")  # cooked matrix base
    g_emu_module_cmd = resolve_symbol(elf, "g_emu_module_cmd")
    if matrix_addr is None or g_emu_module_cmd is None:
        print("E: required symbols (matrix, g_emu_module_cmd) missing")
        return 1
    row3_addr = matrix_addr + 12  # row 3 in 6×32-bit matrix

    layout = Layout.from_keyboard(
        ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    j_row, j_col = layout.coords("KC_J")
    k_row, k_col = layout.coords("KC_K")
    assert j_row == 3 and k_row == 3, f"unexpected J/K rows {j_row} {k_row}"
    j_bit = 1 << j_col
    k_bit = 1 << k_col

    # ------------------------------------------------------------------
    # Build the Renode -e expression.
    #
    # Note: we CANNOT LoadBinary into g_module_sram before `start`,
    # because ChibiOS clears .bss during early boot and would erase our
    # staged bytes. Instead we let firmware boot fully (RunFor 1.5s),
    # then pause, LoadBinary, write the runtime cmd byte to trigger
    # module_load via the matrix_scan poll trampoline, then resume.
    cmds = [
        f"$bin = @{elf}",
        f"include @{resc}",
        "start",
    ]
    if not args.no_load:
        # Let boot complete & .bss clear, then stage and trigger load.
        cmds += [
            'emulation RunFor "0:00:01.500"',
            "pause",
            f"sysbus LoadBinary @{mod_bin} 0x{g_module_sram:08x}",
            # Trigger via the polled command byte. Edge-detected so we
            # don't need to clear it first (it starts at 0).
            f"sysbus WriteByte 0x{g_emu_module_cmd:08x} 1",
            "start",
        ]
    expr = "; ".join(cmds)

    # ------------------------------------------------------------------
    # Print banner.
    banner = f"""
================================================================
Sticky combo SRAM pipeline module — Renode interactive debug
================================================================
  Firmware ELF : {elf}
  Module bin   : {mod_bin}{' (NOT loaded: --no-load)' if args.no_load else ''}
  Module size  : {mod_bin.stat().st_size if mod_bin.exists() else '?'} bytes
  g_module_sram: 0x{g_module_sram:08x}
  matrix[3] @ 0x{row3_addr:08x}
  KC_J = row=3 col={j_col} → bit 0x{j_bit:03x}
  KC_K = row=3 col={k_col} → bit 0x{k_bit:03x}
  g_emu_module_cmd @ 0x{g_emu_module_cmd:08x}

Watch the USART2 analyzer window for:
  - 'emu: staged module detected at slot 8, auto-load OK'  (boot)
  - 'mod load sram slot=8 init OK rc=0x600dbeef'           (loader)
  - 'MAT 00 00 XX XX'                                       (matrix delta)
  - 'REG xx'                                                (register_code)

Combo gesture (J+K arms; J-held + tap K = KC_DOWN; K-held + tap J = KC_UP):

  Arm the combo:
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{j_bit:x}             # J down
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{j_bit | k_bit:x}     # J+K within 50ms (ARMS combo)
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{k_bit:x}             # release J, K held

  Hold K, tap J → KC_UP (0x52):
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{j_bit | k_bit:x}     # J down + K still held
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{k_bit:x}             # J up, K held
        → expect 'REG 52' on USART2

  Hold J, tap K → KC_DOWN (0x51) (mirror gesture):
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x{j_bit:x}             # release K to switch hand
    (... re-arm via J+K, then K-held + tap J pattern)

  Reset:
    sysbus WriteDoubleWord 0x{row3_addr:08x} 0x0                     # all keys up

Runtime module control:
  sysbus WriteByte 0x{g_emu_module_cmd:08x} 0     # clear (then re-issue)
  sysbus WriteByte 0x{g_emu_module_cmd:08x} 1     # load slot 8
  sysbus WriteByte 0x{g_emu_module_cmd:08x} 2     # unload slot 8

  Restage bytes (e.g. after rebuilding the module):
    sysbus LoadBinary @{mod_bin} 0x{g_module_sram:08x}
    sysbus WriteByte 0x{g_emu_module_cmd:08x} 0
    sysbus WriteByte 0x{g_emu_module_cmd:08x} 1

Renode controls:
  pause / start          # halt and resume CPU
  cpu PC                  # show current PC
  cpu Registers           # full register dump
  quit                    # exit Renode

================================================================
Launching Renode...
"""
    print(banner)
    sys.stdout.flush()

    # ------------------------------------------------------------------
    # Spawn Renode interactively. No --disable-xwt so the analyzer window
    # opens; the user closes Renode by typing `quit` or Ctrl-C.
    cmd = ["renode", "-e", expr]
    os.execvp(cmd[0], cmd)


if __name__ == "__main__":
    sys.exit(main())
