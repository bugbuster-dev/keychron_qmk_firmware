"""Unit tests for LKBT51 SPI-slave protocol stub.

Tests only the pure-Python protocol layer (no Renode required).
Reference: keyboards/keychron/common/wireless/lkbt51.c command opcodes.
"""

import pytest

from lkbt51_stub import Lkbt51Protocol

CMD_GET_MODULE_INFO = 0x40
CMD_READ_STATE_REG = 0x25
CMD_HAND_SHAKE_TOKEN = 0x61
CMD_SEND_KB = 0x11
CMD_SEND_KB_NKRO = 0x12
CMD_SEND_CONSUMER = 0x13
CMD_SEND_MOUSE = 0x16
CMD_BATTERY_MANAGE = 0x31


def test_ack_is_zero_byte():
    p = Lkbt51Protocol()
    assert p.ACK == 0x00


def test_get_module_info_returns_acked_blob():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_GET_MODULE_INFO, payload=b"")
    assert reply[0] == 0x00
    assert len(reply) >= 2  # ACK + at least one version byte


def test_read_state_reg_reports_no_host_paired():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_READ_STATE_REG, payload=b"")
    assert reply[0] == 0x00
    # All state bits zero = idle, wired-only, no pairing in progress.
    assert all(b == 0 for b in reply[1:])


def test_hand_shake_token_acked():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_HAND_SHAKE_TOKEN, payload=b"")
    assert reply[0] == 0x00


def test_send_kb_is_acked_and_discarded():
    p = Lkbt51Protocol()
    reply = p.handle_command(
        CMD_SEND_KB, payload=bytes([0, 0, 0x0B, 0, 0, 0, 0, 0])
    )
    assert reply == bytes([0x00])


def test_all_send_report_opcodes_acked():
    p = Lkbt51Protocol()
    for cmd in [
        CMD_SEND_KB,
        CMD_SEND_KB_NKRO,
        CMD_SEND_CONSUMER,
        CMD_SEND_MOUSE,
    ]:
        reply = p.handle_command(cmd, payload=b"\x00" * 8)
        assert reply == bytes([0x00]), f"opcode 0x{cmd:02x} not acked"


def test_battery_commands_acked_with_status():
    p = Lkbt51Protocol()
    reply = p.handle_command(CMD_BATTERY_MANAGE, payload=b"")
    assert reply[0] == 0x00
    # Convention: byte 1 = battery level percentage (100 = full).
    assert reply[1] == 100


def test_unknown_command_acks_with_zero_only():
    p = Lkbt51Protocol()
    reply = p.handle_command(0xFE, payload=b"")
    assert reply == bytes([0x00])


def test_payload_is_recorded_for_inspection():
    """Scenarios may want to assert which key reports the firmware
    sent over SPI. The stub records every command + payload it saw."""
    p = Lkbt51Protocol()
    p.handle_command(CMD_SEND_KB, payload=bytes([2, 0, 0x0B, 0, 0, 0, 0, 0]))
    p.handle_command(CMD_SEND_KB, payload=bytes([0, 0, 0, 0, 0, 0, 0, 0]))
    history = p.history()
    assert len(history) == 2
    assert history[0] == (CMD_SEND_KB, bytes([2, 0, 0x0B, 0, 0, 0, 0, 0]))
    assert history[1] == (CMD_SEND_KB, bytes([0, 0, 0, 0, 0, 0, 0, 0]))


def test_history_clear():
    p = Lkbt51Protocol()
    p.handle_command(CMD_SEND_KB, payload=b"\x00" * 8)
    p.clear_history()
    assert p.history() == []
