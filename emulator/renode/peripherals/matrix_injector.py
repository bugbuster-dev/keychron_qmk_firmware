"""Matrix injector: simulates the keyboard switch matrix.

Pure-Python `MatrixState` models the press/release state and the
voltage at each row/col line based on diode direction. Renode
integration (when run inside Renode) wires this to GPIO pins so the
firmware's matrix-scan code sees the injected presses.

For Q3 Max (diode_direction = ROW2COL):
- Firmware drives one **col** pin LOW at a time.
- Firmware reads the **row** pins.
- A pressed switch at (r, c) connects row r to col c through a diode
  pointing row→col. When col c is low, row r reads low.
- Other cols (high) cannot pull rows low even if their switches are
  pressed, because the diode blocks back-flow.
"""

from __future__ import annotations


class MatrixState:
    """Switch-matrix electrical state model (Renode-independent)."""

    _DIODE_DIRECTIONS = ("ROW2COL", "COL2ROW")

    def __init__(self, rows: int, cols: int, diode_direction: str):
        if diode_direction not in self._DIODE_DIRECTIONS:
            raise ValueError(
                f"diode_direction must be one of {self._DIODE_DIRECTIONS}; "
                f"got {diode_direction!r}"
            )
        self.rows = rows
        self.cols = cols
        self.diode_direction = diode_direction
        self._pressed: set[tuple[int, int]] = set()
        self._row_driven = [False] * rows
        self._col_driven = [False] * cols

    # ---- press/release state ----

    def press(self, row: int, col: int) -> None:
        self._check_bounds(row, col)
        self._pressed.add((row, col))

    def release(self, row: int, col: int) -> None:
        self._check_bounds(row, col)
        self._pressed.discard((row, col))

    def pressed(self) -> list[tuple[int, int]]:
        return sorted(self._pressed)

    # ---- ROW2COL semantics (Q3 Max, diode points row→col) ----
    # Firmware drives **cols** low one at a time, reads **row** pins.
    # A pressed switch at (r, c) pulls row r low when col c is driven.

    def set_col_driven(self, col: int, active: bool) -> None:
        """Mark col as driven (low) or released (high/input)."""
        if not 0 <= col < self.cols:
            raise IndexError(f"col {col} out of range")
        self._col_driven[col] = bool(active)

    def read_row(self, row: int) -> bool:
        """Return True if row reads active (pulled low by a driven col)."""
        if not 0 <= row < self.rows:
            raise IndexError(f"row {row} out of range")
        for c in range(self.cols):
            if self._col_driven[c] and (row, c) in self._pressed:
                return True
        return False

    # ---- COL2ROW semantics (some boards, diode points col→row) ----
    # Firmware drives **rows** low one at a time, reads **col** pins.

    def set_row_driven(self, row: int, active: bool) -> None:
        """Mark row as driven (low) or released (high/input)."""
        if not 0 <= row < self.rows:
            raise IndexError(f"row {row} out of range")
        self._row_driven[row] = bool(active)

    def read_col(self, col: int) -> bool:
        """Return True if col reads active (pulled low by a driven row)."""
        if not 0 <= col < self.cols:
            raise IndexError(f"col {col} out of range")
        for r in range(self.rows):
            if self._row_driven[r] and (r, col) in self._pressed:
                return True
        return False

    # ---- helpers ----

    def _check_bounds(self, row: int, col: int) -> None:
        if not 0 <= row < self.rows:
            raise IndexError(f"row {row} out of range (have {self.rows})")
        if not 0 <= col < self.cols:
            raise IndexError(f"col {col} out of range (have {self.cols})")


# ----------------------------------------------------------------------
# Renode integration shim (activated only when imported inside Renode).
# Wires MatrixState to GPIO pins on the platform. The firmware's
# matrix scan toggles row pins as outputs; we watch those toggles and
# drive the corresponding col pins as inputs to reflect the pressed
# state.
# ----------------------------------------------------------------------

try:  # pragma: no cover — exercised only inside Renode
    from Antmicro.Renode.Logging import LogLevel  # type: ignore
    _IN_RENODE = True
except ImportError:
    _IN_RENODE = False


if _IN_RENODE:  # pragma: no cover
    # The Renode wrapper is intentionally minimal here; full wiring
    # (parsing info.json + binding to GPIO ports) is deferred to
    # the matrix_injector.resc loader script, which has cleaner
    # access to the platform's GPIO ports than this Python module.
    pass
