<!-- markdownlint-disable-file -->

# Task Research Notes: Declarative DSL for QMK Tap Dance, Leader Key, and Combos

## Research Executed

### File Analysis

- `data/schemas/keymap.jsonschema` (81 lines)
  - Existing schema supports: layers, encoders, macros (with tap/down/up/delay/beep actions), custom keycodes
  - **No support** for tap_dance, leader_key, or combos definitions
  - Macros already demonstrate the pattern: declarative JSON → generated C code in `process_record_user()`
  
- `lib/python/qmk/keymap.py` (745 lines)
  - `generate_c()` uses template substitution (`__PLACEHOLDER__` patterns) to compose `keymap.c`
  - `_generate_macros_function()` shows the established pattern: JSON macro definitions → C switch/case in `process_record_user()`
  - Template: `DEFAULT_KEYMAP_C` with placeholder slots for includes, keycodes, keymap, encoder_map, macros

- `data/mappings/info_config.hjson` (244 lines)
  - Already maps `combo.term` → `COMBO_TERM`, `leader_key.*` → `LEADER_*`, `tapping.*` → `TAPPING_*`
  - Only configuration parameters mapped, NOT behavior definitions
  - Established bidirectional JSON↔C mapping system

- `quantum/process_keycode/process_tap_dance.h`
  - 6 built-in action macros: `ACTION_TAP_DANCE_DOUBLE`, `_LAYER_MOVE`, `_LAYER_TOGGLE`, `_FN`, `_FN_ADVANCED`, `_FN_ADVANCED_WITH_RELEASE`
  - Custom tap dances need: `on_each_tap_fn_t`, `on_dance_finished_fn_t`, `on_dance_reset_fn_t`

- `quantum/process_keycode/process_combo.h`
  - `combo_t` struct: keys (PROGMEM uint16_t array), keycode (output), state (bitfield)
  - Macros: `COMBO(keys, keycode)`, `COMBO_ACTION(keys)`

- `quantum/leader.h` / `quantum/leader.c`
  - `leader_sequence_one_key()` through `leader_sequence_five_keys()` for matching
  - `leader_start_user()` / `leader_end_user()` callbacks

### External Research

- #fetch:https://zmk.dev/docs/keymaps/behaviors/tap-dance
  - ZMK uses Devicetree syntax: `compatible = "zmk,behavior-tap-dance"`, `bindings` array, `tapping-term-ms`
  - Behaviors compose: tap-dance can contain hold-taps, sticky keys, etc.
  - Each tap-dance is a named node under `behaviors {}`

- #fetch:https://zmk.dev/docs/keymaps/combos
  - ZMK combos: `compatible = "zmk,combos"`, `key-positions`, `timeout-ms`, `bindings`, `layers`, `slow-release`, `require-prior-idle-ms`
  - Reference keys by position index (0-based), not keycode
  - Each combo is a named node under `combos {}`

- ZMK has NO leader key support (404 on docs page)

### Code Generation Patterns in QMK

The existing `keymap.json → keymap.c` pipeline follows this pattern:

1. **Schema** (`data/schemas/keymap.jsonschema`) validates input
2. **Python generator** (`lib/python/qmk/keymap.py`) transforms JSON → C via template substitution
3. **Template** has `__PLACEHOLDER__` slots filled by generator functions
4. **Build system** (`builddefs/build_keyboard.mk`) invokes `qmk json2c` when keymap.json exists

### C Code That Must Be Generated

#### Tap Dance Target Output
```c
// Enum for tap dance indices
enum {
    TD_ESC_CAPS,
    TD_MEDIA,
};

// For simple tap dances:
tap_dance_action_t tap_dance_actions[] = {
    [TD_ESC_CAPS] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, KC_CAPS),
    [TD_MEDIA] = ACTION_TAP_DANCE_DOUBLE(KC_MPLY, KC_MNXT),
};

// For advanced tap dances (per-tap-count actions):
void td_media_finished(tap_dance_state_t *state, void *user_data) {
    switch (state->count) {
        case 1: tap_code16(KC_MPLY); break;
        case 2: tap_code16(KC_MNXT); break;
        case 3: tap_code16(KC_MPRV); break;
    }
}
void td_media_reset(tap_dance_state_t *state, void *user_data) {}

tap_dance_action_t tap_dance_actions[] = {
    [TD_MEDIA] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, td_media_finished, td_media_reset),
};
```

