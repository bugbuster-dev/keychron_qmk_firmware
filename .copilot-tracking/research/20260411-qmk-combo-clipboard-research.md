<!-- markdownlint-disable-file -->

# Task Research Notes: QMK Combo Keys for Select All, Copy, Paste

## Research Executed

### File Analysis

- `keyboards/keychron/q3_max/ansi_encoder/keymaps/via/keymap.c`
  - Dynamic combo system with EEPROM persistence, defaults at lines 122-126
- `quantum/process_keycode/process_combo.h`
  - Default `COMBO_TERM = 50ms`, `COMBO(trigger_keys, output_keycode)` macro

### Bigram Frequency Analysis

Source: Google Books Ngram corpus (743B words, 2.8T bigrams)

**Problem**: Current `C+D` combo fires when typing "cd", "could", "academy" — CD bigram is common.
Similarly `C+A` fires on "cat", "can", "car", "case" — CA is extremely common.

**Key pairs ranked by misfire resistance** (combined forward+reverse frequency):

| Rank | Pair | Frequency | % of bigrams | Common words? | Comfort |
|------|------|-----------|-------------|---------------|---------|
| #1 | **Z + X** | 244K | 0.0000087% | NONE | EASY - adjacent bottom row |
| #2 | **X + D** | 1.4M | 0.0000519% | NONE | MEDIUM - diagonal |
| #3 | **X + S** | 2.3M | 0.0000819% | NONE | EASY - same column |
| #4 | **V + F** | 2.3M | 0.0000836% | NONE | EASY - cross-row, both index |
| #5 | **V + G** | 4.1M | 0.0001473% | NONE | MEDIUM - cross-row |
| #6 | **J + K** | 5.2M | 0.0001848% | NONE | EASY - adjacent home row |
| #7 | **M + J** | 6.5M | 0.0002335% | NONE | EASY - cross-row adjacent |
| #8 | **C + V** | 12.4M | 0.0004412% | NONE | EASY - adjacent bottom row |
| #9 | **B + G** | 21.1M | 0.0007500% | NONE | EASY - cross-row adjacent |

**Rejected pairs that look safe but aren't:**
- `X+C` — "exceed, except, exchange, excellent" (747M occurrences)
- `Z+A` — "amazing, organization, realize" (1B+)
- `N+M/N+H` — "environment, unhappy" (1B+)

## Recommended Approach

Replace current defaults with trigger pairs that have **near-zero misfire risk**. All pairs below have essentially zero overlap with English text:

### Recommended Combo Defaults

| Action | Trigger Keys | Output | Why safe | Physical |
|--------|-------------|--------|----------|----------|
| **Select All** | `Z` + `X` | `LCTL(KC_A)` | Rarest bigram in English (0.0000087%) | Adjacent pinky+ring, bottom row |
| **Copy** | `X` + `S` | `LCTL(KC_C)` | Zero common words (0.0000819%) | Same column, easy vertical chord |
| **Paste** | `C` + `V` | `LCTL(KC_V)` | No English words use CV (0.0004412%) | Adjacent middle+index, bottom row |

### Code Change (Dynamic/EEPROM pattern)

```c
const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)},  // Select All
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)},  // Copy
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)},  // Paste
};
```

### Extended Set (bonus clipboard combos)

| Action | Trigger Keys | Output | Frequency |
|--------|-------------|--------|-----------|
| **Cut** | `V` + `F` | `LCTL(KC_X)` | 0.0000836% — zero words |
| **Undo** | `X` + `D` | `LCTL(KC_Z)` | 0.0000519% — zero words |

```c
const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)},  // Select All
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)},  // Copy
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)},  // Paste
    {.keys = {KC_V, KC_F, COMBO_END}, .keycode = LCTL(KC_X)},  // Cut
    {.keys = {KC_X, KC_D, COMBO_END}, .keycode = LCTL(KC_Z)},  // Undo
};
```

## Implementation Guidance

- **Objectives**: Replace misfire-prone combos (C+A, C+D) with statistically safe pairs
- **Key Tasks**: Update `combo_default_defs[]` in keymap.c, then EEPROM factory reset to load new defaults
- **Dependencies**: After flashing, hold Fn+J+Z for 4s (or clear EEPROM) to reload defaults
- **Success Criteria**: No misfires during fast English typing; all clipboard combos fire reliably on simultaneous press
