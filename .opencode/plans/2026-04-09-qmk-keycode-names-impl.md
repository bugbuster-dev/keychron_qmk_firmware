# QMK Keycode Names Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Allow combo config UI to accept QMK keycode names (KC_A, LCTL(KC_B), LT(1, KC_SPC)) and display stored keycodes as names.

**Architecture:** New `QMKataKeycodes.py` module with `KeycodeResolver` class that parses HJSON data files for name<->value mapping and evaluates QMK macro expressions. UI layer translates between names and uint16 values; keyboard communication layer unchanged.

**Tech Stack:** Python 3, PySide6, hjson package, QMK HJSON data files

**Design doc:** `.opencode/plans/2026-04-09-qmk-keycode-names-design.md`

---

### Task 1: Install hjson dependency

**Step 1: Install hjson**

```bash
pip install hjson
```

**Step 2: Verify import works**

```bash
python3 -c "import hjson; print(hjson.__version__)"
```

Expected: prints version, no errors.

**Step 3: Commit**

No commit needed for pip install.

---

### Task 2: HJSON loader - build name<->value dicts

**Files:**
- Create: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeycodes.py`

**Step 1: Write the HJSON loader**

Create `QMKataKeycodes.py` with class `KeycodeResolver`:

```python
import os
import re
import hjson


