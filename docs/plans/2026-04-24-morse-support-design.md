# Morse Code Support for Q3 Max — Design

**Date:** 2026-04-24
**Status:** Design approved, ready for implementation planning
**Scope:** Q3 Max keyboard (ANSI + ISO encoder variants)

## Summary

Add on-keyboard morse code support to the Q3 Max, comprising three capabilities that share a single small subsystem:

1. **Input mode** — manual dual-paddle keying (`MC_DIT` / `MC_DAH`) decodes into typed characters sent to the host.
2. **Training mode** — host pushes a prompt string into keyboard SRAM; user taps the morse pattern for each character; firmware validates and gives per-character RGB feedback.
3. **Paddle-echoed RGB output** — every paddle press produces a full-matrix white flash whose duration encodes dit vs. dah. This is the "morse as output" visual and runs in both modes.

The feature integrates with the existing dynamic-feature pattern in this fork (QMKata SysEx for host-side config), but stores all config in **SRAM only** — no EEPROM wear, no persistence across power cycles.

## Non-Goals (explicit YAGNI)

- No iambic keyer; no straight-key mode. Manual dual paddle only.
- No EEPROM persistence for any morse state or config.
- No status-driven blinking (layer switches, BT host, battery, etc.).
- No host-triggered standalone "blink this string" playback — blinks are paddle-echo only.
- No audio output (hardware has no piezo).
- No multi-prompt slots — one active SRAM prompt buffer.

## Keycodes (5 total)

| Keycode          | Behavior                                                                                               |
| ---------------- | ------------------------------------------------------------------------------------------------------ |
| `MC_DIT`         | Paddle — left/dit. No-op when no mode active.                                                          |
| `MC_DAH`         | Paddle — right/dah. No-op when no mode active.                                                         |
| `MC_INPUT_TOGG`  | Toggle input mode. Enabling disables training mode.                                                    |
| `MC_TRAIN_TOGG`  | Toggle training mode. Enabling disables input mode. Refuses to enable if no prompt loaded (red x2).   |
| (no commit key)  | Idle timeout handles character commit.                                                                 |

Modes are mutually exclusive; paddles are no-ops when `MORSE_OFF`.

## Paddle Mechanics

- **Style:** manual dual paddle. Each press of `MC_DIT` / `MC_DAH` = exactly one element. No auto-repeat, no squeeze logic.
- **Character commit (input mode):** after `letter_timeout_ms` (default 500) of no paddle activity with a non-empty element buffer, decode and commit.
- **Word space (input mode only):** after a committed letter, if `word_timeout_ms` (default 1200) of further idle elapses without new paddle activity, emit `KC_SPC`. Armed only once per commit.
- **Character commit (training mode, hybrid):**
  - **Fast path:** on each paddle press, if `element_count` matches the expected pattern length, evaluate immediately. Correct → green flash, advance prompt. (Incorrect-at-length is *not* committed immediately — it falls through to the timeout path so the user has a chance to add more elements.)
  - **Timeout path:** on `letter_timeout_ms` idle with a non-empty buffer, evaluate. Match → green; mismatch or wrong-length → red. Advance prompt either way.
- **Element buffer cap:** 8 elements. Excess paddle presses while full are dropped silently; the resulting oversize commit will decode to nothing (input) / mismatch (training).

## RGB Feedback

Uses the existing `g_rgb_matrix_host_buf` mechanism already in `q3_max_user.c:136-152` — no new RGB plumbing.

| Event                 | Effect                                                  |
| --------------------- | ------------------------------------------------------- |
| `MC_DIT` pressed      | Full-matrix white flash, duration `dit_flash_ms` (80)   |
| `MC_DAH` pressed      | Full-matrix white flash, duration `dah_flash_ms` (240)  |
| Training: correct     | Full-matrix green flash (short, ~150 ms)                |
| Training: wrong       | Full-matrix red flash (short, ~150 ms)                  |
| Training: empty prompt on toggle | Two short red blips                           |

Flash durations are ms values converted to host-buffer frame counts in `morse_rgb_flash()`.

## Configuration

All config lives in a file-static `morse_cfg_t` in SRAM. Compile-time defaults populated in `morse_init()`; host may overwrite via QMKata SysEx. Host is expected to re-push config after each power cycle if non-default values are desired.

