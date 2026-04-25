# VIA Dynamic Macro Defaults — Plan

## Goal

Ship a set of default VIA dynamic macros with the firmware so the
keyboard has useful macros out of the box, without requiring the user
to program them via VIA first. Defaults are restored on EEPROM reset
and whenever VIA issues `id_dynamic_keymap_macro_reset`. Users can still
edit macros via VIA at runtime; edits persist in EEPROM until an
explicit reset.

## Problem / current state

- QMK's `dynamic_keymap_macro_reset()` (`quantum/dynamic_keymap.c:134`)
  simply wipes the macro EEPROM region to zeros. There is no hook to
  install factory-default macros.
- VIA reads / writes the macro buffer via
  `dynamic_keymap_macro_{get,set}_buffer()`. After a fresh flash or
  bootmagic EEPROM reset, the buffer is all-zero, i.e. 16 empty macros.
- Keychron's `eeconfig_init_kb_datablock()` already fans out to
  feature-specific `_reset_defaults()` calls for combo, tap_dance, and
  leader. Macros are the only VIA-surface feature without defaults.

## Design

### Wire format recap (QMK VIA macro buffer)

The macro region is a single byte buffer split into
`DYNAMIC_KEYMAP_MACRO_COUNT` NUL-terminated entries:

```
[macro0_bytes...] 0x00 [macro1_bytes...] 0x00 ... [macroN_bytes...] 0x00
```

Within a macro body, bytes are interpreted by `send_string`:
- Literal ASCII byte → typed as that character
- `0x01 KC_XX` → tap keycode (press+release)
- `0x02 KC_XX` → press (hold down) keycode
- `0x03 KC_XX` → release keycode
- `0x04 'd','i','g','i','t','s','|'` → delay in milliseconds (ASCII digits, pipe-terminated)
- `0x00` → end of this macro slot; the Nth `0x00` marks the start of
  macro N+1

Empty slots are a single `0x00` byte. The final macro is followed by a
trailing `0x00`. VIA counts NULs to find slot boundaries.

### Defaults source

A const byte array in firmware flash, declared in a new file:

```
keyboards/keychron/common/dynamic_macro/
    macro_defaults.c
    macro_defaults.h
    macro_defaults.mk
```

The array uses `send_string_keycodes.h` macros (`SS_TAP`, `SS_DOWN`,
`SS_UP`, `SS_DELAY`) or their raw byte equivalents. Example shape:

```c
static const uint8_t default_macro_buffer[] = {
    // macro 0: literal text
    'h','e','l','l','o', 0x00,
    // macro 1: Ctrl+Shift+V paste-as-plain
    0x02, KC_LCTL, 0x02, KC_LSFT, 0x01, KC_V,
    0x03, KC_LSFT, 0x03, KC_LCTL, 0x00,
    // remaining slots: empty
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
```

The buffer size must be `<= DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` or the
write will be truncated silently. Build-time assert:

```c
_Static_assert(sizeof(default_macro_buffer) <= DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE,
               "default macro buffer exceeds macro EEPROM region");
```

### Install path

New function:

```c
void dynamic_macro_reset_defaults(void) {
    /* Write defaults into the macro EEPROM region. Called from
       eeconfig_init_kb_datablock() on EEPROM reset (first boot,
       version magic mismatch, bootmagic reset, VIA "Reset EEPROM"). */
    dynamic_keymap_macro_set_buffer(
        0,
        sizeof(default_macro_buffer),
        (uint8_t*)default_macro_buffer);
}
```

Wire into `keyboards/keychron/common/eeconfig_kb.c` alongside the
existing feature resets, gated on a new config flag:

```c
#if defined(DYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE)
    extern void dynamic_macro_reset_defaults(void);
    dynamic_macro_reset_defaults();
#endif
```

VIA's `id_dynamic_keymap_macro_reset` (0x10) currently calls
`dynamic_keymap_macro_reset()` directly (`quantum/via.c:441-442`),
which zeroes the buffer *without* re-invoking the kb datablock init.
Two ways to address this:

1. **Hook the VIA reset path** via `via_custom_value_command_kb()` or
   by overriding `dynamic_keymap_macro_reset()` with a `__weak` wrapper
   in Keychron common code that calls the upstream function then
   re-applies defaults. This preserves upstream semantics.
2. **Document the limitation**: VIA "Reset macros" button leaves the
   buffer empty; user must use EEPROM reset or bootmagic to get
   defaults back. Simpler, no code change to VIA path.

Recommendation: option 2 for the first iteration (simpler, no
upstream-dependent patching). Revisit if users complain.

### Makefile integration

`keyboards/keychron/common/dynamic_macro/macro_defaults.mk`:

```make
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE
SRC += $(KEYBOARD_COMMON_DIR)/dynamic_macro/macro_defaults.c
VPATH += $(KEYBOARD_COMMON_DIR)/dynamic_macro
```

Include from per-keyboard `rules.mk` when defaults are desired:

```make
# keyboards/keychron/q3_max/rules.mk
include keyboards/keychron/common/dynamic_macro/macro_defaults.mk
```

### Per-keyboard customization

If different keyboards need different default macro sets, make the
`default_macro_buffer` array `__weak` and let a keyboard-level file
override it. First iteration ships one default set in common/; specific
keyboards can override later.

## Configuration surface

| Flag | Effect |
|---|---|
| `DYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE` | Compile + wire defaults into eeconfig reset |
| `DYNAMIC_KEYMAP_MACRO_COUNT` | Existing QMK flag; defaults to 16. Array must have this many NUL-terminated entries |
| `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` | Existing QMK flag; upper bound on array size (asserted at build time) |

## Testing

### Build-time
- `_Static_assert` on buffer size.
- Compile with and without `DYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE` to
  confirm the feature is correctly gated.

### Manual integration
1. Flash firmware with defaults enabled.
2. Hold bootmagic keys on boot (or use VIA "Reset EEPROM").
3. Open VIA → Macros tab. Verify default macros appear in slots 0..N.
4. Edit macro 0 in VIA to something different.
5. Power-cycle. Verify edited macro 0 persists (not overwritten by
   defaults).
6. Reset EEPROM again. Verify defaults are restored.
7. Test macro execution: bind a key to `MACRO0`, press it, verify the
   default text / keysequence is sent.

### Test on wireless + wired paths
- Defaults apply regardless of connection mode (macro EEPROM region is
  mode-agnostic), but verify on both USB and wireless to catch any
  mode-specific init ordering issues.

## Open questions

1. **Which default macros to ship**? Needs content decision. Candidates:
   - Text: `"hello"` — trivial smoke test for literal mode.
   - Key combos: Ctrl+Shift+V (paste as plain text), Cmd+Shift+4 (Mac
     screenshot), Alt+Tab. Platform-agnostic combos that work on
     macOS/Windows are safest for a macOS/Windows dual-mode keyboard.
   - Start with 1-2 meaningful defaults + 14 empty slots. User fills
     the rest.
2. **Do we need wipe-before-write?** `dynamic_keymap_macro_set_buffer()`
   only overwrites the requested range. If defaults are shorter than
   the macro EEPROM region, leftover bytes from a previous state could
   leak in. Either (a) zero the whole region first via
   `nvm_dynamic_keymap_macro_reset()` then write defaults, or (b) pad
   the defaults array with trailing zeros to fill the region. Option
   (a) is cleaner and matches the sequence used elsewhere (reset ⇒
   populate).
3. **Mac/Win variants**: keyboards with a mode switch (Q3 Max does)
   could ship different defaults per mode. Out of scope for v1 — same
   defaults regardless of mode. If desired later, hook into
   `default_layer_state_set_kb()` or similar to swap macros on mode
   change, but that complicates persistence (user edits would be
   overwritten).
4. **Backward compatibility**: existing users upgrading to a build with
   defaults enabled will NOT get the defaults until they reset EEPROM
   (their EEPROM version magic matches, so `eeconfig_init_kb_datablock`
   does not run). This is expected — we don't want to trample their
   recorded macros. Document in release notes.

## Delivery order

1. Create `keyboards/keychron/common/dynamic_macro/` directory,
   `macro_defaults.{c,h,mk}` skeleton with an empty default buffer.
2. Wire into `eeconfig_init_kb_datablock()` under
   `DYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE`.
3. Opt in from `keyboards/keychron/q3_max/rules.mk`.
4. Decide and populate actual default macros (blocked on question 1).
5. Add build-time assert.
6. Consider the wipe-before-write concern (question 2).
7. Manual test per the test plan above.

## Files touched

| File | Change |
|---|---|
| `keyboards/keychron/common/dynamic_macro/macro_defaults.c` (new) | `default_macro_buffer[]` + `dynamic_macro_reset_defaults()` |
| `keyboards/keychron/common/dynamic_macro/macro_defaults.h` (new) | Function prototype |
| `keyboards/keychron/common/dynamic_macro/macro_defaults.mk` (new) | SRC + `OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_DEFAULTS_ENABLE` |
| `keyboards/keychron/common/eeconfig_kb.c` | Call `dynamic_macro_reset_defaults()` on reset |
| `keyboards/keychron/q3_max/rules.mk` | Include the new `.mk` |

No firmware-side struct format changes. No host-side changes. Purely
additive: feature is off by default, opt-in via rules.mk.