class KeycodeResolver:
    """Resolves QMK keycode names to/from uint16 values.

    Loads keycode definitions from QMK firmware HJSON data files.
    Supports named keycodes (KC_A), raw hex (0x0004), and
    macro expressions (LCTL(KC_A), LT(1, KC_SPC)).
    """

    # QK_* range base addresses (from keycodes.h enum qk_keycode_ranges)
    QK_BASIC       = 0x0000
    QK_MODS        = 0x0100
    QK_MODS_MAX    = 0x1FFF
    QK_MOD_TAP     = 0x2000
    QK_MOD_TAP_MAX = 0x3FFF
    QK_LAYER_TAP     = 0x4000
    QK_LAYER_TAP_MAX = 0x4FFF
    QK_LAYER_MOD     = 0x5000
    QK_LAYER_MOD_MAX = 0x51FF
    QK_TO              = 0x5200
    QK_TO_MAX          = 0x521F
    QK_MOMENTARY       = 0x5220
    QK_MOMENTARY_MAX   = 0x523F
    QK_DEF_LAYER       = 0x5240
    QK_DEF_LAYER_MAX   = 0x525F
    QK_TOGGLE_LAYER       = 0x5260
    QK_TOGGLE_LAYER_MAX   = 0x527F
    QK_ONE_SHOT_LAYER     = 0x5280
    QK_ONE_SHOT_LAYER_MAX = 0x529F
    QK_ONE_SHOT_MOD       = 0x52A0
    QK_ONE_SHOT_MOD_MAX   = 0x52BF
    QK_LAYER_TAP_TOGGLE     = 0x52C0
    QK_LAYER_TAP_TOGGLE_MAX = 0x52DF

    # Modifier bits for QK_MODS range (OR'd into high byte)
    QK_LCTL = 0x0100
    QK_LSFT = 0x0200
    QK_LALT = 0x0400
    QK_LGUI = 0x0800
    QK_RCTL = 0x1100
    QK_RSFT = 0x1200
    QK_RALT = 0x1400
    QK_RGUI = 0x1800

    # 5-bit packed modifier constants (from modifiers.h)
    MOD_LCTL = 0x01
    MOD_LSFT = 0x02
    MOD_LALT = 0x04
    MOD_LGUI = 0x08
    MOD_RCTL = 0x11
    MOD_RSFT = 0x12
    MOD_RALT = 0x14
    MOD_RGUI = 0x18

    def __init__(self, firmware_path=None):
        self.name_to_value = {}   # "KC_A" -> 0x0004 (includes aliases)
        self.value_to_name = {}   # 0x0004 -> "KC_A" (canonical only)

        if firmware_path:
            self._load_hjson(firmware_path)
        self._register_mod_constants()
        self._register_macros()

    def _load_hjson(self, firmware_path):
        """Load keycodes from HJSON data files, layering versions."""
        data_dir = os.path.join(firmware_path, "data", "constants", "keycodes")
        if not os.path.isdir(data_dir):
            return

        # Collect and sort HJSON files by version then category
        hjson_files = []
        for f in os.listdir(data_dir):
            if f.startswith("keycodes_") and f.endswith(".hjson"):
                hjson_files.append(f)
        # Sort: 0.0.1 before 0.0.2 before 0.0.3, categories within version
        hjson_files.sort()

        for fname in hjson_files:
            fpath = os.path.join(data_dir, fname)
            try:
                with open(fpath, "r") as f:
                    data = hjson.load(f)
                keycodes = data.get("keycodes", {})
                for hex_str, entry in keycodes.items():
                    value = int(hex_str, 16)
                    key = entry.get("key", "")
                    if key:
                        self.name_to_value[key] = value
                        self.value_to_name[value] = key
                    for alias in entry.get("aliases", []):
                        self.name_to_value[alias] = value
            except Exception:
                pass  # Skip files that fail to parse

    def _register_mod_constants(self):
        """Register MOD_* constants so they can be used in macro args."""
        mod_constants = {
            "MOD_LCTL": self.MOD_LCTL, "MOD_LSFT": self.MOD_LSFT,
            "MOD_LALT": self.MOD_LALT, "MOD_LGUI": self.MOD_LGUI,
            "MOD_RCTL": self.MOD_RCTL, "MOD_RSFT": self.MOD_RSFT,
            "MOD_RALT": self.MOD_RALT, "MOD_RGUI": self.MOD_RGUI,
        }
        self.name_to_value.update(mod_constants)

    def _register_macros(self):
        """Build macro function lookup table."""
        # Single-arg modifier wrappers: name -> QK_* bits to OR
        self._mod_macros = {
            "LCTL": self.QK_LCTL, "LSFT": self.QK_LSFT,
            "LALT": self.QK_LALT, "LGUI": self.QK_LGUI,
            "LOPT": self.QK_LALT, "LCMD": self.QK_LGUI, "LWIN": self.QK_LGUI,
            "RCTL": self.QK_RCTL, "RSFT": self.QK_RSFT,
            "RALT": self.QK_RALT, "RGUI": self.QK_RGUI,
            "ALGR": self.QK_RALT, "ROPT": self.QK_RALT,
            "RCMD": self.QK_RGUI, "RWIN": self.QK_RGUI,
            "C": self.QK_LCTL, "S": self.QK_LSFT,
            "A": self.QK_LALT, "G": self.QK_LGUI,
            "HYPR": self.QK_LCTL | self.QK_LSFT | self.QK_LALT | self.QK_LGUI,
            "MEH":  self.QK_LCTL | self.QK_LSFT | self.QK_LALT,
            "LCAG": self.QK_LCTL | self.QK_LALT | self.QK_LGUI,
            "LSG":  self.QK_LSFT | self.QK_LGUI,
            "SGUI": self.QK_LSFT | self.QK_LGUI,
            "SCMD": self.QK_LSFT | self.QK_LGUI,
            "SWIN": self.QK_LSFT | self.QK_LGUI,
            "LAG":  self.QK_LALT | self.QK_LGUI,
            "RSG":  self.QK_RSFT | self.QK_RGUI,
            "RAG":  self.QK_RALT | self.QK_RGUI,
            "LCA":  self.QK_LCTL | self.QK_LALT,
            "LSA":  self.QK_LSFT | self.QK_LALT,
            "RSA":  self.QK_RSFT | self.QK_RALT,
            "RCS":  self.QK_RCTL | self.QK_RSFT,
            "SAGR": self.QK_RSFT | self.QK_RALT,
        }

        # Single-arg layer macros: name -> (base_addr, mask)
        self._layer_macros = {
            "TO":  (self.QK_TO, 0x1F),
            "MO":  (self.QK_MOMENTARY, 0x1F),
            "DF":  (self.QK_DEF_LAYER, 0x1F),
            "TG":  (self.QK_TOGGLE_LAYER, 0x1F),
            "OSL": (self.QK_ONE_SHOT_LAYER, 0x1F),
            "TT":  (self.QK_LAYER_TAP_TOGGLE, 0x1F),
        }

        # Single-arg one-shot mod: name -> (base_addr, mask)
        # OSM(mod) = QK_ONE_SHOT_MOD | (mod & 0x1F)
        # Handled specially since arg is a mod, not a layer

        # Mod-tap shortcut macros: name -> MOD_* value
        self._modtap_macros = {
            "LCTL_T": self.MOD_LCTL, "RCTL_T": self.MOD_RCTL, "CTL_T": self.MOD_LCTL,
            "LSFT_T": self.MOD_LSFT, "RSFT_T": self.MOD_RSFT, "SFT_T": self.MOD_LSFT,
            "LALT_T": self.MOD_LALT, "RALT_T": self.MOD_RALT, "ALT_T": self.MOD_LALT,
            "LOPT_T": self.MOD_LALT, "ROPT_T": self.MOD_RALT, "OPT_T": self.MOD_LALT,
            "ALGR_T": self.MOD_RALT,
            "LGUI_T": self.MOD_LGUI, "RGUI_T": self.MOD_RGUI, "GUI_T": self.MOD_LGUI,
            "LCMD_T": self.MOD_LGUI, "RCMD_T": self.MOD_RGUI, "CMD_T": self.MOD_LGUI,
            "LWIN_T": self.MOD_LGUI, "RWIN_T": self.MOD_RGUI, "WIN_T": self.MOD_LGUI,
            "ALL_T": self.MOD_LCTL | self.MOD_LSFT | self.MOD_LALT | self.MOD_LGUI,
            "MEH_T": self.MOD_LCTL | self.MOD_LSFT | self.MOD_LALT,
            "LCAG_T": self.MOD_LCTL | self.MOD_LALT | self.MOD_LGUI,
            "RCAG_T": self.MOD_RCTL | self.MOD_RALT | self.MOD_RGUI,
            "HYPR_T": self.MOD_LCTL | self.MOD_LSFT | self.MOD_LALT | self.MOD_LGUI,
            "C_S_T": self.MOD_LCTL | self.MOD_LSFT,
            "LSG_T": self.MOD_LSFT | self.MOD_LGUI,
            "SGUI_T": self.MOD_LSFT | self.MOD_LGUI,
            "SCMD_T": self.MOD_LSFT | self.MOD_LGUI,
            "SWIN_T": self.MOD_LSFT | self.MOD_LGUI,
            "LAG_T": self.MOD_LALT | self.MOD_LGUI,
            "RSG_T": self.MOD_RSFT | self.MOD_RGUI,
            "RAG_T": self.MOD_RALT | self.MOD_RGUI,
            "LCA_T": self.MOD_LCTL | self.MOD_LALT,
            "LSA_T": self.MOD_LSFT | self.MOD_LALT,
            "RSA_T": self.MOD_RSFT | self.MOD_RALT,
            "RCS_T": self.MOD_RCTL | self.MOD_RSFT,
            "SAGR_T": self.MOD_RSFT | self.MOD_RALT,
        }