#### Combo Target Output
```c
const uint16_t PROGMEM combo_esc[] = {KC_Q, KC_W, COMBO_END};
const uint16_t PROGMEM combo_tab[] = {KC_A, KC_S, COMBO_END};

combo_t key_combos[] = {
    COMBO(combo_esc, KC_ESC),
    COMBO(combo_tab, KC_TAB),
};
```

#### Leader Key Target Output
```c
void leader_end_user(void) {
    if (leader_sequence_two_keys(KC_D, KC_D)) {
        SEND_STRING(SS_LCTL("a") SS_TAP(X_DELETE));
    }
    if (leader_sequence_three_keys(KC_D, KC_D, KC_S)) {
        SEND_STRING(SS_LCTL("s"));
    }
}
```

## Key Discoveries

### Project Structure

QMK already has a mature **JSON → C code generation** pipeline for keymaps, encoder maps, and macros. The build system natively supports `keymap.json` as a first-class alternative to `keymap.c`. The generation uses Python with template substitution patterns.

### Critical Gap

Combos, tap dances, and leader sequences are the **three remaining keymap-level behavior features** with NO declarative support. All three require hand-written C code with significant boilerplate that obscures intent.

### Design Constraints

1. **Must extend `keymap.json`** — not a separate file format. QMK's build system and tooling (VIA, QMK Configurator) already center on this format
2. **Must generate valid C** — the DSL is a compile-time abstraction, not a runtime interpreter
3. **Must cover 90%+ use cases declaratively** — power users can still use `keymap.c` for edge cases
4. **Must use QMK keycode strings** — same as keymaps/macros already do (e.g., `"KC_ESC"`, `"LCTL(KC_S)"`)
5. **Schema-validatable** — must extend `keymap.jsonschema`

### Shared Concepts Across All Three Features

| Concept | Tap Dance | Leader Key | Combos |
|---------|-----------|------------|--------|
| Trigger | Single key pressed N times | Sequence of keys after leader | Multiple keys pressed simultaneously |
| Timing | `tapping_term` (between taps) | `timeout` (total sequence time) | `term` (simultaneous window) |
| Output | Keycode, layer action, or macro | Keycode or macro string | Keycode or custom action |
| Per-entry config | Per-dance tapping term | Per-key timing option | Per-combo term, must_hold, must_tap, order |
| Referenced in keymap | `TD(index)` | `QK_LEADER` (single trigger) | Implicit (key positions) |

## Recommended Approach: Extend `keymap.json` with Three New Top-Level Properties

### Format: JSON (HJSON for human-authored files)

**Rationale**: JSON is the established format in QMK's data-driven system. The schema validation, Python tooling, bidirectional conversion (c2json/json2c), and build system integration all expect JSON. Using anything else (YAML, TOML, custom syntax) would require building an entirely new toolchain. HJSON (JSON with comments and trailing commas) is already used for data files and could be supported for human authoring comfort.

### DSL Specification

#### 1. Tap Dance (`"tap_dances"`)

```jsonc
{
    "tap_dances": {
        "TD_ESC_CAPS": {
            "tapping_term": 200,        // optional, overrides global TAPPING_TERM
            "bindings": [
                "KC_ESC",               // tap once → Escape
                "KC_CAPS"               // tap twice → Caps Lock
            ]
        },
        "TD_MEDIA": {
            "bindings": [
                "KC_MPLY",              // tap once → Play/Pause
                "KC_MNXT",              // tap twice → Next Track
                "KC_MPRV"              // tap three times → Previous Track
            ]
        },
        "TD_SHIFT_CAPS": {
            "bindings": [
                "KC_LSFT",             // tap once → Left Shift (will behave as modifier if held)
                "KC_CAPS"              // tap twice → Caps Lock
            ]
        },
        "TD_LAYER": {
            "bindings": [
                "KC_NO",                // tap once → nothing (or could be a keycode)
                {"layer_toggle": 2}     // tap twice → toggle layer 2
            ]
        },
        "TD_COMPLEX": {
            "bindings": [
                "KC_SPC",
                {"macro": "Hello World"},    // tap twice → type "Hello World"
                {"layer_move": 3}            // tap three times → move to layer 3
            ]
        }
    }
}
```

