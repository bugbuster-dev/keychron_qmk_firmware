"""Layout: load matrix layout + keymap → (row, col) mapping from QMK files.

Reads:
- `<keyboard_dir>/info.json` for matrix_pins and diode_direction
- `<keyboard_dir>/<variant>/keyboard.json` for layout positions
- `<keyboard_dir>/<variant>/keymaps/<keymap>/keymap.c` for layer-0 KC_*
  assignments (default keymap name: "keychron")

The layout is the single source of truth: emulator scenarios call
`layout.coords("KC_H")` and get the `(row, col)` tuple to drive the
matrix injector.
"""

from __future__ import annotations

import json
import re
from pathlib import Path


class Layout:
    def __init__(
        self,
        key_to_coord: dict[str, tuple[int, int]],
        row_pins: list[str],
        col_pins: list[str],
        diode_direction: str,
    ):
        self._k2c = key_to_coord
        self.row_pins = row_pins
        self.col_pins = col_pins
        self.diode_direction = diode_direction

    @classmethod
    def from_keyboard(
        cls,
        keyboard_dir: Path,
        variant: str,
        layout_name: str,
        keymap: str = "keychron",
    ) -> "Layout":
        keyboard_dir = Path(keyboard_dir)
        info = json.loads((keyboard_dir / "info.json").read_text())
        variant_kb = json.loads(
            (keyboard_dir / variant / "keyboard.json").read_text()
        )

        layout_entries = variant_kb["layouts"][layout_name]["layout"]
        keymap_path = (
            keyboard_dir / variant / "keymaps" / keymap / "keymap.c"
        )
        layer0_tokens = cls._parse_layer0(keymap_path)

        if len(layer0_tokens) != len(layout_entries):
            raise ValueError(
                f"keymap layer 0 has {len(layer0_tokens)} tokens, "
                f"layout {layout_name} has {len(layout_entries)} positions"
            )

        k2c: dict[str, tuple[int, int]] = {}
        for tok, entry in zip(layer0_tokens, layout_entries):
            row, col = entry["matrix"]
            # First occurrence wins so KC_MUTE (which appears twice)
            # resolves to its first physical key, not the last.
            k2c.setdefault(tok, (row, col))

        return cls(
            k2c,
            row_pins=info["matrix_pins"]["rows"],
            col_pins=info["matrix_pins"]["cols"],
            diode_direction=info["diode_direction"],
        )

    @staticmethod
    def _parse_layer0(keymap_c: Path) -> list[str]:
        """Extract tokens from the FIRST [LAYER] = LAYOUT_*(...) call.

        Strips C-style comments, then captures everything between the
        matched parens of the first LAYOUT macro invocation. Tokens
        are comma-separated, stripped of whitespace.
        """
        text = keymap_c.read_text()
        # Strip block comments and line comments to avoid false splits.
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
        text = re.sub(r"//[^\n]*", " ", text)
        m = re.search(r"LAYOUT[A-Za-z0-9_]*\s*\(", text)
        if not m:
            raise ValueError(f"no LAYOUT(...) macro in {keymap_c}")
        depth = 0
        start = m.end() - 1  # at the opening '('
        body: str | None = None
        for i in range(start, len(text)):
            ch = text[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    body = text[start + 1 : i]
                    break
        if body is None:
            raise ValueError(f"unbalanced LAYOUT(...) parens in {keymap_c}")
        # Split on commas at depth 0 only (to keep MO(2), LT(2,KC_X) intact).
        tokens: list[str] = []
        cur = ""
        depth = 0
        for ch in body:
            if ch == "(":
                depth += 1
                cur += ch
            elif ch == ")":
                depth -= 1
                cur += ch
            elif ch == "," and depth == 0:
                tokens.append(cur.strip())
                cur = ""
            else:
                cur += ch
        if cur.strip():
            tokens.append(cur.strip())
        return tokens

    def coords(self, key: str) -> tuple[int, int]:
        if key not in self._k2c:
            raise KeyError(key)
        return self._k2c[key]

    def all_keys(self) -> list[str]:
        return list(self._k2c.keys())