```

**Step 2: Verify HJSON loading works**

```bash
python3 -c "
import sys; sys.path.insert(0, '/home/user/qmk/qmk-tools/qmk/QMKata')
from QMKataKeycodes import KeycodeResolver
r = KeycodeResolver('/home/user/qmk/keychron_qmk_firmware')
print(f'Loaded {len(r.name_to_value)} names, {len(r.value_to_name)} values')
print(f'KC_A = 0x{r.name_to_value.get(\"KC_A\", -1):04x}')
print(f'KC_ENT = 0x{r.name_to_value.get(\"KC_ENT\", -1):04x}')
print(f'KC_LCTL = 0x{r.name_to_value.get(\"KC_LCTL\", -1):04x}')
print(f'QK_BOOT = 0x{r.name_to_value.get(\"QK_BOOT\", -1):04x}')
print(f'0x0004 -> {r.value_to_name.get(0x0004, \"?\")}')
"
```

Expected: prints loaded counts, KC_A=0x0004, KC_ENT=0x0028, etc.

**Step 3: Commit**

```
feat: add QMKataKeycodes HJSON loader for name<->value mapping
```

---

### Task 3: Expression evaluator (resolve method)

**Files:**
- Modify: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeycodes.py`

**Step 1: Add the resolve method to KeycodeResolver**