**Design decisions**:
- **Named entries** (not array indices) — the name becomes the enum value and is used as `TD(TD_ESC_CAPS)` in the keymap layers
- **`bindings` array** — index = tap count (0-based: first element = 1 tap). Mirrors ZMK's approach
- **String bindings** = simple keycodes (covers `ACTION_TAP_DANCE_DOUBLE` and N-tap patterns)
- **Object bindings** = special actions: `{"layer_toggle": N}`, `{"layer_move": N}`, `{"macro": "text"}`
- **`tapping_term`** = optional per-dance override (generates `ACTION_TAP_DANCE_FN_ADVANCED` with per-key timing)

**Generated C**: For simple 2-keycode dances → `ACTION_TAP_DANCE_DOUBLE()`. For 3+ bindings or mixed types → `ACTION_TAP_DANCE_FN_ADVANCED()` with generated `_finished`/`_reset` callbacks.

**Keymap reference**: Users write `"TD(TD_ESC_CAPS)"` in their layer arrays, same as today.

#### 2. Combos (`"combos"`)

```jsonc
{
    "combos": {
        "COMBO_ESC": {
            "key_positions": [0, 1],        // positional indices into the layout
            "bindings": "KC_ESC",           // output keycode
            "term": 50,                     // optional, per-combo timeout override
            "layers": [0, 1]                // optional, restrict to specific layers
        },
        "COMBO_TAB": {
            "key_positions": [10, 11],
            "bindings": "KC_TAB"
        },
        "COMBO_CTRL_Z": {
            "keys": ["KC_Z", "KC_X"],       // alternative: specify by keycode (not position)
            "bindings": "LCTL(KC_Z)"
        },
        "COMBO_ENTER": {
            "key_positions": [21, 22],
            "bindings": "KC_ENT",
            "must_hold": true,              // optional: must hold to activate
            "must_tap": false,              // optional: must tap (not hold) to activate
            "press_in_order": true          // optional: keys must be pressed in order
        },
        "COMBO_LEADER": {
            "keys": ["KC_J", "KC_K"],
            "bindings": "QK_LEADER"         // combo triggers leader mode!
        }
    }
}
```

**Design decisions**:
- **Two trigger modes**: `key_positions` (layout-index-based, like ZMK) OR `keys` (keycode-based, like current QMK). Both generate the same PROGMEM arrays, but `keys` is more readable and `key_positions` is more precise (handles duplicate keycodes)
- **`bindings`** = single keycode string (covers `COMBO()` macro). For complex actions, could use macro object like tap dance
- **Per-combo options** map directly to QMK's `COMBO_MUST_HOLD_PER_COMBO`, `COMBO_MUST_TAP_PER_COMBO`, `COMBO_MUST_PRESS_IN_ORDER_PER_COMBO`
- **`layers`** restriction generates `COMBO_SHOULD_TRIGGER` callback logic
- **`term`** override generates `COMBO_TERM_PER_COMBO` callback logic

**Generated C**: PROGMEM key arrays + `combo_t key_combos[]` + optional per-combo callback functions.

#### 3. Leader Sequences (`"leader_sequences"`)

```jsonc
{
    "leader_sequences": {
        "timeout": 300,                     // global leader timeout (also settable in config)
        "per_key_timing": true,             // reset timeout on each key press
        "sequences": {
            "DELETE_LINE": {
                "keys": ["KC_D", "KC_D"],
                "action": {"macro": [
                    {"action": "tap", "keycodes": ["LCTL", "A"]},
                    {"action": "tap", "keycodes": ["DELETE"]}
                ]}
            },
            "SAVE": {
                "keys": ["KC_S"],
                "action": "LCTL(KC_S)"          // simple keycode output
            },
            "SAVE_ALL": {
                "keys": ["KC_S", "KC_A"],
                "action": {"macro": "save-all"}  // SEND_STRING shorthand
            },
            "LAYER_GAMING": {
                "keys": ["KC_G", "KC_G"],
                "action": {"layer_toggle": 3}
            },
            "EMOJI_THUMBSUP": {
                "keys": ["KC_E", "KC_T"],
                "action": {"unicode": "👍"}      // future: unicode output
            }
        }
    }
}
```

