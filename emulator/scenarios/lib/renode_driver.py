"""Minimal Renode driver wrapper for scenarios.

Spawns Renode as a subprocess in headless mode and feeds it commands
through `-e`. For v1.0 this is sufficient — scenarios are linear:
load script, run N seconds with hooks installed, capture stdout,
assert on it. Interactive control (matrix injection mid-run from
outside) would need a TCP monitor connection; we defer that to v2.

Renode prints log lines to stderr (most lines) and stdout (monitor
command echo). We capture both. The driver returns the merged text
to the caller, which uses regex/substring checks for assertions.
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
    resc_script: Path | str,
    *,
    elf: Path | str | None = None,
    extra_commands: list[str] | None = None,
    sim_seconds: float = 2.0,
    timeout_seconds: float = 120.0,
    renode_binary: str = "renode",
) -> RenodeRun:
    """Run Renode headlessly, execute the .resc, advance simulated time, quit.

    `extra_commands` are run AFTER `include @resc_script` and BEFORE
    `start`. Useful for adding CPU hooks for diagnostic logging.

    `elf` overrides the default ELF path baked into the .resc.

    `sim_seconds` is in *simulated* time (Renode `emulation RunFor`).
    Real wall-clock time will typically be 1-3× longer depending on
    workload. Renode's `sleep` command does NOT exist — using it
    silently no-ops, which is a common pitfall.
    """
    cmds = []
    if elf is not None:
        cmds.append(f"$bin = @{elf}")
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
    cmds.append("quit")

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


def repo_root() -> Path:
    """Return the QMK repo root (three levels up from this file)."""
    return Path(__file__).resolve().parents[3]