```python
    def resolve(self, expr):
        """Resolve a keycode expression to a uint16 value.

        Accepts:
          - Raw hex: "0x0004"
          - Raw decimal: "4"
          - Named keycode: "KC_A"
          - Modifier wrapper: "LCTL(KC_A)"
          - Layer macro: "TO(1)", "MO(2)"
          - Layer-Tap: "LT(1, KC_SPC)"
          - Mod-Tap: "MT(MOD_LCTL, KC_A)", "LCTL_T(KC_A)"
          - Layer-Mod: "LM(1, MOD_LCTL)"
          - One-shot mod: "OSM(MOD_LSFT)"
          - MOD_* expressions with |: "MOD_LCTL | MOD_LSFT"

        Returns: int (uint16 keycode value)
        Raises: ValueError if expression cannot be resolved
        """
        expr = expr.strip()
        if not expr:
            raise ValueError("Empty expression")

        # Raw hex
        if expr.startswith("0x") or expr.startswith("0X"):
            return int(expr, 16)

        # Raw decimal (only if all digits)
        if expr.isdigit():
            return int(expr)

        # Check for MOD_* bitwise OR expressions (e.g. "MOD_LCTL | MOD_LSFT")
        if "|" in expr and "(" not in expr:
            result = 0
            for part in expr.split("|"):
                result |= self.resolve(part.strip())
            return result

        # Macro expression: NAME(args)
        paren_idx = expr.find("(")
        if paren_idx != -1:
            if not expr.endswith(")"):
                raise ValueError(f"Unbalanced parentheses: {expr}")
            func_name = expr[:paren_idx].strip()
            args_str = expr[paren_idx + 1:-1].strip()
            return self._eval_macro(func_name, args_str)

        # Named keycode lookup
        if expr in self.name_to_value:
            return self.name_to_value[expr]

        raise ValueError(f"Unknown keycode: {expr}")

    def _split_args(self, args_str):
        """Split macro arguments respecting nested parentheses."""
        args = []
        depth = 0
        current = []
        for ch in args_str:
            if ch == "," and depth == 0:
                args.append("".join(current).strip())
                current = []
            else:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                current.append(ch)
        if current:
            args.append("".join(current).strip())
        return [a for a in args if a]

    def _eval_macro(self, func_name, args_str):
        """Evaluate a macro expression."""
        args = self._split_args(args_str)

        # Single-arg modifier wrappers: LCTL(kc), S(kc), HYPR(kc), etc.
        if func_name in self._mod_macros:
            if len(args) != 1:
                raise ValueError(f"{func_name}() takes 1 argument, got {len(args)}")
            kc = self.resolve(args[0])
            return self._mod_macros[func_name] | kc

        # Single-arg layer macros: TO(layer), MO(layer), etc.
        if func_name in self._layer_macros:
            if len(args) != 1:
                raise ValueError(f"{func_name}() takes 1 argument, got {len(args)}")
            base, mask = self._layer_macros[func_name]
            layer = self.resolve(args[0])
            return base | (layer & mask)

        # OSM(mod)
        if func_name == "OSM":
            if len(args) != 1:
                raise ValueError(f"OSM() takes 1 argument, got {len(args)}")
            mod = self.resolve(args[0])
            return self.QK_ONE_SHOT_MOD | (mod & 0x1F)

        # LT(layer, kc)
        if func_name == "LT":
            if len(args) != 2:
                raise ValueError(f"LT() takes 2 arguments, got {len(args)}")
            layer = self.resolve(args[0])
            kc = self.resolve(args[1])
            return self.QK_LAYER_TAP | ((layer & 0xF) << 8) | (kc & 0xFF)

        # MT(mod, kc)
        if func_name == "MT":
            if len(args) != 2:
                raise ValueError(f"MT() takes 2 arguments, got {len(args)}")
            mod = self.resolve(args[0])
            kc = self.resolve(args[1])
            return self.QK_MOD_TAP | ((mod & 0x1F) << 8) | (kc & 0xFF)

        # LM(layer, mod)
        if func_name == "LM":
            if len(args) != 2:
                raise ValueError(f"LM() takes 2 arguments, got {len(args)}")
            layer = self.resolve(args[0])
            mod = self.resolve(args[1])
            return self.QK_LAYER_MOD | ((layer & 0xF) << 5) | (mod & 0x1F)

        # Mod-tap shortcuts: LCTL_T(kc), MEH_T(kc), etc.
        if func_name in self._modtap_macros:
            if len(args) != 1:
                raise ValueError(f"{func_name}() takes 1 argument, got {len(args)}")
            mod = self._modtap_macros[func_name]
            kc = self.resolve(args[0])
            return self.QK_MOD_TAP | ((mod & 0x1F) << 8) | (kc & 0xFF)

        raise ValueError(f"Unknown macro: {func_name}()")
```

