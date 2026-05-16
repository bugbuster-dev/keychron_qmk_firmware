"""boot_smoke.py — Phase 2 scenario.

Verifies that the unmodified Q3 Max firmware boots cleanly under the
Renode emulator and reaches QMK's steady-state main loop. Asserts on
PC-hook log markers, not on UART output (CONSOLE_ENABLE is off in
the stock build).

Success criteria:
1. Firmware reaches `main()`.
2. Firmware reaches `keyboard_init`.
3. Firmware reaches `protocol_keyboard_task` at least 3 times
   (proves the main loop is iterating).

Run from emulator/scenarios/:
    python3 boot_smoke.py
"""

from __future__ import annotations

import sys
from pathlib import Path

# Make lib/ importable.
sys.path.insert(0, str(Path(__file__).parent / "lib"))

from layout import Layout  # noqa: E402
from renode_driver import make_hook, repo_root, run_renode  # noqa: E402


HOOKS = {
    0x0801E194: "HIT main",
    0x0801D7F0: "HIT keyboard_init",
    0x08013A14: "HIT wireless_init",
    0x0801E0F0: "HIT matrix_init",
    0x0801E190: "HIT protocol_keyboard_task",
    0x0801E124: "HIT matrix_scan",
}


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

    # Confirm the layout loads (proves Task 2.1 integration).
    layout = Layout.from_keyboard(
        root / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    h_coords = layout.coords("KC_H")
    print(f"  layout: KC_H is at row={h_coords[0]} col={h_coords[1]}")
    print(f"  layout: row_pins={layout.row_pins}")
    print(f"  layout: diode_direction={layout.diode_direction}")

    extra = [make_hook(addr, msg) for addr, msg in HOOKS.items()]

    print("  running Renode for 2s simulated time (~15s real)...")
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

    # Assertions.
    failures = []

    if not result.contains("HIT main"):
        failures.append("main() never reached")
    if not result.contains("HIT keyboard_init"):
        failures.append("keyboard_init never reached")
    if not result.contains("HIT matrix_init"):
        failures.append("matrix_init never reached")

    main_loop_iters = result.count("HIT protocol_keyboard_task")
    if main_loop_iters < 3:
        failures.append(
            f"protocol_keyboard_task fired only {main_loop_iters} times "
            "(need >= 3 to prove main loop is iterating)"
        )

    matrix_iters = result.count("HIT matrix_scan")
    if matrix_iters < 1:
        failures.append("matrix_scan never reached")

    # Report.
    print(f"  main() reached: {result.contains('HIT main')}")
    print(f"  keyboard_init reached: {result.contains('HIT keyboard_init')}")
    print(f"  matrix_init reached: {result.contains('HIT matrix_init')}")
    print(f"  wireless_init reached: {result.contains('HIT wireless_init')}")
    print(f"  matrix_scan iterations: {matrix_iters}")
    print(f"  protocol_keyboard_task iterations: {main_loop_iters}")

    if failures:
        print()
        print("FAIL:")
        for f in failures:
            print(f"  - {f}")
        return 1

    print()
    print("boot_smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
