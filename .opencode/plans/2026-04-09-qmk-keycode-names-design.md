# QMK Keycode Name Support for Combo Config

**Date**: 2026-04-09
**Status**: Approved

## Goal

Allow the combo config UI to accept QMK keycode names (KC_A, LCTL(KC_B), LT(1, KC_SPC), etc.) in addition to raw hex values, and display stored keycodes as human-readable names.

## Scope

- All named keycodes from QMK HJSON data files (~750 primary + ~577 aliases)
- Modifier wrapper macros: LCTL(), LSFT(), LALT(), LGUI(), RCTL(), RSFT(), RALT(), RGUI(), HYPR(), MEH(), LCAG(), LSG(), LAG(), RSG(), RAG(), LCA(), LSA(), RSA(), RCS(), C(), S(), A(), G()
- Layer macros: TO(), MO(), DF(), TG(), OSL(), TT()
- Layer-Tap: LT(layer, kc)
- Mod-Tap: MT(mod, kc), LCTL_T(), LSFT_T(), LALT_T(), LGUI_T(), etc.
- Layer-Mod: LM(layer, mod)
- One-shot: OSM(mod)
- MOD_* constants as parameters: MOD_LCTL, MOD_LSFT, MOD_LALT, MOD_LGUI, MOD_RCTL, MOD_RSFT, MOD_RALT, MOD_RGUI
- Raw hex (0x0004) and decimal integers still accepted
- Mixed input: "KC_A, 0x05" is valid

## Architecture

### New file: QMKataKeycodes.py

Class `KeycodeResolver`:

```
__init__(firmware_path: str)
```
- Loads all keycodes_*.hjson from firmware_path/data/constants/keycodes/
- Versions layered: 0.0.1 base, 0.0.2 and 0.0.3 merge on top
- Builds name_to_value dict (all names + aliases -> uint16)
- Builds value_to_name dict (uint16 -> canonical name, for display)
- Registers MOD_* constants and QK_* range bases

```
resolve(expr: str) -> int
```
- "0x04" -> int("0x04", 16)
- "KC_A" -> name_to_value["KC_A"]
- "LCTL(KC_A)" -> parse macro, recurse on args, compute: QK_LCTL | resolve("KC_A")
- "LT(1, KC_SPC)" -> QK_LAYER_TAP | ((1 & 0xF) << 8) | (resolve("KC_SPC") & 0xFF)
- Raises ValueError on unknown names

```
value_to_display(value: int) -> str
```
- Direct lookup in value_to_name if exists
- Range decomposition for QK_MODS, QK_LAYER_TAP, QK_MOD_TAP, QK_TO/MO/DF/TG/OSL/TT/OSM, QK_LAYER_MOD
- Fallback: "0x{:04x}".format(value)

### Data source

HJSON files in: `<firmware_path>/data/constants/keycodes/keycodes_*.hjson`

Each file contains:
```json
{
    "keycodes": {
        "0x0004": {
            "group": "basic",
            "key": "KC_A",
            "label": "A",
            "aliases": ["optional"]
        }
    }
}
```

Version layering: load 0.0.1 files first, then 0.0.2 and 0.0.3 files merge/override entries with same hex key.

### Macro evaluation rules

| Macro | Formula |
|---|---|
| LCTL(kc) | QK_LCTL(0x0100) \| resolve(kc) |
| LSFT(kc) | QK_LSFT(0x0200) \| resolve(kc) |
| LALT(kc) | QK_LALT(0x0400) \| resolve(kc) |
| LGUI(kc) | QK_LGUI(0x0800) \| resolve(kc) |
| RCTL(kc) | QK_RCTL(0x1100) \| resolve(kc) |
| RSFT(kc) | QK_RSFT(0x1200) \| resolve(kc) |
| RALT(kc) | QK_RALT(0x1400) \| resolve(kc) |
| RGUI(kc) | QK_RGUI(0x1800) \| resolve(kc) |
| HYPR(kc) | 0x0F00 \| resolve(kc) |
| MEH(kc) | 0x0700 \| resolve(kc) |
| C(kc), S(kc), A(kc), G(kc) | aliases for LCTL/LSFT/LALT/LGUI |
| TO(layer) | QK_TO(0x5200) \| (layer & 0x1F) |
| MO(layer) | QK_MOMENTARY(0x5210) \| (layer & 0x1F) |
| DF(layer) | QK_DEF_LAYER(0x5220) \| (layer & 0x1F) |
| TG(layer) | QK_TOGGLE_LAYER(0x5230) \| (layer & 0x1F) |
| OSL(layer) | QK_ONE_SHOT_LAYER(0x5240) \| (layer & 0x1F) |
| TT(layer) | QK_LAYER_TAP_TOGGLE(0x5280) \| (layer & 0x1F) |
| OSM(mod) | QK_ONE_SHOT_MOD(0x5250) \| (mod & 0x1F) |
| LM(layer, mod) | QK_LAYER_MOD(0x5100) \| ((layer & 0xF) << 5) \| (mod & 0x1F) |
| LT(layer, kc) | QK_LAYER_TAP(0x4000) \| ((layer & 0xF) << 8) \| (resolve(kc) & 0xFF) |
| MT(mod, kc) | QK_MOD_TAP(0x2000) \| ((mod & 0x1F) << 8) \| (resolve(kc) & 0xFF) |
| LCTL_T(kc) | MT(MOD_LCTL, kc) |
| (other _T variants) | MT(corresponding MOD_*, kc) |

### Modified: QMKata.py

- Add `--firmware-path` CLI argument
- MainWindow creates KeycodeResolver(firmware_path) if path provided
- Pass resolver to ComboConfigTab constructor
- ComboConfigTab.save_combo: use resolver.resolve() on each token
- ComboConfigTab.update_slot: use resolver.value_to_display() for display
- Graceful fallback: if no firmware path given or HJSON load fails, resolver operates in hex-only mode

### Dependencies

- `hjson` pip package (for parsing HJSON data files)

### Data flow

```
User types "KC_A, KC_B" + "LCTL(KC_C)"
  -> save_combo splits keys by comma, calls resolver.resolve() on each
  -> gets [0x0004, 0x0005] and 0x0106
  -> signal_keyb_set_combo.emit(slot, [4, 5], 262)
  -> keyboard sends raw uint16 over sysex (unchanged)

Keyboard returns {keys: [4, 5, 0, ...], keycode: 262}
  -> update_slot calls resolver.value_to_display() on each
  -> displays "KC_A, KC_B" and "LCTL(KC_C)"
```

### No changes to QMKataKeyboard.py

The keyboard communication layer deals in raw uint16 values. The name<->value translation sits entirely in the UI layer.

### Error handling

- Unknown keycode name: ValueError with descriptive message, shown in UI (debug trace + field highlight or status message)
- HJSON files not found: log warning, resolver operates in hex-only mode
- Malformed macro expression (unbalanced parens, wrong arg count): ValueError