**Design decisions**:
- **`sequences` object** — each named sequence has `keys` (the sequence to type after leader) and `action` (what happens)
- **`keys` array** — 1-5 keycodes matching `leader_sequence_one_key()` through `leader_sequence_five_keys()`
- **`action` types**:
  - String: simple keycode → `tap_code16(KC_xxx)`
  - `{"macro": "text"}`: SEND_STRING text
  - `{"macro": [...]}`: full macro actions (reuse same format as `keymap.json` macros)
  - `{"layer_toggle": N}` / `{"layer_move": N}`: layer actions
- **Global config** (`timeout`, `per_key_timing`) at top level of `leader_sequences` (also expressible in `info.json` — the DSL value takes precedence if both specified)

**Generated C**: `leader_end_user()` function with chained `if (leader_sequence_N_keys(...))` blocks.

### Complete Example: Full `keymap.json` with All Three Features

```jsonc
{
    "keyboard": "keychron/q3_max/ansi_encoder",
    "keymap": "my_custom",
    "layout": "LAYOUT_ansi_89",
    "layers": [
        ["KC_ESC", "KC_F1", "...", "TD(TD_MEDIA)", "..."],
        ["_______", "_______", "...", "_______", "..."]
    ],
    "macros": [
        ["Hello, World!"]
    ],
    "tap_dances": {
        "TD_MEDIA": {
            "bindings": ["KC_MPLY", "KC_MNXT", "KC_MPRV"]
        },
        "TD_ESC_CAPS": {
            "tapping_term": 175,
            "bindings": ["KC_ESC", "KC_CAPS"]
        }
    },
    "combos": {
        "COMBO_ESC": {
            "keys": ["KC_Q", "KC_W"],
            "bindings": "KC_ESC"
        },
        "COMBO_TAB": {
            "keys": ["KC_A", "KC_S"],
            "bindings": "KC_TAB",
            "layers": [0]
        }
    },
    "leader_sequences": {
        "sequences": {
            "DELETE_LINE": {
                "keys": ["KC_D", "KC_D"],
                "action": {"macro": [
                    {"action": "tap", "keycodes": ["LCTL", "A"]},
                    {"action": "tap", "keycodes": ["DELETE"]}
                ]}
            },
            "SAVE": {
                "keys": ["KC_S"],
                "action": "LCTL(KC_S)"
            }
        }
    }
}
```

### Generated `keymap.c` Additions

The generator would produce these additional blocks in the template:

```c
/* ===== TAP DANCE ===== */
enum tap_dance_codes {
    TD_MEDIA,
    TD_ESC_CAPS,
};

void td_TD_MEDIA_finished(tap_dance_state_t *state, void *user_data) {
    switch (state->count) {
        case 1: tap_code16(KC_MPLY); break;
        case 2: tap_code16(KC_MNXT); break;
        case 3: tap_code16(KC_MPRV); break;
    }
}
void td_TD_MEDIA_reset(tap_dance_state_t *state, void *user_data) {}

tap_dance_action_t tap_dance_actions[] = {
    [TD_MEDIA] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, td_TD_MEDIA_finished, td_TD_MEDIA_reset),
    [TD_ESC_CAPS] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, KC_CAPS),
};

/* ===== COMBOS ===== */
const uint16_t PROGMEM combo_COMBO_ESC_keys[] = {KC_Q, KC_W, COMBO_END};
const uint16_t PROGMEM combo_COMBO_TAB_keys[] = {KC_A, KC_S, COMBO_END};

combo_t key_combos[] = {
    [0] = COMBO(combo_COMBO_ESC_keys, KC_ESC),
    [1] = COMBO(combo_COMBO_TAB_keys, KC_TAB),
};

bool combo_should_trigger(uint16_t combo_index, combo_t *combo, uint16_t keycode, keyrecord_t *record) {
    switch (combo_index) {
        case 1: /* COMBO_TAB */
            if (get_highest_layer(layer_state) > 0) return false;
            break;
    }
    return true;
}

/* ===== LEADER SEQUENCES ===== */
void leader_end_user(void) {
    if (leader_sequence_two_keys(KC_D, KC_D)) {
        // DELETE_LINE
        SEND_STRING(SS_DOWN(X_LCTL) SS_TAP(X_A) SS_UP(X_LCTL) SS_TAP(X_DELETE));
    }
    if (leader_sequence_one_key(KC_S)) {
        // SAVE
        tap_code16(LCTL(KC_S));
    }
}
```