**Step 2: Verify resolve works**

```bash
python3 -c "
import sys; sys.path.insert(0, '/home/user/qmk/qmk-tools/qmk/QMKata')
from QMKataKeycodes import KeycodeResolver
r = KeycodeResolver('/home/user/qmk/keychron_qmk_firmware')
tests = [
    ('KC_A', 0x0004),
    ('0x0004', 0x0004),
    ('4', 4),
    ('KC_LCTL', 0x00E0),
    ('LCTL(KC_A)', 0x0100 | 0x0004),
    ('S(KC_1)', 0x0200 | 0x001E),
    ('HYPR(KC_X)', 0x0F00 | 0x001B),
    ('TO(1)', 0x5200 | 1),
    ('MO(2)', 0x5220 | 2),
    ('LT(1, KC_SPC)', 0x4000 | (1 << 8) | 0x2C),
    ('MT(MOD_LCTL, KC_A)', 0x2000 | (0x01 << 8) | 0x0004),
    ('LCTL_T(KC_A)', 0x2000 | (0x01 << 8) | 0x0004),
    ('OSM(MOD_LSFT)', 0x52A0 | 0x02),
    ('LM(1, MOD_LCTL)', 0x5000 | (1 << 5) | 0x01),
]
for expr, expected in tests:
    result = r.resolve(expr)
    status = 'OK' if result == expected else f'FAIL (got 0x{result:04x}, expected 0x{expected:04x})'
    print(f'  {expr:30s} -> 0x{result:04x}  {status}')
"
```

Expected: all OK.

**Step 3: Commit**

```
feat: add expression evaluator for QMK keycode macros
```

---

### Task 4: Reverse display (value_to_display method)

