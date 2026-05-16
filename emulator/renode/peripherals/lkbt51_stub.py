"""LKBT51 SPI-slave protocol stub.

This module has two layers:

1. `Lkbt51Protocol` — pure-Python command/reply state machine.
   Unit-testable in isolation (no Renode needed). Implements the
   subset of the LKBT51 command set the Q3 Max firmware sends
   during boot and steady-state operation. Reference:
   keyboards/keychron/common/wireless/lkbt51.c.

2. A Renode `PythonPeripheral` wrapper that wires the protocol to
   SPI1's slave port. Activated only when the module is imported
   inside Renode (where `request`, `Antmicro.Renode.Logging`, etc.
   are available).

v1.0 behaviour: respond as "no host paired, wired-only mode,
battery 100%." This stays static; v2.5 will add scriptability so
scenarios can pretend a Bluetooth host paired and the firmware
should switch to wireless mode.
"""

from __future__ import annotations


class Lkbt51Protocol:
    """Pure-Python LKBT51 command protocol."""

    ACK = 0x00

    # Command opcodes from lkbt51.c:
    CMD_SEND_KB = 0x11
    CMD_SEND_KB_NKRO = 0x12
    CMD_SEND_CONSUMER = 0x13
    CMD_SEND_SYSTEM = 0x14
    CMD_SEND_FN = 0x15
    CMD_SEND_MOUSE = 0x16
    CMD_SEND_BOOT_KB = 0x17

    CMD_PAIRING = 0x21
    CMD_CONNECT = 0x22
    CMD_DISCONNECT = 0x23
    CMD_SWITCH_HOST = 0x24
    CMD_READ_STATE_REG = 0x25

    CMD_BATTERY_MANAGE = 0x31
    CMD_UPDATE_BAT_LVL = 0x32
    CMD_UPDATE_BAT_STATE = 0x33

    CMD_GET_MODULE_INFO = 0x40
    CMD_HAND_SHAKE_TOKEN = 0x61

    def __init__(self) -> None:
        self._history: list[tuple[int, bytes]] = []

    def handle_command(self, cmd: int, payload: bytes) -> bytes:
        """Return the reply byte sequence for a single SPI command."""
        self._history.append((cmd, bytes(payload)))

        if cmd == self.CMD_GET_MODULE_INFO:
            # ACK + fw_ver_hi.fw_ver_lo + hw_ver_hi.hw_ver_lo (plausible).
            return bytes([self.ACK, 0x01, 0x00, 0x01, 0x00])

        if cmd == self.CMD_READ_STATE_REG:
            # ACK + 3 state bytes; all zero = idle, wired, no pairing.
            return bytes([self.ACK, 0x00, 0x00, 0x00])

        if cmd == self.CMD_HAND_SHAKE_TOKEN:
            # ACK + plausible echo token.
            return bytes([self.ACK, 0x55, 0xAA])

        # All HID-report sends (0x11..0x17): firmware mirrors USB reports
        # to LKBT51 when wireless is enabled. We swallow + ACK.
        if 0x11 <= cmd <= 0x17:
            return bytes([self.ACK])

        # Battery commands: ACK + 100% charge.
        if 0x31 <= cmd <= 0x33:
            return bytes([self.ACK, 100])

        # Default: bare ACK for anything we don't model. The firmware
        # tolerates unknown replies as long as the SPI transaction
        # completes; the LKBT51 protocol is forgiving by design.
        return bytes([self.ACK])

    def history(self) -> list[tuple[int, bytes]]:
        """Return all commands seen since construction or last clear."""
        return list(self._history)

    def clear_history(self) -> None:
        self._history.clear()


# ----------------------------------------------------------------------
# Renode integration shim (activated only when imported inside Renode).
# Renode injects `request`, `Antmicro`, `Request`, `self` etc. into the
# script's globals when it executes a PythonPeripheral. We gate the
# shim on the presence of those symbols so unit tests outside Renode
# can import this module cleanly.
# ----------------------------------------------------------------------

try:  # pragma: no cover — exercised only inside Renode
    from Antmicro.Renode.Logging import LogLevel  # type: ignore
    _IN_RENODE = True
except ImportError:
    _IN_RENODE = False


if _IN_RENODE:  # pragma: no cover
    # State: a single protocol instance reused across SPI transactions.
    # Each SPI write byte from the master is appended to _rx; once the
    # transaction completes (chip-select goes high or the master
    # sends a known command-terminator), we pop the command + payload
    # off the queue and stage the reply for subsequent master reads.
    _proto = Lkbt51Protocol()
    _rx: list[int] = []
    _tx: list[int] = []

    def Transmit(request):  # noqa: N802 — Renode API name
        """Called by Renode when the SPI master writes/reads a byte.

        For now we use a simple framing: byte 0 of every transaction
        is the command; payload bytes follow up to a fixed length per
        command. After the payload is complete, the reply is staged
        for the next master read. Reads while no reply is staged
        return 0x00 (the ACK convention).
        """
        global _rx, _tx
        in_byte = int(request.value) & 0xFF

        if not _tx and _rx:
            # Master sending another byte while we have no staged reply
            # — interpret as command continuation.
            pass

        if not _rx:
            _rx = [in_byte]
            # Single-byte commands stage their reply immediately.
            cmd = in_byte
            if cmd in (
                Lkbt51Protocol.CMD_GET_MODULE_INFO,
                Lkbt51Protocol.CMD_READ_STATE_REG,
                Lkbt51Protocol.CMD_HAND_SHAKE_TOKEN,
                Lkbt51Protocol.CMD_BATTERY_MANAGE,
            ):
                _tx = list(_proto.handle_command(cmd, b""))
                _rx = []
            out_byte = _tx.pop(0) if _tx else 0x00
        else:
            # Continuation of a multi-byte command.
            _rx.append(in_byte)
            # HID-report commands always have 8 bytes of payload.
            cmd = _rx[0]
            if 0x11 <= cmd <= 0x17 and len(_rx) >= 9:
                _tx = list(
                    _proto.handle_command(cmd, bytes(_rx[1:]))
                )
                _rx = []
            out_byte = _tx.pop(0) if _tx else 0x00

        request.value = out_byte
