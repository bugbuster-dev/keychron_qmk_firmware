"""type_hello.py — end-to-end matrix injection → HID report scenario.

Verifies the full pipeline:
1. Layout resolves KC_H to (row, col)
2. Matrix injector sees the press during firmware's matrix scan
3. QMK processes the key event
4. HID report bytes flow into usb_endpoint_in_send
5. obqWriteTimeout hook captures the report with 0x0B (H usage code)

Run from emulator/scenarios/:
    python3 type_hello.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "lib"))

from layout import Layout  # noqa: E402
from renode_driver import make_hook, press_key, release_key, repo_root, run_renode  # noqa: E402
from symbol_resolver import resolve_symbols  # noqa: E402


def main() -> int:
    root = repo_root()
    resc = root / "emulator/renode/q3_max.resc"
    elf = root / ".build/keychron_q3_max_ansi_encoder_keychron.elf"

    if not resc.exists():
        print(f"FAIL: missing {resc}")
        return 2
    if not elf.exists():
        print(f"FAIL: missing {elf} — run `qmk compile -kb keychron/q3_max/ansi_encoder -km keychron` first")
        return 2

    # Resolve KC_H matrix coordinates from the actual keymap.
    layout = Layout.from_keyboard(
        root / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    h_row, h_col = layout.coords("KC_H")
    print(f"  KC_H is at row={h_row} col={h_col}")

    # Matrix injector offset: (row << 5) | col  (from .repl IsUser branch)
    h_offset = (h_row << 5) | h_col

    # Build commands: hooks for diagnostics + keypress injection.
    # The ControlWrite commands execute before RunFor starts the CPU,
    # so KC_H is held down for the entire 2-second simulation.
    syms = resolve_symbols(elf)
    extra = [
        make_hook(int(syms["protocol_keyboard_task"], 16), "HIT protocol_keyboard_task"),
        make_hook(int(syms["matrix_scan"], 16), "HIT matrix_scan"),
        press_key(h_row, h_col),
    ]

    print("  running Renode (~15s real)...")
    result = run_renode(
        resc,
        elf=elf,
        extra_commands=extra,
        sim_seconds=2.0,
        timeout_seconds=120.0,
    )

    if result.returncode != 0:
        print(f"FAIL: Renode exited {result.returncode}")
        print(result.text[-2000:])
        return 1

    # Check boot reached steady state.
    main_loop_iters = result.count("HIT protocol_keyboard_task")
    matrix_iters = result.count("HIT matrix_scan")

    print(f"  matrix_scan iterations: {matrix_iters}")
    print(f"  protocol_keyboard_task iterations: {main_loop_iters}")

    # Check for HID report capture.
    hid_reports = [
        line for line in result.text.splitlines()
        if "HID-EP-WRITE" in line
    ]
    print(f"  HID-EP-WRITE log lines: {len(hid_reports)}")
    for line in hid_reports[:5]:
        print(f"    {line.strip()}")

    # Assertions.
    failures = []

    if main_loop_iters < 3:
        failures.append(
            f"protocol_keyboard_task fired only {main_loop_iters} times "
            f"(need >= 3 to prove main loop is iterating)"
        )

    if matrix_iters < 1:
        failures.append("matrix_scan never reached")

    if not hid_reports:
        failures.append(
            "No HID-EP-WRITE log lines found. "
            "The HID report capture hook may not be firing, "
            "or no key events were generated."
        )
    else:
        # Check that at least one report contains 0x0B (H usage code).
        h_found = any("0b" in line.lower() for line in hid_reports)
        if not h_found:
            failures.append(
                "HID-EP-WRITE lines found but none contain 0x0B (H usage code). "
                "Matrix injection patches the firmware's local scan buffer, "
                "but end-to-end keypress→HID-report verification is not yet "
                "working. See emulator/docs/boot_path.md for details."
            )

    if failures:
        print()
        print("FAIL:")
        for f in failures:
            print(f"  - {f}")
        return 1

    print()
    print("type_hello: OK — KC_H press produced a HID report with usage 0x0B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