### Schema Extension

New properties added to `keymap.jsonschema`:

```jsonc
{
    "tap_dances": {
        "type": "object",
        "additionalProperties": {
            "type": "object",
            "required": ["bindings"],
            "properties": {
                "tapping_term": {"type": "integer", "minimum": 0},
                "bindings": {
                    "type": "array",
                    "minItems": 2,
                    "items": {
                        "oneOf": [
                            {"type": "string"},
                            {"type": "object", "properties": {
                                "layer_toggle": {"type": "integer"},
                                "layer_move": {"type": "integer"},
                                "macro": {}
                            }}
                        ]
                    }
                }
            }
        }
    },
    "combos": {
        "type": "object",
        "additionalProperties": {
            "type": "object",
            "required": ["bindings"],
            "properties": {
                "keys": {"type": "array", "items": {"type": "string"}, "minItems": 2},
                "key_positions": {"type": "array", "items": {"type": "integer"}, "minItems": 2},
                "bindings": {"type": "string"},
                "term": {"type": "integer", "minimum": 0},
                "layers": {"type": "array", "items": {"type": "integer"}},
                "must_hold": {"type": "boolean"},
                "must_tap": {"type": "boolean"},
                "press_in_order": {"type": "boolean"}
            }
        }
    },
    "leader_sequences": {
        "type": "object",
        "properties": {
            "timeout": {"type": "integer", "minimum": 0},
            "per_key_timing": {"type": "boolean"},
            "sequences": {
                "type": "object",
                "additionalProperties": {
                    "type": "object",
                    "required": ["keys", "action"],
                    "properties": {
                        "keys": {"type": "array", "items": {"type": "string"}, "minItems": 1, "maxItems": 5},
                        "action": {
                            "oneOf": [
                                {"type": "string"},
                                {"type": "object"}
                            ]
                        }
                    }
                }
            }
        }
    }
}
```

### Rules.mk Auto-Enable

The generator should automatically add feature flags to `rules.mk` when the corresponding section is non-empty:

| JSON property | rules.mk flag |
|---------------|---------------|
| `tap_dances` (non-empty) | `TAP_DANCE_ENABLE = yes` |
| `combos` (non-empty) | `COMBO_ENABLE = yes` |
| `leader_sequences` (non-empty) | `LEADER_ENABLE = yes` |

## Implementation Guidance

- **Objectives**: Enable declarative definition of tap dances, combos, and leader sequences in `keymap.json`, with automatic C code generation
- **Key Tasks**:
  1. Extend `data/schemas/keymap.jsonschema` with the three new properties
  2. Add generator functions to `lib/python/qmk/keymap.py`: `_generate_tap_dances()`, `_generate_combos()`, `_generate_leader_sequences()`
  3. Add placeholder slots to `DEFAULT_KEYMAP_C` template: `__TAP_DANCE_OUTPUT__`, `__COMBO_OUTPUT__`, `__LEADER_OUTPUT__`
  4. Update `generate_c()` to call new generators and fill template slots
  5. Update `data/mappings/info_rules.hjson` to auto-enable features based on JSON presence
  6. Add tests for each generator function
  7. Update `c2json.py` / `parse_keymap_c()` for round-trip support (optional, lower priority)
- **Dependencies**: Python 3.7+ (already required), existing keymap.py infrastructure
- **Success Criteria**: 
  - A `keymap.json` with all three feature sections generates a valid, compilable `keymap.c`
  - Generated firmware behavior is identical to hand-written equivalent
  - Schema validation catches malformed definitions
  - Existing `keymap.json` files without new sections continue to work unchanged (backward compatible)
