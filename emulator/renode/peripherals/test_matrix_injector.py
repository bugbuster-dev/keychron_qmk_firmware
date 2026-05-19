"""Unit tests for matrix injector state model."""

import pytest

from matrix_injector import MatrixState


def test_no_keys_pressed_returns_inactive_row():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    # ROW2COL: master drives col low, reads row. If no keys pressed,
    # all rows read high (inactive).
    m.set_col_driven(0, active=True)
    for r in range(6):
        assert m.read_row(r) is False


def test_press_makes_row_active_when_correct_col_driven():
    """ROW2COL: pressing (r, c) means when col c is driven (low),
    row r reads low (active)."""
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.set_col_driven(5, active=True)
    assert m.read_row(2) is True
    assert m.read_row(1) is False
    assert m.read_row(3) is False


def test_press_isolated_to_driven_col():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    # Drive a DIFFERENT col → row 2 should NOT show as active.
    m.set_col_driven(6, active=True)
    assert m.read_row(2) is False


def test_release_clears_press():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.release(row=2, col=5)
    m.set_col_driven(5, active=True)
    assert m.read_row(2) is False


def test_multiple_presses_independent():
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.press(row=4, col=5)
    m.press(row=4, col=11)
    m.set_col_driven(5, active=True)
    assert m.read_row(2) is True
    assert m.read_row(4) is True
    assert m.read_row(1) is False
    m.set_col_driven(5, active=False)
    m.set_col_driven(11, active=True)
    assert m.read_row(4) is True
    assert m.read_row(2) is False


def test_diode_direction_col2row():
    """COL2ROW: master drives row, reads col. Swap dimensions of
    set/read semantics."""
    m = MatrixState(rows=5, cols=14, diode_direction="COL2ROW")
    m.press(row=2, col=5)
    m.set_row_driven(2, active=True)
    assert m.read_col(5) is True
    assert m.read_col(0) is False


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


def test_inactive_col_reads_no_rows_active():
    """If the col line is high (driven=False in our model), the col's
    pressed keys must NOT pull rows low."""
    m = MatrixState(rows=6, cols=17, diode_direction="ROW2COL")
    m.press(row=2, col=5)
    m.set_col_driven(5, active=False)
    assert m.read_row(2) is False