**Files:**
- Modify: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeycodes.py`

**Step 1: Add value_to_display method**

```python
    def value_to_display(self, value):
        """Convert a uint16 keycode value to human-readable display string.

        Returns the most readable representation:
        - Named keycode if in value_to_name: "KC_A"
        - Macro decomposition for known ranges: "LCTL(KC_A)", "LT(1, KC_SPC)"
        - Hex fallback: "0x1234"
        """
        # Direct lookup
        if value in self.value_to_name:
            return self.value_to_name[value]

        # QK_MODS range: modifier + basic keycode
        if self.QK_MODS <= value <= self.QK_MODS_MAX:
            mods = (value >> 8) & 0x1F
            basic = value & 0xFF
            basic_name = self.value_to_name.get(basic, f"0x{basic:02x}")
            mod_name = self._mods_to_macro_name(mods)
            if mod_name:
                return f"{mod_name}({basic_name})"
            return f"0x{value:04x}"

        # QK_MOD_TAP range
        if self.QK_MOD_TAP <= value <= self.QK_MOD_TAP_MAX:
            mod = (value >> 8) & 0x1F
            kc = value & 0xFF
            kc_name = self.value_to_name.get(kc, f"0x{kc:02x}")
            # Try to find a shortcut name (LCTL_T, etc.)
            shortcut = self._mod_to_modtap_name(mod)
            if shortcut:
                return f"{shortcut}({kc_name})"
            mod_name = self._mod5_to_name(mod)
            return f"MT({mod_name}, {kc_name})"

        # QK_LAYER_TAP range
        if self.QK_LAYER_TAP <= value <= self.QK_LAYER_TAP_MAX:
            layer = (value >> 8) & 0xF
            kc = value & 0xFF
            kc_name = self.value_to_name.get(kc, f"0x{kc:02x}")
            return f"LT({layer}, {kc_name})"

        # QK_LAYER_MOD range
        if self.QK_LAYER_MOD <= value <= self.QK_LAYER_MOD_MAX:
            layer = (value >> 5) & 0xF
            mod = value & 0x1F
            mod_name = self._mod5_to_name(mod)
            return f"LM({layer}, {mod_name})"

        # Single-arg layer macros
        for name, (base, mask) in self._layer_macros.items():
            max_val = base | mask
            if base <= value <= max_val:
                layer = value & mask
                return f"{name}({layer})"

        # QK_ONE_SHOT_MOD
        if self.QK_ONE_SHOT_MOD <= value <= self.QK_ONE_SHOT_MOD_MAX:
            mod = value & 0x1F
            mod_name = self._mod5_to_name(mod)
            return f"OSM({mod_name})"

        # Fallback
        return f"0x{value:04x}"

    def _mods_to_macro_name(self, mods_8bit):
        """Convert 8-bit QK_MODS modifier bits to macro name."""
        # Map of modifier bit combinations to macro names
        # mods_8bit is bits [12:8] of the keycode = 5-bit: RGUI RALT RSFT RCTL LGUI LALT LSFT LCTL
        # But in QK_MODS encoding, bits are: bit4=R, bit3=GUI, bit2=ALT, bit1=SFT, bit0=CTL
        # Actually QK_MODS stores mods in bits 12:8 as the OR of QK_L/R flags
        # QK_LCTL=0x01, QK_LSFT=0x02, QK_LALT=0x04, QK_LGUI=0x08, QK_RCTL=0x11, etc.
        # The mods byte extracted as (value >> 8) & 0x1F gives us the 5-bit packed form
        mod_combo_to_name = {
            0x01: "LCTL", 0x02: "LSFT", 0x04: "LALT", 0x08: "LGUI",
            0x11: "RCTL", 0x12: "RSFT", 0x14: "RALT", 0x18: "RGUI",
            0x0F: "HYPR", 0x07: "MEH",
            0x0D: "LCAG", 0x0A: "LSG", 0x0C: "LAG",
            0x05: "LCA", 0x06: "LSA",
            0x1A: "RSG", 0x1C: "RAG", 0x16: "RSA", 0x13: "RCS",
        }
        return mod_combo_to_name.get(mods_8bit)

    def _mod5_to_name(self, mod5):
        """Convert 5-bit packed modifier to name string."""
        # Simple single-mod cases
        simple = {
            0x01: "MOD_LCTL", 0x02: "MOD_LSFT", 0x04: "MOD_LALT", 0x08: "MOD_LGUI",
            0x11: "MOD_RCTL", 0x12: "MOD_RSFT", 0x14: "MOD_RALT", 0x18: "MOD_RGUI",
        }
        if mod5 in simple:
            return simple[mod5]
        # Composite: build with | operator
        parts = []
        for bit, name in simple.items():
            if (mod5 & bit) == bit:
                parts.append(name)
                mod5 &= ~bit
                if mod5 == 0:
                    break
        if parts:
            return " | ".join(parts)
        return f"0x{mod5:02x}"

    def _mod_to_modtap_name(self, mod5):
        """Find the shortest mod-tap shortcut name for a 5-bit mod value."""
        # Reverse lookup from _modtap_macros
        # Prefer short names: LCTL_T over CTL_T
        for name, mod_val in self._modtap_macros.items():
            if mod_val == mod5:
                return name
        return None