```c
typedef struct {
    uint16_t letter_timeout_ms;   // default 500
    uint16_t word_timeout_ms;     // default 1200
    uint16_t dit_flash_ms;        // default 80
    uint16_t dah_flash_ms;        // default 240
    char     prompt_buf[128];     // host-pushed training prompt
    uint16_t prompt_buf_len;      // 0 = unset
} morse_cfg_t;
```

**Host API (new QMKata SysEx handlers):**

| ID                   | Direction | Payload                                        |
| -------------------- | --------- | ---------------------------------------------- |
| `MORSE_SET_PROMPT`   | host→kb   | `uint8_t len; char data[len]` (≤128)          |
| `MORSE_SET_TIMING`   | host→kb   | `u16 letter_ms; u16 word_ms`                  |
| `MORSE_SET_FLASH_MS` | host→kb   | `u16 dit_ms; u16 dah_ms`                      |
| `MORSE_GET_STATE`    | host↔kb   | req: empty; resp: `{mode, prompt_pos, element_count, prompt_len}` |

Mode changes are NOT host-controllable. Mode is owned by keymap keycodes.

## Architecture & File Layout

```
quantum/morse/
  morse.h              — public API, keycode enum, mode enum
  morse.c              — state machine, decoder, timeout logic, init/task
  morse_table.h        — static const dit/dah → char lookup (A-Z, 0-9, basic punct)
  morse_rgb.c          — full-matrix flash driver (wraps g_rgb_matrix_host_buf writes)
  morse_sram.h         — morse_cfg_t / morse_state_t struct definitions
  morse.mk             — MORSE_ENABLE gate, SRC additions, -DMORSE_ENABLE

keyboards/keychron/qmkata/
  qmkata_morse_handler.c — new SysEx handler file (follows existing handler pattern)
```

**Integration touch-points (existing files):**

- `keyboards/keychron/q3_max/rules.mk` — add `MORSE_ENABLE = yes`, `include quantum/morse/morse.mk`
- `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c` and `iso_encoder/keymaps/keychron/keymap.c` — `process_record_user` gains a `process_record_morse(keycode, record)` call before `process_record_keychron_common`
- `keyboards/keychron/q3_max/q3_max_user.c`:
  - `keyboard_post_init_user` → add `morse_init()` call (under `MORSE_ENABLE` guard)
  - `keychron_task_user` → add `morse_task()` call
- `keyboards/keychron/qmkata/qmkata_sysex_handler.c` (or wherever handlers register) — register morse handlers

All morse code is wrapped in `#ifdef MORSE_ENABLE`. RGB feedback wrapped in `#if defined(MORSE_ENABLE) && defined(RGB_MATRIX_ENABLE)`.

**Build choice:** built-in (not a loadable module). Code+table estimated <4 KB; simpler than module overhead for this feature.

## State

```c
typedef enum { MORSE_OFF, MORSE_INPUT, MORSE_TRAIN } morse_mode_t;

typedef struct {
    morse_mode_t mode;
    // Element buffer (current in-progress character)
    uint8_t  elements;            // bit-packed: bit N = 0 (dit) or 1 (dah)
    uint8_t  element_count;       // 0..8
    uint32_t last_element_time;   // timer_read32() of last paddle press
    bool     letter_committed;    // armed for word-space in input mode
    // Training cursor (valid when mode == MORSE_TRAIN)
    uint16_t prompt_pos;
} morse_state_t;
```

Both `morse_cfg_t` and `morse_state_t` are file-static singletons in `morse.c`.

## Data Flow

### Paddle press (both modes)

```
process_record_user(MC_DIT|MC_DAH, pressed=true)
 └─ process_record_morse(kc, rec)
      ├─ if state.mode == OFF: return true (consumed, no-op)
      ├─ if element_count < 8:
      │    elements |= (kind << element_count)
      │    element_count++
      ├─ last_element_time = timer_read32()
      ├─ letter_committed = false
      ├─ morse_rgb_flash_paddle(kind)   // white, dit_ms or dah_ms
      └─ if mode == TRAIN:
           if element_count == expected_len(prompt[prompt_pos])
              && elements == expected_pattern(prompt[prompt_pos]):
                morse_rgb_flash(GREEN, 150ms)
                prompt_pos = (prompt_pos + 1) % prompt_len
                element_count = 0
```

