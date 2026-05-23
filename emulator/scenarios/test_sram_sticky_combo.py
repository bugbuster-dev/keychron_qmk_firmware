#!/usr/bin/env python3
"""Comprehensive SRAM pipeline sticky_combo tests.

  1. Combo arm (J+K within window → K consumed)
  2. Combo timeout (>500ms virt → ~105ms QMK → exceeds 50ms window)
  3. Non-combo keys pass through (L)
  4. Non-combo keys pass through during active combo
  5. Unload / reload
  6. Tap action: ARMED_FOR_KEY1 (J held, K released, tap J → KC_UP 0x52)
  7. Tap action: ARMED_FOR_KEY2 (K held, J released, tap K → KC_DOWN 0x51)

Each test starts a fresh Renode instance, boots firmware, loads the
SRAM module post-boot, runs a gesture, then exits.

PREREQUISITE: firmware must be built with EMULATOR_BUILD flag so that
dbg_* UART tracing (REG/MAT output) is active. Use the emu keymap:
    qmk compile -kb keychron/q3_max/ansi_encoder -km emu
The stock keychron keymap (without EMULATOR_BUILD) will produce no
UART output and these tests will fail their gesture assertions.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
sys.path.insert(0, str(ROOT / "emulator/scenarios/lib"))


def nm_symbol(name):
    """Resolve firmware ELF symbol to int address."""
    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    out = subprocess.run(
        ["arm-none-eabi-nm", str(elf)], capture_output=True, text=True, check=True
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == name:
            return int(parts[0], 16)
    raise KeyError(f"symbol {name!r} not found")


def run_test(name, gesture_cmds, expect_regs=None, forbid_regs=None,
             count_reg=None, timeout_s=180.0):
    """Boot, load module, run gesture. Return number of failures.

    gesture_cmds is a list of strings:
      - "W<hex>"       → write matrix row-3 value (e.g. "W80" = 0x80)
      - "R<seconds>"   → RunFor (e.g. "R0.010")
    """
    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    resc = ROOT / "emulator/renode/q3_max.resc"
    mod_bin = ROOT / ".build/sticky_combo_module.bin"

    row3 = nm_symbol("matrix") + 12
    stage_addr = nm_symbol("g_emu_module_stage")
    stage_len_addr = nm_symbol("g_emu_module_stage_len")
    cmd_addr = nm_symbol("g_emu_module_cmd")
    mod_len = mod_bin.stat().st_size

    parts = [
        f"$bin = @{elf}",
        f"include @{resc}",
        'emulation RunFor "0:00:01.800"',  # boot
        f"sysbus LoadBinary @{mod_bin} 0x{stage_addr:08x}",
        f"sysbus WriteWord 0x{stage_len_addr:08x} 0x{mod_len:04x}",
        f"sysbus WriteByte 0x{cmd_addr:08x} 1",
        'emulation RunFor "0:00:00.500"',  # settle
    ]

    for cmd in gesture_cmds:
        if cmd[0] == "W":
            val = int(cmd[1:], 16)
            parts.append(f"sysbus WriteDoubleWord 0x{row3:08x} 0x{val:x}")
        elif cmd[0] == "R":
            secs = float(cmd[1:])
            parts.append(f'emulation RunFor "0:00:{secs:06.3f}"')
        else:
            raise ValueError(f"unknown gesture cmd: {cmd!r}")
    parts.append("quit")
    expr = "; ".join(parts)

    proc = subprocess.run(
        ["renode", "--disable-xwt", "--hide-monitor", "--plain", "-e", expr],
        capture_output=True, text=True, timeout=timeout_s, cwd=str(ROOT),
    )
    text = proc.stdout + "\n" + proc.stderr

    usart_clean = []
    for line in text.splitlines():
        if "usart2" in line.lower():
            usart_clean.append(line.split("] ", 1)[-1] if "] " in line else line)

    hids = [line.strip() for line in text.splitlines() if "HID-EP-WRITE" in line]

    print(f"  {name}: {len(usart_clean)} USART2 lines, {len(hids)} HID reports")

    init_ok = "mod load sram slot=8 init" in text
    if not init_ok:
        print("  WARNING: module not loaded — all gesture checks may be invalid")

    failures = 0
    if expect_regs:
        for label, hex_byte in expect_regs:
            needle = f"REG {hex_byte}"
            found = any(needle in line for line in usart_clean)
            status = "PASS" if found else "FAIL"
            if not found:
                failures += 1
            print(f"    {status}: {label}")

    if forbid_regs:
        for label, hex_byte in forbid_regs:
            needle = f"REG {hex_byte}"
            found = any(needle in line for line in usart_clean)
            status = "FAIL" if found else "PASS"
            if found:
                failures += 1
            print(f"    {status}: {label}")

    if count_reg:
        label, hex_byte, expected = count_reg
        got = sum(1 for line in usart_clean if f"REG {hex_byte}" in line)
        if got == expected:
            print(f"    PASS: {label} (got {got})")
        else:
            print(f"    FAIL: {label} (expected {expected}, got {got})")
            failures += 1

    return failures


def main():
    failures = 0

    # =====================================================================
    # Test 1: Combo ARM (J+K within effective window)
    #   QMK timer runs ~4.76x slower than Renode virt time.
    #   J+K within 10ms virt → ~2ms QMK → well within 50ms window.
    #   K should be CONSUMED (no REG 0e), proving the module intercepted it.
    # =====================================================================
    failures += run_test(
        "Combo arm (J+K → K consumed)",
        [
            "W80",      # J down
            "R0.010",   # ~2ms QMK → within window
            "W180",     # K down → combo arms, K consumed (no REG 0e)
            "R0.200",
            "W0",       # all up
            "R0.300",
        ],
        expect_regs=[("J fire normal → REG 0d", "0d")],
        forbid_regs=[("K consumed → NO REG 0e", "0e")],
    )

    # =====================================================================
    # Test 2: Combo window timeout
    #   500ms virt → ~105ms QMK → exceeds 50ms window.
    #   Both J and K should fire normally.
    # =====================================================================
    failures += run_test(
        "Timeout (500ms virt → ~105ms QMK → >50ms)",
        [
            "W80",      # J down
            "R0.500",
            "W180",     # K down (too late)
            "R0.200",
            "W0",       # all up
            "R0.300",
        ],
        expect_regs=[
            ("J fire normal → REG 0d", "0d"),
            ("K fire normal → REG 0e", "0e"),
        ],
        forbid_regs=[
            ("no KC_UP 0x52", "52"),
            ("no KC_DOWN 0x51", "51"),
        ],
    )

    # =====================================================================
    # Test 3: Non-combo key (L) passes through
    # =====================================================================
    failures += run_test(
        "L alone",
        [
            "W200",     # L down (col=9, bit=0x200)
            "R0.100",
            "W0",       # L up
            "R0.300",
        ],
        expect_regs=[("KC_L 0x0f", "0f")],
    )

    # =====================================================================
    # Test 4: Non-combo key passes through during active combo
    # =====================================================================
    failures += run_test(
        "L during active combo",
        [
            # Arm combo
            "W80",      # J down
            "R0.010",
            "W180",     # J+K
            "R0.100",
            "W100",     # release J, K held → ARMED_FOR_KEY2
            "R0.300",
            # L press while in ARMED_FOR_KEY2
            "W300",     # L+K (K=0x100, L=0x200 → 0x300)
            "R0.100",
            "W0",       # all up
            "R0.300",
        ],
        expect_regs=[("KC_L 0x0f during combo", "0f")],
    )

    # =====================================================================
    # Test 4c: Tap action — ARMED_FOR_KEY1 → KC_UP (0x52)
    #   J+K → arm, release K → ARMED_FOR_KEY1 (J held, K released).
    #   Tap J: release J (W0) then press J (W80) → 0x52 fires.
    # =====================================================================
    failures += run_test(
        "Tap: ARMED_FOR_KEY1 → KC_UP 0x52",
        [
            "W80",      # J down
            "R0.010",
            "W180",     # J+K → arm
            "R0.100",
            "W80",      # release K, J held → ARMED_FOR_KEY1
            "R0.300",
            # Tap J: up (W0) then press (W80) → tap_action_1=KC_UP
            "W0",       # J up
            "R0.020",
            "W80",      # J press → fires 0x52
            "R0.100",
            "W0",       # release
            "R0.300",
        ],
        expect_regs=[("KC_UP 0x52", "52")],
    )

    # =====================================================================
    # Test 4d: Tap action — ARMED_FOR_KEY2 → KC_DOWN (0x51)
    #   J+K → arm, release J → ARMED_FOR_KEY2 (K held, J released).
    #   Tap K: release K (W0) then press K (W100) → 0x51 fires.
    # =====================================================================
    failures += run_test(
        "Tap: ARMED_FOR_KEY2 → KC_DOWN 0x51",
        [
            "W80",      # J down
            "R0.010",
            "W180",     # J+K → arm
            "R0.100",
            "W100",     # release J, K held → ARMED_FOR_KEY2
            "R0.300",
            # Tap K: up (W0) then press (W100) → tap_action_2=KC_DOWN
            "W0",       # K up
            "R0.020",
            "W100",     # K press → fires 0x51
            "R0.100",
            "W0",       # release
            "R0.300",
        ],
        expect_regs=[("KC_DOWN 0x51", "51")],
    )

    # =====================================================================
    # Test 5: Unload + reload
    # =====================================================================
    print("\n  === Test 5: Unload / reload ===")
    elf = ROOT / ".build/keychron_q3_max_ansi_encoder_keychron.elf"
    resc = ROOT / "emulator/renode/q3_max.resc"
    mod_bin = ROOT / ".build/sticky_combo_module.bin"
    stage_addr = nm_symbol("g_emu_module_stage")
    stage_len_addr = nm_symbol("g_emu_module_stage_len")
    cmd_addr = nm_symbol("g_emu_module_cmd")
    mod_len = mod_bin.stat().st_size

    expr = "; ".join([
        f"$bin = @{elf}",
        f"include @{resc}",
        'emulation RunFor "0:00:01.800"',
        f"sysbus LoadBinary @{mod_bin} 0x{stage_addr:08x}",
        f"sysbus WriteWord 0x{stage_len_addr:08x} 0x{mod_len:04x}",
        f"sysbus WriteByte 0x{cmd_addr:08x} 1",  # load
        'emulation RunFor "0:00:00.500"',
        f"sysbus WriteByte 0x{cmd_addr:08x} 2",  # unload
        'emulation RunFor "0:00:00.300"',
        f"sysbus WriteByte 0x{cmd_addr:08x} 0",  # clear cmd
        'emulation RunFor "0:00:00.100"',
        f"sysbus WriteByte 0x{cmd_addr:08x} 1",  # reload
        'emulation RunFor "0:00:00.500"',
        "quit",
    ])
    proc = subprocess.run(
        ["renode", "--disable-xwt", "--hide-monitor", "--plain", "-e", expr],
        capture_output=True, text=True, timeout=180, cwd=str(ROOT),
    )
    text = proc.stdout + "\n" + proc.stderr
    init_oks = text.count("init OK rc=0x600dbeef")
    unload_oks = text.count("emu: unload")

    if init_oks >= 2:
        print(f"    PASS: reload OK ({init_oks} init OKs)")
    else:
        print(f"    FAIL: expected >=2 init OKs, got {init_oks}")
        failures += 1
    if unload_oks >= 1:
        print(f"    PASS: unload ({unload_oks} unload(s))")
    else:
        print(f"    FAIL: no unload message in output")
        failures += 1

    # =====================================================================
    print()
    if failures:
        print(f"{failures} FAILURES")
    else:
        print("ALL TESTS PASSED")
    return failures


if __name__ == "__main__":
    sys.exit(min(main(), 127))