```

**Step 2: Verify reverse display works**

```bash
python3 -c "
import sys; sys.path.insert(0, '/home/user/qmk/qmk-tools/qmk/QMKata')
from QMKataKeycodes import KeycodeResolver
r = KeycodeResolver('/home/user/qmk/keychron_qmk_firmware')
tests = [
    (0x0004, 'KC_A'),
    (0x0000, 'KC_NO'),
    (0x0104, 'LCTL(KC_A)'),       # QK_LCTL | KC_A
    (0x021E, 'LSFT(KC_1)'),       # QK_LSFT | KC_1 = S(KC_1)
    (0x5201, 'TO(1)'),
    (0x5222, 'MO(2)'),
    (0x4100 | 0x2C, 'LT(1, KC_SPACE)'),
    (0x2000 | (0x01 << 8) | 0x04, 'LCTL_T(KC_A)'),
    (0x52A2, 'OSM(MOD_LSFT)'),
]
for value, expected_contains in tests:
    result = r.value_to_display(value)
    print(f'  0x{value:04x} -> {result}')
"
```

Expected: human-readable names for all values.

**Step 3: Commit**

```
feat: add reverse display for keycode values to QMK names
```

---

### Task 5: Integrate into ComboConfigTab and MainWindow

**Files:**
- Modify: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKata.py`

**Step 1: Add --firmware-path CLI argument**

At the bottom of QMKata.py, modify the argparse section:

```python
parser.add_argument(
    "--firmware-path", required=False, type=str,
    help="path to QMK firmware root (enables keycode name resolution)"
)
```

And pass it to main:

```python
main((args.vid, args.pid), firmware_path=args.firmware_path)
```

**Step 2: Modify main() to accept firmware_path**

```python
def main(keyboard_vid_pid, firmware_path=None):
```

Pass `firmware_path` to `MainWindow`:

```python
main_window = MainWindow(keyboard_vid_pid, firmware_path=firmware_path)
```

**Step 3: Modify MainWindow to create KeycodeResolver**

Add import at top of file:

```python
from QMKataKeycodes import KeycodeResolver
```

Modify `MainWindow.__init__`:

```python
def __init__(self, keyboard_vid_pid, firmware_path=None):
    self.keyboard_vid_pid = keyboard_vid_pid
    self.firmware_path = firmware_path
    super().__init__()
    self.init_gui()
```

In `init_gui`, create resolver and pass to ComboConfigTab:

```python
resolver = KeycodeResolver(firmware_path) if firmware_path else KeycodeResolver()
self.combo_config_tab = ComboConfigTab(self.keyboard.keyboardModel, resolver)
```

**Step 4: Modify ComboConfigTab to use resolver**

Update constructor:

```python
def __init__(self, keyboard_model, resolver=None):
    self.dbg = DebugTracer(zones={"D": 0}, obj=self)
    self.keyboard_model = keyboard_model
    self.resolver = resolver or KeycodeResolver()
    super().__init__()
    self.init_gui()
```

