"""Unit tests for matrix injector state model."""

import pytest

from matrix_injector import MatrixState


def test_no_keys_pressed_returns_inactive_row():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    # ROW2COL: master drives row low, reads col. If no keys pressed,
    # all cols read high (inactive).
    m.set_row_driven(0, active=True)
    for c in range(17):
        assert m.read_col(c) is False


def test_press_makes_col_active_when_correct_row_driven():
    """ROW2COL: pressing (r, c) means when row r is driven (low),
    column c reads low (active)."""
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.set_row_driven(2, active=True)
    assert m.read_col(5) is True
    assert m.read_col(4) is False
    assert m.read_col(6) is False


def test_press_isolated_to_driven_row():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    # Drive a DIFFERENT row → col 5 should NOT show as active.
    m.set_row_driven(3, active=True)
    assert m.read_col(5) is False


def test_release_clears_press():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.release(row=2, col=5)
    m.set_row_driven(2, active=True)
    assert m.read_col(5) is False


def test_multiple_presses_independent():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.press(row=2, col=9)
    m.press(row=4, col=11)
    m.set_row_driven(2, active=True)
    assert m.read_col(5) is True
    assert m.read_col(9) is True
    assert m.read_col(11) is False
    m.set_row_driven(2, active=False)
    m.set_row_driven(4, active=True)
    assert m.read_col(11) is True
    assert m.read_col(5) is False


def test_diode_direction_col2row():
    """COL2ROW: master drives col, reads row. Swap dimensions of
    set/read semantics."""
    m = MatrixState(rows=5, cols=14, diode_direction="COL2ROW")
    m.press(row=2, col=5)
    m.set_col_driven(5, active=True)
    assert m.read_row(2) is True
    assert m.read_row(0) is False


def test_invalid_diode_direction_rejected():
    with pytest.raises(ValueError):
        MatrixState(rows=6, cols=17, diode_direction="FORWARDS")


def test_out_of_range_indices_rejected():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    with pytest.raises(IndexError):
        m.press(row=6, col=0)
    with pytest.raises(IndexError):
        m.press(row=0, col=17)
    with pytest.raises(IndexError):
        m.read_col(17)


def test_pressed_set_introspection():
    """Scenarios may want to query currently-pressed keys."""
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=1, col=2)
    m.press(row=3, col=4)
    assert set(m.pressed()) == {(1, 2), (3, 4)}
    m.release(row=1, col=2)
    assert set(m.pressed()) == {(3, 4)}


def test_inactive_row_reads_no_cols_active():
    """If the row line is high (driven=False in our model), the row's
    pressed keys must NOT pull cols low."""
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.set_row_driven(2, active=False)
    assert m.read_col(5) is False
