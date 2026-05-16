"""Unit tests for layout module."""
from pathlib import Path

import pytest

from layout import Layout

REPO_ROOT = Path(__file__).resolve().parents[3]


def test_coords_for_kc_h():
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    row, col = layout.coords("KC_H")
    assert isinstance(row, int) and isinstance(col, int)
    assert 0 <= row < 6 and 0 <= col < 17


def test_all_keys_nonempty():
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    keys = layout.all_keys()
    assert len(keys) > 50  # full-size keyboard has many keys


def test_missing_key_raises():
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    with pytest.raises(KeyError):
        layout.coords("KC_NOT_A_REAL_KEY")


def test_row_col_pins_loaded():
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    assert layout.row_pins == ["C12", "D2", "B3", "B4", "B5", "B6"]
    assert layout.col_pins[0] == "C6"
    assert len(layout.col_pins) == 17


def test_diode_direction():
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    assert layout.diode_direction == "ROW2COL"


def test_alphabet_keys_present():
    """Every letter A-Z should map to a (row, col)."""
    layout = Layout.from_keyboard(
        REPO_ROOT / "keyboards/keychron/q3_max",
        variant="ansi_encoder",
        layout_name="LAYOUT_tkl_ansi",
    )
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        coords = layout.coords(f"KC_{ch}")
        assert coords is not None
