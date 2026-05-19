"""Minimal Renode driver wrapper for scenarios.

Spawns Renode as a subprocess in headless mode and feeds it commands
through `-e`. For v1.0 this is sufficient — scenarios are linear:
load script, run N seconds with hooks installed, capture stdout,
assert on it. Interactive control (matrix injection mid-run from
outside) would need a TCP monitor connection; we defer that to v2.

Renode prints log lines to stderr (most lines) and stdout (monitor
command echo). We capture both. The driver returns the merged text
to the caller, which uses regex/substring checks for assertions.

Symbol resolution: when an ELF path is provided, the driver queries
arm-none-eabi-nm for known firmware symbols and injects them as
Renode variables ($symbolName = 0xaddr) before the .resc script
runs. This eliminates hard-coded addresses in .resc files.
"""

from __future__ import annotations

import os
import subprocess
import textwrap
from dataclasses import dataclass
from pathlib import Path


@dataclass
class RenodeRun:
    """Result of one Renode invocation."""

    stdout: str
    stderr: str
    returncode: int

    @property
    def text(self) -> str:
        return self.stdout + "\n" + self.stderr

    def contains(self, needle: str) -> bool:
        return needle in self.text

    def count(self, needle: str) -> int:
        return self.text.count(needle)


def run_renode(
    resc_script,
    *,
    elf=None,
    extra_commands=None,
    post_commands=None,
    sim_seconds=2.0,
    timeout_seconds=120.0,
    renode_binary="renode",
    resolve_symbols=True,
):
    """Run Renode headlessly, execute the .resc, advance simulated time, quit.

    `extra_commands` are run AFTER `include @resc_script` and BEFORE
    `emulation RunFor`. Useful for adding CPU hooks for diagnostic logging.

    `post_commands` are run AFTER `emulation RunFor` and BEFORE `quit`.
    Useful for reading SRAM buffers that firmware populated during the run.

    `elf` overrides the default ELF path baked into the .resc.

    `sim_seconds` is in *simulated* time (Renode `emulation RunFor`).
    Real wall-clock time will typically be 1-3× longer depending on
    workload. Renode's `sleep` command does NOT exist — using it
    silently no-ops, which is a common pitfall.

    `resolve_symbols` (default True): query arm-none-eabi-nm on the ELF
    for known firmware symbols and verify they match the hard-coded
    addresses in q3_max.resc. Raises RuntimeError on mismatch so
    stale addresses are caught immediately. Set False to skip.
    """
    cmds = []
    if elf is not None:
        cmds.append(f"$bin = @{elf}")

        # Verify firmware symbols match the hard-coded addresses in q3_max.resc.
        if resolve_symbols:
            from symbol_resolver import EXPECTED_ADDRESSES, verify_symbols  # noqa: N812
            mismatches = verify_symbols(elf, EXPECTED_ADDRESSES)
            if mismatches:
                raise RuntimeError(
                    "Firmware symbol addresses have drifted! "
                    "Update q3_max.resc and symbol_resolver.py.\n"
                    + "\n".join(f"  {m}" for m in mismatches)
                )
    cmds.append(f"include @{resc_script}")
    if extra_commands:
        cmds.extend(extra_commands)
    # `emulation RunFor` advances simulated time AND drives the CPU.
    # Do NOT call `start` first — RunFor errors if emulation is already
    # running. (The Renode `sleep` command does not exist; that pitfall
    # silently no-ops and was the source of much confusion.)
    h = int(sim_seconds // 3600)
    m = int((sim_seconds % 3600) // 60)
    s = sim_seconds - (h * 3600 + m * 60)
    cmds.append(f'emulation RunFor "{h}:{m:02d}:{s:06.3f}"')
    if post_commands:
        cmds.extend(post_commands)
    cmds.append("quit")

    # Join commands with semicolons, but preserve triple-quoted blocks.
    # Commands that contain """...""" are Renode multi-line scripts and
    # must not be split or have their newlines stripped.
    expression = "; ".join(cmds)

    # Write Renode's massive log output to temp files rather than
    # subprocess pipes. With many CPU hooks installed, Renode can
    # generate thousands of log lines per simulated second; pipe
    # buffers fill and the child blocks. Files don't.
    import tempfile
    with tempfile.NamedTemporaryFile(
        mode="w+", suffix=".stdout", delete=False
    ) as f_out, tempfile.NamedTemporaryFile(
        mode="w+", suffix=".stderr", delete=False
    ) as f_err:
        out_path = f_out.name
        err_path = f_err.name

    try:
        with open(out_path, "w") as fo, open(err_path, "w") as fe:
            proc = subprocess.run(
                [renode_binary,
                 "--disable-xwt",
                 "--hide-monitor",
                 "--plain",
                 "-e", expression],
                stdin=subprocess.DEVNULL,
                stdout=fo,
                stderr=fe,
                timeout=timeout_seconds,
                env={**os.environ},
                cwd=str(repo_root()),
            )
        with open(out_path) as fo, open(err_path) as fe:
            out_text = fo.read()
            err_text = fe.read()
    finally:
        Path(out_path).unlink(missing_ok=True)
        Path(err_path).unlink(missing_ok=True)

    return RenodeRun(
        stdout=out_text,
        stderr=err_text,
        returncode=proc.returncode,
    )


def make_hook(addr: int, message: str) -> str:
    """Build a Renode monitor command that logs `message` when PC hits `addr`."""
    # Escape message safely.
    safe = message.replace("'", "\\'")
    return f"sysbus.cpu AddHook 0x{addr:08x} \"self.Log(LogLevel.Warning, '{safe}')\""


def press_key(row: int, col: int) -> str:
    """Build a Renode monitor command that presses a matrix key.

    Registers a CPU hook after 'bl debounce' in matrix_scan where the
    cooked matrix is finalized. The hook ORs the pressed-key
    bit into the cooked matrix AFTER debounce, so it won't be overwritten.

    NOTE: The hook fires on every matrix scan, keeping the key
    permanently pressed. Hook overhead slows the simulation ~5x.
    """
    bit = 1 << col
    row_offset = row * 4
    cooked = 0x200099b8
    # Hook right after 'bl debounce' returns (0x0801e2a6)
    script = (
        f"sb = self.GetMachine()['sysbus']; "
        f"sb.WriteDoubleWord(0x{cooked:x} + {row_offset}, sb.ReadDoubleWord(0x{cooked:x} + {row_offset}) | {bit})"
    )
    return f'cpu AddHook 0x0801e2a6 """{script}"""'


def release_key(row: int, col: int) -> str:
    """Build a Renode monitor command that releases a matrix key.

    Clears the key bit in the cooked matrix after debounce.
    """
    mask = ~(1 << col) & 0xFFFFFFFF
    row_offset = row * 4
    cooked = 0x200099b8
    script = (
        f"sb = self.GetMachine()['sysbus']; "
        f"sb.WriteDoubleWord(0x{cooked:x} + {row_offset}, sb.ReadDoubleWord(0x{cooked:x} + {row_offset}) & {mask})"
    )
    return f'cpu AddHook 0x0801e2a6 """{script}"""'


def repo_root() -> Path:
    """Return the QMK repo root (three levels up from this file)."""
    return Path(__file__).resolve().parents[3]