Update `update_slot` to use `resolver.value_to_display()`:

```python
def update_slot(self, data):
    if not data:
        return
    slot = data["slot"]
    if slot < 16:
        widget = self.slot_widgets[slot]
        if self.resolver:
            keys_str = ", ".join([
                self.resolver.value_to_display(k) for k in data["keys"] if k != 0
            ])
            widget.keys_input.setText(keys_str)
            widget.code_input.setText(self.resolver.value_to_display(data["keycode"]))
        else:
            keys_str = ", ".join([f"0x{k:04x}" for k in data["keys"] if k != 0])
            widget.keys_input.setText(keys_str)
            widget.code_input.setText(f"0x{data['keycode']:04x}")
```

Update `save_combo` to use `resolver.resolve()`:

```python
def save_combo(self, slot):
    widget = self.slot_widgets[slot]
    try:
        keys_str = widget.keys_input.text()
        if self.resolver:
            keys = [self.resolver.resolve(k.strip()) for k in keys_str.split(",") if k.strip()]
            keycode = self.resolver.resolve(widget.code_input.text().strip())
        else:
            keys = [int(k.strip(), 0) for k in keys_str.split(",") if k.strip()]
            keycode = int(widget.code_input.text().strip(), 0)

        self.signal_keyb_set_combo.emit(slot, keys, keycode)
    except Exception as e:
        self.dbg.tr("E", "Failed to parse combo input: {}", e)
```

**Step 5: Verify syntax**

```bash
python3 -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKata.py
python3 -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeycodes.py
```

Expected: no errors.

**Step 6: Commit**

```
feat: integrate keycode name resolution into combo config UI
```

---

### Task 6: End-to-end verification

**Step 1: Run full resolver test suite**

```bash
python3 -c "
import sys; sys.path.insert(0, '/home/user/qmk/qmk-tools/qmk/QMKata')
from QMKataKeycodes import KeycodeResolver
r = KeycodeResolver('/home/user/qmk/keychron_qmk_firmware')

# Test round-trip: resolve then display
exprs = [
    'KC_A', 'KC_ENT', 'KC_LCTL', 'KC_F1', 'KC_VOLU',
    'LCTL(KC_A)', 'S(KC_1)', 'HYPR(KC_X)',
    'TO(1)', 'MO(2)', 'TG(3)',
    'LT(1, KC_SPC)', 'MT(MOD_LCTL, KC_A)', 'LCTL_T(KC_A)',
    'OSM(MOD_LSFT)', 'LM(1, MOD_LCTL)',
]
print('Round-trip tests:')
for expr in exprs:
    val = r.resolve(expr)
    display = r.value_to_display(val)
    print(f'  {expr:30s} -> 0x{val:04x} -> {display}')
print()

# Test mixed input parsing (simulating save_combo)
mixed = 'KC_A, 0x05, KC_LSFT'
keys = [r.resolve(k.strip()) for k in mixed.split(',')]
print(f'Mixed: {mixed} -> {[hex(k) for k in keys]}')
# Display back
display = ', '.join([r.value_to_display(k) for k in keys])
print(f'Display: {display}')
"
```

Expected: all round-trips produce readable output, mixed input parses correctly.

**Step 2: Verify QMKata.py syntax check passes**

```bash
python3 -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKata.py
python3 -m py_compile /home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeycodes.py
```

**Step 3: Commit if any final fixes were needed**

---

## Execution Notes

- **QMKataKeyboard.py requires NO changes** — it deals in raw uint16 values
- The `KeycodeResolver` gracefully degrades: if no firmware_path, it works in hex-only mode
- The comma-splitting in `save_combo` may need special handling for expressions containing commas (e.g. `LT(1, KC_SPC)`). However, the trigger keys field should only contain simple key names (combo triggers are basic keycodes), so this is only relevant for the result field which is a single expression.
- The result field should NOT be split by comma — it's always a single keycode expression.
