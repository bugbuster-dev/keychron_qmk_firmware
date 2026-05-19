"""Resolve firmware symbol addresses from the ELF at runtime.

Eliminates hard-coded addresses in .resc scripts by querying
arm-none-eabi-nm on the built ELF and injecting them as Renode
variables via -e expressions.

Usage:
    # In renode_driver.py or a scenario:
    from symbol_resolver import resolve_symbols, make_renode_vars

    syms = resolve_symbols(elf_path)
    # syms = {"usb_endpoint_in_send": "0x08024856", ...}

    # Emit Renode -e expressions to set $variables:
    for cmd in make_renode_vars(syms):
        # cmd = "$usb_endpoint_in_send = 0x08024bb6"
        pass
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


# Symbols we need from the firmware ELF.
# Map: (nm search pattern, variable name for .resc)
KNOWN_SYMBOLS = [
    # Boot patches (used in q3_max.resc reset macro)
    ("chSysPolledDelayX", "chSysPolledDelayX"),
    ("spiSend", "spiSend"),
    ("spiExchange", "spiExchange"),
    ("rtc_enter_init", "rtc_enter_init"),
    # USB / HID hooks (used in q3_max.resc cpu AddHook)
    ("usb_endpoint_in_send", "usb_endpoint_in_send"),
    ("obqWriteTimeout", "obqWriteTimeout"),
    ("USBD1", "USBD1"),
    # Boot diagnostic hooks (used in boot_smoke.py)
    ("main", "main"),
    ("keyboard_init", "keyboard_init"),
    ("wireless_init", "wireless_init"),
    ("matrix_init", "matrix_init"),
    ("protocol_keyboard_task", "protocol_keyboard_task"),
    ("matrix_scan", "matrix_scan"),
]


# Hard-coded addresses in q3_max.resc that must match the ELF.
# Updated when firmware is rebuilt; verified at scenario startup.
EXPECTED_ADDRESSES = {
    "chSysPolledDelayX": "0x08024e24",
    "spiSend": "0x08025ace",
    "spiExchange": "0x08025aac",
    "rtc_enter_init": "0x080277f4",
    "usb_endpoint_in_send": "0x08024856",
    "obqWriteTimeout": "0x08025800",
    "USBD1": "0x20009b84",
}


def resolve_symbols(elf_path, nm_binary="arm-none-eabi-nm"):
    """Query arm-none-eabi-nm for known symbol addresses.

    Returns dict of variable_name -> hex address string (e.g. "0x08024bb6").
    Raises RuntimeError if nm fails or a symbol is missing.
    """
    result = subprocess.run(
        [nm_binary, "-n", str(elf_path)],
        capture_output=True, text=True, timeout=10,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "%s failed on %s: %s" % (nm_binary, elf_path, result.stderr.strip())
        )

    found = {}
    for line in result.stdout.splitlines():
        # nm -n output: "08024bb6 T usb_endpoint_in_send"
        m = re.match(r"^([0-9a-fA-F]+)\s+\S+\s+(\S+)$", line.strip())
        if not m:
            continue
        addr = "0x" + m.group(1)
        name = m.group(2)
        for pattern, var in KNOWN_SYMBOLS:
            if name == pattern and var not in found:
                found[var] = addr

    missing = [p for p, _ in KNOWN_SYMBOLS if p not in found]
    if missing:
        available = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        raise RuntimeError(
            "Missing symbols in %s: %s\nAvailable symbols (%d total): first 20 shown.\n%s"
            % (elf_path, missing, len(available), "\n".join(available[:20]))
        )

    return found


def make_renode_vars(syms):
    """Return Renode -e expressions that set $variable = address for each symbol.

    Example output: ["$usb_endpoint_in_send = 0x08024bb6", ...]
    """
    return ["$%s = %s" % (name, addr) for name, addr in sorted(syms.items())]


def verify_symbols(elf_path, expected):
    """Verify that expected addresses still match the current ELF.

    Returns a list of mismatch descriptions (empty if all match).
    """
    actual = resolve_symbols(elf_path)
    mismatches = []
    for name, exp_addr in expected.items():
        act_addr = actual.get(name)
        if act_addr is None:
            mismatches.append("%s: expected %s, symbol missing in ELF" % (name, exp_addr))
        elif act_addr != exp_addr:
            mismatches.append("%s: expected %s, got %s in ELF" % (name, exp_addr, act_addr))
    return mismatches