### Idle timeout (polled)

```
morse_task()  // called from keychron_task_user
 ├─ if mode == OFF: return
 ├─ elapsed = timer_elapsed32(last_element_time)
 ├─ // Letter commit
 ├─ if !letter_committed && element_count > 0 && elapsed >= letter_timeout_ms:
 │     commit_letter()
 │     letter_committed = true
 └─ // Word space (input mode only)
    if mode == INPUT && letter_committed && elapsed >= word_timeout_ms:
       tap_code(KC_SPC)
       letter_committed = false   // don't re-fire
```

### commit_letter() branches by mode

**Input:** `char c = morse_decode(elements, element_count)`; if valid, `tap_code16(ascii_to_keycode(c))`. Reset `element_count = 0`.

**Train:** compare to `expected_pattern(prompt[prompt_pos])`. Match → green flash. Else → red flash. Advance `prompt_pos`. Reset `element_count = 0`.

### Mode toggle

```
on MC_INPUT_TOGG press:
  old = state.mode
  state.mode = (old == INPUT) ? OFF : INPUT
  reset_letter_buffer()   // discard half-typed character
  letter_committed = false

on MC_TRAIN_TOGG press:
  if state.mode != TRAIN && cfg.prompt_buf_len == 0:
      morse_rgb_flash_blips(RED, 2)
      return
  state.mode = (state.mode == TRAIN) ? OFF : TRAIN
  state.prompt_pos = 0
  reset_letter_buffer()
  letter_committed = false
```

## Error Handling

| Condition                                        | Behavior                                             |
| ------------------------------------------------ | ---------------------------------------------------- |
| Paddle press, mode == OFF                        | Silent no-op, event consumed                         |
| Element buffer overflow (>8 elements)            | Drop excess silently; commit decodes to nothing / mismatch |
| Undecodable pattern in input mode                | Emit nothing; reset buffer                           |
| Host prompt > 128 bytes                          | Truncate to 128; ack with actual stored length       |
| Host empty prompt                                | Stored (len=0); `MC_TRAIN_TOGG` refuses with red x2  |
| Mode toggled mid-character                       | Discard buffer; clean state for new mode             |
| QMKata not enabled at compile time               | Config-push handlers #ifdef'd out; compile-time defaults only |

## Testing

### Automated (host-compiled unit tests)

Pure-C tests for decoder and matcher, no QMK dependency. New target `make test:morse` following QMK's existing test pattern. Lives in `tests/morse/`.

- `morse_decode(elements, count)` across A-Z, 0-9, basic punct, unknown patterns, empty, overflow
- `morse_elements_match(a, na, b, nb)` — equality + length
- `morse_expected_pattern(ascii)` round-trip against the table

### Manual hardware tests

Full script documented in the impl plan; covers at minimum:

1. `MORSE_ENABLE=yes` flash succeeds; existing features (leader/combo/tap-dance) unaffected.
2. Passive (no mode): paddles emit nothing, no flash.
3. Input mode: `.-` → typed `a`; white flashes; word-space after long idle.
4. Training: push prompt via QMKata CLI; correct tap → green; wrong → red; prompt advances.
5. Mode switching: toggles mutually exclusive; buffer resets cleanly.
6. Host config: timing and flash ms updates take effect live.
7. Safe-mode regression: hold DEL at boot still skips module init.

### What we don't test automatically

- Timeout/task behavior (requires timer mocks; trivial to eyeball manually).
- RGB rendering (infrastructure shared with other features, already exercised).
- QMKata SysEx transport (exercised manually via existing host CLI).

## Open Questions / Future Extensions

- **Iambic keyer mode** — could be added as a third paddle style behind a mode flag; deferred.
- **Training "lap complete" cue** — optional dim flash when prompt wraps; deferred (YAGNI).
- **EEPROM-persisted prompt** — if users want "sticky" prompts, revisit; current design explicitly avoids EEPROM cost.
- **Per-key flash instead of full matrix** — if users find full-matrix flashes too disruptive; easy to retrofit since the RGB sink is already abstracted.
