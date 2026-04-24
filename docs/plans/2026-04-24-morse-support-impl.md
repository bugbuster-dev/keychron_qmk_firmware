# Morse Code Support for Q3 Max — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add on-keyboard morse code support to the Q3 Max: manual-paddle input mode (types decoded characters), training mode (validates user against a host-pushed prompt), and full-matrix RGB flash as a paddle echo in both modes. Config lives in SRAM only; keycodes drive mode changes.

**Architecture:** One new feature directory under the Keychron common tree (mirrors `keyboards/keychron/common/leader/`). Feature gated by `MORSE_ENABLE` in `rules.mk`. Hooks into `process_record_user` (paddle handling), `keyboard_post_init_user` (init), `keychron_task_user` (idle-timeout polling), and the existing `g_rgb_matrix_host_buf` consumer (full-matrix flashes). QMKata SysEx integration adds a new `QMKATA_ID_MORSE` handler for host-side config push (prompt, timings). A decoder table + pure-C pattern matcher are unit-tested on the host.

**Tech Stack:** QMK firmware (C99), ChibiOS, existing Keychron QMKata SysEx layer, Google Test (QMK's existing test framework) for host-side unit tests.

**Design doc:** `docs/plans/2026-04-24-morse-support-design.md`

---

## Preconditions for executing this plan

1. Working directory is a clean Q3 Max worktree on the feature branch.
2. `MORSE_ENABLE` is not yet defined anywhere — this plan introduces it.
3. ARM GCC toolchain is installed and `make keychron/q3_max/ansi_encoder:keychron` builds cleanly **before** you start (baseline).

**Verify baseline build before Task 1:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -5
```
Expected: ends with `The firmware size is fine …` and `Creating load file …`. If this fails, stop and fix the baseline first.

---

## Task 1: Create feature skeleton (header + empty module + mk include)

**Why first:** establish file layout and a compile target so subsequent tasks have somewhere to put code. No behavior yet — feature gated off.

**Files:**
- Create: `keyboards/keychron/common/morse/morse.h`
- Create: `keyboards/keychron/common/morse/morse.c`
- Create: `keyboards/keychron/common/morse/morse_table.h`
- Create: `keyboards/keychron/common/morse/morse.mk`
- Modify: `keyboards/keychron/q3_max/rules.mk` (add `MORSE_ENABLE = yes` and `include` line)

**Step 1.1 — Write `morse.h` (public API, empty bodies later):**

```c
// keyboards/keychron/common/morse/morse.h
#pragma once

#include "quantum.h"

#ifdef MORSE_ENABLE

typedef enum {
    MORSE_OFF = 0,
    MORSE_INPUT,
    MORSE_TRAIN,
} morse_mode_t;

// Custom keycodes — user defines MC_SAFE_RANGE = QK_USER (or similar) in keymap,
// and this header exposes the 4 morse keycodes relative to a user-provided base.
// To keep the feature self-contained, we define them via a QK_USER offset block
// that the keymap just references by name.
enum morse_keycodes {
    MC_DIT = QK_USER,
    MC_DAH,
    MC_INPUT_TOGG,
    MC_TRAIN_TOGG,
    MC_SAFE_RANGE,  // keymaps should start their own custom keycodes here
};

// Called from keyboard_post_init_user
void morse_init(void);

// Called from keychron_task_user (polled timeout driver)
void morse_task(void);

// Called from process_record_user BEFORE process_record_keychron_common.
// Returns false if the event was a morse keycode that was fully handled
// (firmware should stop propagating it).
bool process_record_morse(uint16_t keycode, keyrecord_t *record);

// Accessors used by the QMKata handler (defined in later task).
void morse_cfg_set_prompt(const char *buf, uint16_t len);
void morse_cfg_set_timing(uint16_t letter_ms, uint16_t word_ms);
void morse_cfg_set_flash_ms(uint16_t dit_ms, uint16_t dah_ms);
void morse_get_state(morse_mode_t *mode_out, uint16_t *prompt_pos_out,
                    uint8_t *element_count_out, uint16_t *prompt_len_out);

#endif // MORSE_ENABLE
```

**Step 1.2 — Write `morse_table.h` (empty stub, table filled in Task 3):**

```c
// keyboards/keychron/common/morse/morse_table.h
#pragma once
#include <stdint.h>

// Pattern encoding:
//   elements: bit-packed, LSB = first element. bit=0 dit, bit=1 dah.
//   count:    number of valid bits.
// Returns lowercase ASCII char, or 0 if pattern is not in the table.
char morse_decode(uint8_t elements, uint8_t count);

// Inverse: ASCII char -> (elements, count). Returns false if not encodable.
bool morse_encode(char c, uint8_t *elements_out, uint8_t *count_out);
```

**Step 1.3 — Write `morse.c` with empty function bodies:**

```c
// keyboards/keychron/common/morse/morse.c
#include "morse.h"
#include "morse_table.h"

#ifdef MORSE_ENABLE

void morse_init(void) {}
void morse_task(void) {}
bool process_record_morse(uint16_t keycode, keyrecord_t *record) { (void)keycode; (void)record; return true; }
void morse_cfg_set_prompt(const char *buf, uint16_t len) { (void)buf; (void)len; }
void morse_cfg_set_timing(uint16_t letter_ms, uint16_t word_ms) { (void)letter_ms; (void)word_ms; }
void morse_cfg_set_flash_ms(uint16_t dit_ms, uint16_t dah_ms) { (void)dit_ms; (void)dah_ms; }
void morse_get_state(morse_mode_t *mode_out, uint16_t *prompt_pos_out,
                     uint8_t *element_count_out, uint16_t *prompt_len_out) {
    if (mode_out) *mode_out = MORSE_OFF;
    if (prompt_pos_out) *prompt_pos_out = 0;
    if (element_count_out) *element_count_out = 0;
    if (prompt_len_out) *prompt_len_out = 0;
}

// morse_table stubs (real table arrives in Task 3)
char morse_decode(uint8_t elements, uint8_t count) { (void)elements; (void)count; return 0; }
bool morse_encode(char c, uint8_t *e, uint8_t *n) { (void)c; (void)e; (void)n; return false; }

#endif
```

**Step 1.4 — Write `morse.mk`:**

```makefile
# keyboards/keychron/common/morse/morse.mk
MORSE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

OPT_DEFS += -DMORSE_ENABLE

SRC += \
    $(MORSE_DIR)morse.c

VPATH += $(MORSE_DIR)
```

**Step 1.5 — Patch `keyboards/keychron/q3_max/rules.mk`:**

Add two lines so the final file looks like:

```makefile
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
LEADER_ENABLE = yes
MODULE_LOADER_ENABLE = yes
MORSE_ENABLE = yes

include keyboards/keychron/common/wireless/wireless.mk
include keyboards/keychron/common/keychron_common.mk

include keyboards/keychron/qmkata/qmkata.mk

ifeq ($(strip $(MORSE_ENABLE)), yes)
    include keyboards/keychron/common/morse/morse.mk
endif

SRC += \
    q3_max_user.c \
    debug_user.c

VPATH += $(TOP_DIR)/keyboards/keychron
```

**Step 1.6 — Build to verify the skeleton compiles:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -20
```
Expected: clean build. `MC_DIT` etc. are defined but nothing in the keymap references them yet.

**Step 1.7 — Commit:**

```bash
git add keyboards/keychron/common/morse/ keyboards/keychron/q3_max/rules.mk
git commit -m "feat(morse): add feature skeleton gated by MORSE_ENABLE

Empty stubs for morse_init/morse_task/process_record_morse and
config setters. Wired into Q3 Max rules.mk. No behavior yet."
```

---

## Task 2: Host-side unit test harness for decoder + table (TDD foundation)

**Why now:** the decoder is a pure function — perfect for TDD. Establish the test target before writing the table so we can red-green-refactor the table entries.

**Files:**
- Create: `tests/morse/test.mk`
- Create: `tests/morse/config.h` (minimal — no keyboard)
- Create: `tests/morse/test_morse_table.cpp`
- Create: `tests/morse/test_morse_table.c` — thin C shim pulling in `morse_table.c` sources under the `MORSE_ENABLE` build

**Reference:** look at `tests/leader/test.mk` and `tests/leader/test_leader.cpp` (or similar) for the Google Test pattern QMK uses. Copy that structure.

**Step 2.1 — Read existing leader test setup to mirror it:**

```bash
cat tests/leader/test.mk
ls tests/leader/
```

**Step 2.2 — Write `tests/morse/test.mk`:**

```makefile
# tests/morse/test.mk
MORSE_ROOT := $(ROOT_DIR)/keyboards/keychron/common/morse

VPATH += $(MORSE_ROOT)

SRC += \
    $(MORSE_ROOT)/morse_table.c

OPT_DEFS += -DMORSE_ENABLE
```

> **Note:** This implies we will split the table into its own `morse_table.c` in Task 3. That's a minor split from the design doc (which put the table in `.h` as `static const`) — needed so the test binary can link the decoder without linking the full QMK matrix layer.

**Step 2.3 — Write `tests/morse/test_morse_table.cpp` with the initial failing test:**

```cpp
#include "gtest/gtest.h"
extern "C" {
#include "morse_table.h"
}

TEST(MorseTable, DecodesA) {
    // 'a' = dit dah = bits: 0, 1 -> elements = 0b10 = 2, count = 2
    EXPECT_EQ('a', morse_decode(0b10, 2));
}

TEST(MorseTable, DecodesE) {
    // 'e' = dit -> elements = 0, count = 1
    EXPECT_EQ('e', morse_decode(0, 1));
}

TEST(MorseTable, DecodesT) {
    // 't' = dah -> elements = 1, count = 1
    EXPECT_EQ('t', morse_decode(1, 1));
}

TEST(MorseTable, UnknownReturnsZero) {
    // pattern 0 with count 0 is empty, not a valid char
    EXPECT_EQ(0, morse_decode(0, 0));
    // 8 dits in a row is not a defined morse character
    EXPECT_EQ(0, morse_decode(0, 8));
}

TEST(MorseTable, EncodeRoundTrip) {
    for (char c = 'a'; c <= 'z'; c++) {
        uint8_t e = 0, n = 0;
        ASSERT_TRUE(morse_encode(c, &e, &n)) << "no encoding for '" << c << "'";
        EXPECT_EQ(c, morse_decode(e, n)) << "round-trip failed for '" << c << "'";
    }
}

TEST(MorseTable, EncodeDigits) {
    for (char c = '0'; c <= '9'; c++) {
        uint8_t e = 0, n = 0;
        ASSERT_TRUE(morse_encode(c, &e, &n)) << "no encoding for '" << c << "'";
        EXPECT_EQ(c, morse_decode(e, n));
        EXPECT_EQ(5, n) << "digits should be 5 elements";
    }
}
```

**Step 2.4 — Run the test and confirm it FAILS (morse_table.c doesn't exist yet):**

```bash
make test:morse 2>&1 | tail -20
```
Expected: compile error about missing `morse_table.c`. This is the RED phase.

**Step 2.5 — Commit (failing test + harness):**

```bash
git add tests/morse/
git commit -m "test(morse): add failing decoder/encoder test harness

Google Test coverage for morse_decode/morse_encode round-trip across
A-Z and 0-9 plus a handful of sentinel values. Red phase — morse_table.c
doesn't exist yet."
```

---

## Task 3: Implement `morse_table.c` to make Task 2 tests pass

**Files:**
- Create: `keyboards/keychron/common/morse/morse_table.c`
- Modify: `keyboards/keychron/common/morse/morse.mk` (add `morse_table.c` to SRC)

**Reference:** canonical morse table. Encoding rule (restated from design doc):
> `elements`: bit N = Nth element in time. 0 = dit, 1 = dah. `count` = number of elements.

**Step 3.1 — Write `morse_table.c`:**

```c
#include "morse_table.h"
#include <stddef.h>

// Each entry encodes a morse pattern:
//   elements: bit N = Nth element (0 = dit, 1 = dah)
//   count:    number of valid bits
//   ch:       lowercase ASCII character produced
typedef struct {
    uint8_t elements;
    uint8_t count;
    char    ch;
} morse_entry_t;

static const morse_entry_t k_table[] = {
    // Letters (A-Z)
    {0b10,    2, 'a'}, // .-
    {0b0001,  4, 'b'}, // -...
    {0b0101,  4, 'c'}, // -.-.
    {0b001,   3, 'd'}, // -..
    {0b0,     1, 'e'}, // .
    {0b0100,  4, 'f'}, // ..-.
    {0b011,   3, 'g'}, // --.
    {0b0000,  4, 'h'}, // ....
    {0b00,    2, 'i'}, // ..
    {0b1110,  4, 'j'}, // .---
    {0b101,   3, 'k'}, // -.-
    {0b0010,  4, 'l'}, // .-..
    {0b11,    2, 'm'}, // --
    {0b01,    2, 'n'}, // -.
    {0b111,   3, 'o'}, // ---
    {0b0110,  4, 'p'}, // .--.
    {0b1011,  4, 'q'}, // --.-
    {0b010,   3, 'r'}, // .-.
    {0b000,   3, 's'}, // ...
    {0b1,     1, 't'}, // -
    {0b100,   3, 'u'}, // ..-
    {0b1000,  4, 'v'}, // ...-
    {0b110,   3, 'w'}, // .--
    {0b1001,  4, 'x'}, // -..-
    {0b1101,  4, 'y'}, // -.--
    {0b0011,  4, 'z'}, // --..

    // Digits (0-9) — all 5 elements
    {0b11111, 5, '0'}, // -----
    {0b11110, 5, '1'}, // .----
    {0b11100, 5, '2'}, // ..---
    {0b11000, 5, '3'}, // ...--
    {0b10000, 5, '4'}, // ....-
    {0b00000, 5, '5'}, // .....
    {0b00001, 5, '6'}, // -....
    {0b00011, 5, '7'}, // --...
    {0b00111, 5, '8'}, // ---..
    {0b01111, 5, '9'}, // ----.

    // Basic punctuation (optional — kept minimal)
    {0b010101, 6, '.'}, // .-.-.-
    {0b110011, 6, ','}, // --..--
    {0b001100, 6, '?'}, // ..--..
};

#define K_TABLE_LEN (sizeof(k_table) / sizeof(k_table[0]))

char morse_decode(uint8_t elements, uint8_t count) {
    if (count == 0 || count > 8) return 0;
    // Mask elements to `count` bits for safe comparison against table entries.
    uint8_t mask = (count == 8) ? 0xFF : (uint8_t)((1u << count) - 1);
    uint8_t e    = elements & mask;
    for (size_t i = 0; i < K_TABLE_LEN; i++) {
        if (k_table[i].count == count && k_table[i].elements == e) {
            return k_table[i].ch;
        }
    }
    return 0;
}

bool morse_encode(char c, uint8_t *elements_out, uint8_t *count_out) {
    // Normalize uppercase to lowercase.
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    for (size_t i = 0; i < K_TABLE_LEN; i++) {
        if (k_table[i].ch == c) {
            if (elements_out) *elements_out = k_table[i].elements;
            if (count_out)    *count_out    = k_table[i].count;
            return true;
        }
    }
    return false;
}
```

**Step 3.2 — Update `morse.mk` to compile the table:**

```makefile
SRC += \
    $(MORSE_DIR)morse.c \
    $(MORSE_DIR)morse_table.c
```

**Step 3.3 — Remove the stub implementations of `morse_decode`/`morse_encode` from `morse.c`** (they'd cause duplicate symbol errors now that `morse_table.c` provides them).

**Step 3.4 — Run unit tests, confirm GREEN:**

```bash
make test:morse 2>&1 | tail -20
```
Expected: all tests pass.

**Step 3.5 — Build firmware, confirm no regressions:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -5
```
Expected: clean build.

**Step 3.6 — Commit:**

```bash
git add keyboards/keychron/common/morse/morse_table.c keyboards/keychron/common/morse/morse.mk keyboards/keychron/common/morse/morse.c
git commit -m "feat(morse): implement decoder/encoder table for A-Z, 0-9, punct

Pure-C lookup table keyed on (elements, count). Passes the host-side
unit tests added in the previous commit."
```

---

## Task 4: State machine + paddle handling + mode toggles

**Why now:** with the decoder verified, we can drive it from the paddle handler. Still no RGB — keep it decoupled so this task is pure logic.

**Files:**
- Modify: `keyboards/keychron/common/morse/morse.c`
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c`
- Modify: `keyboards/keychron/q3_max/iso_encoder/keymaps/keychron/keymap.c`
- Modify: `keyboards/keychron/q3_max/q3_max_user.c`

**Step 4.1 — Replace `morse.c` stubs with full state machine (no RGB hooks yet — those land in Task 5):**

```c
#include "morse.h"
#include "morse_table.h"
#include <string.h>

#ifdef MORSE_ENABLE

#define MORSE_MAX_ELEMENTS 8
#define MORSE_PROMPT_BUF_SIZE 128

#define MORSE_DEFAULT_LETTER_TIMEOUT_MS 500
#define MORSE_DEFAULT_WORD_TIMEOUT_MS   1200
#define MORSE_DEFAULT_DIT_FLASH_MS      80
#define MORSE_DEFAULT_DAH_FLASH_MS      240

typedef struct {
    uint16_t letter_timeout_ms;
    uint16_t word_timeout_ms;
    uint16_t dit_flash_ms;
    uint16_t dah_flash_ms;
    char     prompt_buf[MORSE_PROMPT_BUF_SIZE];
    uint16_t prompt_buf_len;
} morse_cfg_t;

typedef struct {
    morse_mode_t mode;
    uint8_t      elements;
    uint8_t      element_count;
    uint32_t     last_element_time;
    bool         letter_committed;
    uint16_t     prompt_pos;
} morse_state_t;

static morse_cfg_t   s_cfg;
static morse_state_t s_state;

// Forward decls for RGB hooks — stubbed in this task, filled in Task 5.
static void morse_rgb_flash_paddle(uint8_t is_dah);
static void morse_rgb_flash_feedback(bool correct);
static void morse_rgb_flash_blips(uint8_t r, uint8_t g, uint8_t b, uint8_t count);

void morse_init(void) {
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_state, 0, sizeof(s_state));
    s_cfg.letter_timeout_ms = MORSE_DEFAULT_LETTER_TIMEOUT_MS;
    s_cfg.word_timeout_ms   = MORSE_DEFAULT_WORD_TIMEOUT_MS;
    s_cfg.dit_flash_ms      = MORSE_DEFAULT_DIT_FLASH_MS;
    s_cfg.dah_flash_ms      = MORSE_DEFAULT_DAH_FLASH_MS;
    // Default prompt: alphabet (used only if training mode entered before
    // host pushes a prompt). Keep it short.
    const char *dflt = "abcdefghijklmnopqrstuvwxyz";
    uint16_t    n    = (uint16_t)strlen(dflt);
    memcpy(s_cfg.prompt_buf, dflt, n);
    s_cfg.prompt_buf_len = n;
}

static void reset_letter_buffer(void) {
    s_state.elements      = 0;
    s_state.element_count = 0;
}

// Expected pattern length for the current prompt character (in training mode).
// Returns 0 if no encoding exists (prompt has non-morse chars).
static uint8_t expected_len_for_prompt_pos(void) {
    if (s_state.prompt_pos >= s_cfg.prompt_buf_len) return 0;
    uint8_t e = 0, n = 0;
    if (!morse_encode(s_cfg.prompt_buf[s_state.prompt_pos], &e, &n)) return 0;
    return n;
}

static bool elements_match_prompt(void) {
    if (s_state.prompt_pos >= s_cfg.prompt_buf_len) return false;
    uint8_t e = 0, n = 0;
    if (!morse_encode(s_cfg.prompt_buf[s_state.prompt_pos], &e, &n)) return false;
    return s_state.element_count == n && s_state.elements == e;
}

static void commit_letter_input(void) {
    char c = morse_decode(s_state.elements, s_state.element_count);
    if (c >= 'a' && c <= 'z') {
        tap_code16(KC_A + (c - 'a'));
    } else if (c >= '0' && c <= '9') {
        // QMK keycodes: KC_1..KC_9, then KC_0
        if (c == '0') tap_code(KC_0);
        else          tap_code(KC_1 + (c - '1'));
    } else if (c == '.') tap_code16(KC_DOT);
    else if  (c == ',') tap_code16(KC_COMMA);
    else if  (c == '?') tap_code16(LSFT(KC_SLSH));
    // else: unknown pattern, emit nothing
    reset_letter_buffer();
}

static void commit_letter_train(void) {
    bool ok = elements_match_prompt();
    morse_rgb_flash_feedback(ok);
    s_state.prompt_pos++;
    if (s_state.prompt_pos >= s_cfg.prompt_buf_len) s_state.prompt_pos = 0;
    reset_letter_buffer();
}

static void commit_letter(void) {
    if (s_state.mode == MORSE_INPUT) commit_letter_input();
    else if (s_state.mode == MORSE_TRAIN) commit_letter_train();
    else reset_letter_buffer();
}

static void on_paddle(uint8_t is_dah) {
    if (s_state.mode == MORSE_OFF) return;
    if (s_state.element_count < MORSE_MAX_ELEMENTS) {
        if (is_dah) s_state.elements |= (uint8_t)(1u << s_state.element_count);
        s_state.element_count++;
    }
    s_state.last_element_time = timer_read32();
    s_state.letter_committed  = false;
    morse_rgb_flash_paddle(is_dah);

    // Training fast-path: immediate commit on correct-length match.
    if (s_state.mode == MORSE_TRAIN) {
        uint8_t exp_len = expected_len_for_prompt_pos();
        if (exp_len != 0 && s_state.element_count == exp_len && elements_match_prompt()) {
            commit_letter_train();
        }
    }
}

static void on_input_togg(void) {
    s_state.mode = (s_state.mode == MORSE_INPUT) ? MORSE_OFF : MORSE_INPUT;
    reset_letter_buffer();
    s_state.letter_committed = false;
}

static void on_train_togg(void) {
    if (s_state.mode != MORSE_TRAIN && s_cfg.prompt_buf_len == 0) {
        // Refuse — prompt empty.
        morse_rgb_flash_blips(255, 0, 0, 2);
        return;
    }
    s_state.mode = (s_state.mode == MORSE_TRAIN) ? MORSE_OFF : MORSE_TRAIN;
    s_state.prompt_pos = 0;
    reset_letter_buffer();
    s_state.letter_committed = false;
}

bool process_record_morse(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case MC_DIT:
            if (record->event.pressed) on_paddle(0);
            return false;
        case MC_DAH:
            if (record->event.pressed) on_paddle(1);
            return false;
        case MC_INPUT_TOGG:
            if (record->event.pressed) on_input_togg();
            return false;
        case MC_TRAIN_TOGG:
            if (record->event.pressed) on_train_togg();
            return false;
        default:
            return true;
    }
}

void morse_task(void) {
    if (s_state.mode == MORSE_OFF) return;
    uint32_t elapsed = timer_elapsed32(s_state.last_element_time);

    if (!s_state.letter_committed && s_state.element_count > 0 &&
        elapsed >= s_cfg.letter_timeout_ms) {
        commit_letter();
        s_state.letter_committed = true;
    }

    if (s_state.mode == MORSE_INPUT && s_state.letter_committed &&
        elapsed >= s_cfg.word_timeout_ms) {
        tap_code(KC_SPC);
        // Don't repeat the word-space: keep letter_committed=true but shift
        // the base so the next check requires fresh paddle activity.
        s_state.last_element_time = timer_read32() - s_cfg.word_timeout_ms;
        // The only way out of this stuck state is a new paddle press, which
        // resets letter_committed=false in on_paddle().
        // Pragmatic guard: clear the flag to prevent re-fire without new input.
        s_state.letter_committed = false;
    }
}

// Config setters (called from QMKata handler in Task 6).
void morse_cfg_set_prompt(const char *buf, uint16_t len) {
    if (len > MORSE_PROMPT_BUF_SIZE) len = MORSE_PROMPT_BUF_SIZE;
    memcpy(s_cfg.prompt_buf, buf, len);
    s_cfg.prompt_buf_len = len;
    s_state.prompt_pos   = 0;
}

void morse_cfg_set_timing(uint16_t letter_ms, uint16_t word_ms) {
    s_cfg.letter_timeout_ms = letter_ms;
    s_cfg.word_timeout_ms   = word_ms;
}

void morse_cfg_set_flash_ms(uint16_t dit_ms, uint16_t dah_ms) {
    s_cfg.dit_flash_ms = dit_ms;
    s_cfg.dah_flash_ms = dah_ms;
}

void morse_get_state(morse_mode_t *mode_out, uint16_t *prompt_pos_out,
                     uint8_t *element_count_out, uint16_t *prompt_len_out) {
    if (mode_out)          *mode_out          = s_state.mode;
    if (prompt_pos_out)    *prompt_pos_out    = s_state.prompt_pos;
    if (element_count_out) *element_count_out = s_state.element_count;
    if (prompt_len_out)    *prompt_len_out    = s_cfg.prompt_buf_len;
}

// RGB stubs — real implementations arrive in Task 5.
static void morse_rgb_flash_paddle(uint8_t is_dah) { (void)is_dah; }
static void morse_rgb_flash_feedback(bool correct) { (void)correct; }
static void morse_rgb_flash_blips(uint8_t r, uint8_t g, uint8_t b, uint8_t count) {
    (void)r; (void)g; (void)b; (void)count;
}

#endif
```

**Step 4.2 — Hook into `q3_max_user.c`:**

- Add `#include "morse.h"` near the top (guarded by `MORSE_ENABLE`).
- In `keyboard_post_init_user`, after the existing safe-mode block, add:
  ```c
  #ifdef MORSE_ENABLE
      morse_init();
  #endif
  ```
- In `keychron_task_user`, add:
  ```c
  #ifdef MORSE_ENABLE
      morse_task();
  #endif
  ```

**Step 4.3 — Hook into ANSI keymap `process_record_user`:**

In `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c`, inside `process_record_user` and before the existing `process_record_keychron_common` call:

```c
#ifdef MORSE_ENABLE
    if (!process_record_morse(keycode, record)) {
        return false;
    }
#endif
```

Add `#include "morse.h"` near the other includes at the top of `keymap.c` (guarded).

**Step 4.4 — Mirror the same hook in the ISO keymap.**

**Step 4.5 — Build and verify:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -5
make keychron/q3_max/iso_encoder:keychron 2>&1 | tail -5
```
Expected: both clean.

**Step 4.6 — Commit:**

```bash
git add keyboards/keychron/common/morse/morse.c keyboards/keychron/q3_max/q3_max_user.c keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c keyboards/keychron/q3_max/iso_encoder/keymaps/keychron/keymap.c
git commit -m "feat(morse): paddle state machine + mode toggles

Implements MC_DIT/MC_DAH/MC_INPUT_TOGG/MC_TRAIN_TOGG handling, idle-timeout
letter commit, word-space on extended idle (input mode), and training-mode
hybrid commit (fast path on correct-length match, timeout fallback). RGB
hooks are stubs; real flashes land in the next task."
```

---

## Task 5: RGB full-matrix flashes (paddle echo + pass/fail feedback)

**Files:**
- Create: `keyboards/keychron/common/morse/morse_rgb.c`
- Modify: `keyboards/keychron/common/morse/morse.c` (remove RGB stubs, declare external functions instead)
- Modify: `keyboards/keychron/common/morse/morse.mk` (add `morse_rgb.c`)

**Mechanism:** reuse `g_rgb_matrix_host_buf` (defined in `qmkata_sysex_handler.c:157`, consumed by `rgb_matrix_host_buf_render` in `q3_max_user.c:139-152`). For a full-matrix flash we write all `RGB_MATRIX_LED_COUNT` entries with the same color and duration (in frames — since the consumer decrements `duration` once per render).

**Framerate assumption:** the host-buf consumer decrements once per call to `rgb_matrix_host_buf_render`, which is called per RGB frame. At ~60 fps (QMK default), 1 frame ≈ 16 ms. Convert ms → frames via `((ms) + 15) / 16`.

**Step 5.1 — Write `morse_rgb.c`:**

```c
#include "morse.h"

#if defined(MORSE_ENABLE) && defined(RGB_MATRIX_ENABLE)

#include "rgb_matrix.h"
#include "qmkata/qmkata_rgb_matrix_user.h"  // for rgb_matrix_host_buffer_t

// Provided by qmkata_sysex_handler.c
extern rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

static uint8_t ms_to_frames(uint16_t ms) {
    // ~60 fps assumption; round up so short flashes remain visible.
    uint32_t f = ((uint32_t)ms + 15u) / 16u;
    if (f == 0)   f = 1;
    if (f > 255)  f = 255;
    return (uint8_t)f;
}

static void flash_all(uint8_t r, uint8_t g, uint8_t b, uint8_t frames) {
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        g_rgb_matrix_host_buf.led[i].r        = r;
        g_rgb_matrix_host_buf.led[i].g        = g;
        g_rgb_matrix_host_buf.led[i].b        = b;
        g_rgb_matrix_host_buf.led[i].duration = frames;
    }
    g_rgb_matrix_host_buf.written = 1;
}

// Pulled out of morse.c into this TU so they can be real (not static stubs).
// We redeclare them `void` (non-static) here to match the extern signatures.
void morse_rgb_flash_paddle_impl(uint8_t is_dah, uint16_t dit_ms, uint16_t dah_ms) {
    uint16_t ms = is_dah ? dah_ms : dit_ms;
    flash_all(255, 255, 255, ms_to_frames(ms));
}

void morse_rgb_flash_feedback_impl(bool correct) {
    if (correct) flash_all(0,   200, 0,   ms_to_frames(150));
    else         flash_all(200, 0,   0,   ms_to_frames(150));
}

void morse_rgb_flash_blips_impl(uint8_t r, uint8_t g, uint8_t b, uint8_t count) {
    // Simple blip: single flash. A true "N blips" would need a queue;
    // YAGNI — one 100ms pulse is enough signal for "prompt empty".
    (void)count;
    flash_all(r, g, b, ms_to_frames(100));
}

#endif
```

**Step 5.2 — Update `morse.c`:** remove the three `static` stub definitions and the `static` forward declarations. Replace with:

```c
#if defined(MORSE_ENABLE) && defined(RGB_MATRIX_ENABLE)
extern void morse_rgb_flash_paddle_impl(uint8_t is_dah, uint16_t dit_ms, uint16_t dah_ms);
extern void morse_rgb_flash_feedback_impl(bool correct);
extern void morse_rgb_flash_blips_impl(uint8_t r, uint8_t g, uint8_t b, uint8_t count);

static void morse_rgb_flash_paddle(uint8_t is_dah) {
    morse_rgb_flash_paddle_impl(is_dah, s_cfg.dit_flash_ms, s_cfg.dah_flash_ms);
}
static void morse_rgb_flash_feedback(bool correct) {
    morse_rgb_flash_feedback_impl(correct);
}
static void morse_rgb_flash_blips(uint8_t r, uint8_t g, uint8_t b, uint8_t count) {
    morse_rgb_flash_blips_impl(r, g, b, count);
}
#else
static void morse_rgb_flash_paddle(uint8_t is_dah) { (void)is_dah; }
static void morse_rgb_flash_feedback(bool correct) { (void)correct; }
static void morse_rgb_flash_blips(uint8_t r, uint8_t g, uint8_t b, uint8_t count) {
    (void)r; (void)g; (void)b; (void)count;
}
#endif
```

**Step 5.3 — Verify `qmkata_rgb_matrix_user.h` exposes `rgb_matrix_host_buffer_t` publicly.** If it doesn't, replace the include with a forward-declaration of the struct or use the existing pattern in `q3_max_user.c:136` (extern declaration of `g_rgb_matrix_host_buf`).

```bash
grep -n "rgb_matrix_host_buffer_t" keyboards/keychron/q3_max/*.c keyboards/keychron/qmkata/*.h
```

If the struct is only defined in the `.c`, promote its typedef to a shared header or duplicate the typedef locally in `morse_rgb.c` with a comment.

**Step 5.4 — Update `morse.mk`:**

```makefile
SRC += \
    $(MORSE_DIR)morse.c \
    $(MORSE_DIR)morse_table.c

ifeq ($(strip $(RGB_MATRIX_ENABLE)), yes)
    SRC += $(MORSE_DIR)morse_rgb.c
endif
```

**Step 5.5 — Build and verify:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -5
```
Expected: clean build.

**Step 5.6 — Hardware smoke test (manual, if hardware available):**

1. Flash the firmware.
2. Temporarily edit the ANSI keymap to put `MC_INPUT_TOGG` on an otherwise-unused key (e.g., the right-side Fn key slot `KC_RCTL`), and `MC_DIT` / `MC_DAH` on two convenient keys.
3. Rebuild, flash. Press the toggle. Press dit/dah — expect full-matrix white flashes of distinct durations.
4. Revert the temporary keymap edit before commit.

**Step 5.7 — Commit:**

```bash
git add keyboards/keychron/common/morse/
git commit -m "feat(morse): full-matrix RGB flash on paddle + train feedback

Paddle presses produce white full-matrix flashes (dit short, dah long).
Training commits flash green on correct, red on wrong. Reuses the
existing g_rgb_matrix_host_buf pipeline — no new RGB plumbing."
```

---

## Task 6: QMKata SysEx handler for host-side config push

**Files:**
- Modify: `keyboards/keychron/qmkata/QMKata.h` (add `QMKATA_ID_MORSE = 15`)
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c` (register SET and GET handlers)

**Protocol (single message ID, multiplexed by sub-command in `buf[0]`):**

| sub-cmd (buf[0]) | Direction | Payload                                           | Response payload                           |
| ---------------- | --------- | ------------------------------------------------- | ------------------------------------------ |
| `0x01` SET_PROMPT | host→kb  | `u8 len` then `len` bytes (≤128)                 | `u8 stored_len`                            |
| `0x02` SET_TIMING | host→kb  | `u16 letter_ms; u16 word_ms` (LE)                | — (no response, fire-and-forget)           |
| `0x03` SET_FLASH  | host→kb  | `u16 dit_ms; u16 dah_ms` (LE)                    | —                                          |
| `0x04` GET_STATE  | host→kb  | (empty) | `u8 mode; u16 prompt_pos; u8 element_count; u16 prompt_len` |

**Step 6.1 — Add the enum entry in `QMKata.h`:**

Insert after `QMKATA_ID_MODULE` and before `QMKATA_ID_DYNLD_FUNCTION`:

```c
QMKATA_ID_MORSE          = 15,  // morse code config (SRAM)
```

**Step 6.2 — Add the handler in `qmkata_sysex_handler.c`:**

Near the top, next to the other feature includes:

```c
#if defined(MORSE_ENABLE)
#    include "morse.h"
#endif
```

In `qmkata_sysex_handler()` `cmd == QMKATA_CMD_SET` block, add:

```c
#if defined(MORSE_ENABLE)
    if (id == QMKATA_ID_MORSE) _QMKATA_HANDLE_CMD_SET_FN(morse)(cmd, seqnum, len, buf);
#endif
```

Mirror in the `QMKATA_CMD_GET` block.

At the end of the file (before any existing `#endif` EOF), add the handler:

```c
//------------------------------------------------------------------------------
#if defined(MORSE_ENABLE)

enum morse_sub_cmd {
    MORSE_SUB_SET_PROMPT = 0x01,
    MORSE_SUB_SET_TIMING = 0x02,
    MORSE_SUB_SET_FLASH  = 0x03,
    MORSE_SUB_GET_STATE  = 0x04,
};

_QMKATA_HANDLE_CMD_SET(morse) {
    if (len < 1) return;
    uint8_t sub = buf[0];
    DBG_USR(qmkata, "morse:set sub=%u len=%u\n", sub, len);
    switch (sub) {
        case MORSE_SUB_SET_PROMPT: {
            if (len < 2) return;
            uint8_t plen = buf[1];
            if ((uint16_t)plen + 2 > len) plen = (uint8_t)(len - 2);
            morse_cfg_set_prompt((const char *)&buf[2], plen);
            uint8_t resp[3] = { seqnum, QMKATA_ID_MORSE, plen };
            qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
            return;
        }
        case MORSE_SUB_SET_TIMING: {
            if (len < 5) return;
            uint16_t letter_ms = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            uint16_t word_ms   = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
            morse_cfg_set_timing(letter_ms, word_ms);
            return;
        }
        case MORSE_SUB_SET_FLASH: {
            if (len < 5) return;
            uint16_t dit_ms = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            uint16_t dah_ms = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
            morse_cfg_set_flash_ms(dit_ms, dah_ms);
            return;
        }
        default:
            return;
    }
}

_QMKATA_HANDLE_CMD_GET(morse) {
    if (len < 1) return;
    uint8_t sub = buf[0];
    if (sub != MORSE_SUB_GET_STATE) return;

    morse_mode_t mode = MORSE_OFF;
    uint16_t     pp   = 0, pl = 0;
    uint8_t      ec   = 0;
    morse_get_state(&mode, &pp, &ec, &pl);

    uint8_t resp[9];
    resp[0] = seqnum;
    resp[1] = QMKATA_ID_MORSE;
    resp[2] = (uint8_t)mode;
    resp[3] = (uint8_t)(pp & 0xFF);
    resp[4] = (uint8_t)((pp >> 8) & 0xFF);
    resp[5] = ec;
    resp[6] = (uint8_t)(pl & 0xFF);
    resp[7] = (uint8_t)((pl >> 8) & 0xFF);
    resp[8] = 0; // reserved
    qmkata_send_sysex(QMKATA_CMD_RESPONSE, resp, sizeof(resp));
}

#endif // MORSE_ENABLE
```

**Step 6.3 — Build and verify:**

```bash
make keychron/q3_max/ansi_encoder:keychron 2>&1 | tail -5
```

**Step 6.4 — Commit:**

```bash
git add keyboards/keychron/qmkata/QMKata.h keyboards/keychron/q3_max/qmkata_sysex_handler.c
git commit -m "feat(morse): QMKata SysEx handler for SRAM config push

Adds QMKATA_ID_MORSE (15) with four sub-commands: SET_PROMPT,
SET_TIMING, SET_FLASH, GET_STATE. All config is SRAM-only — host
re-pushes after each power cycle."
```

---

## Task 7: Wire keycodes into the actual shipped keymap

**Why last:** previous tasks proved the feature compiles and works with temporary keymap edits. Now decide the permanent home for the morse keycodes.

**Recommendation:** place `MC_DIT`, `MC_DAH`, `MC_INPUT_TOGG`, `MC_TRAIN_TOGG` on the Fn layer (so the base layer is undisturbed). Concrete suggestion for ANSI:

- `MAC_FN` / `WIN_FN` layers, numpad-area keys `KC_7`, `KC_8`, `KC_9`, `KC_4` — currently `KC_1`, `KC_2`, `KC_3`, etc. in WIN_FN. Pick keys that are unlikely to be used on Mac FN layer. Final choice is the user's; this plan is a suggestion only.

**Step 7.1 — Ask the user (or the engineer executing this plan) which keys to use.** Do not guess.

**Step 7.2 — Edit `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c`** and the ISO counterpart to place the four morse keycodes on the chosen positions.

**Step 7.3 — Flash firmware, execute the manual test plan:**

1. **Passive:** no mode active → paddles emit nothing, no flash.
2. **Input mode:** press `MC_INPUT_TOGG`, tap `.-`, wait 500 ms → `a` typed. Tap `-...`, wait → `b`. Wait another 1200 ms → space emitted.
3. **Input garbage:** tap 9 paddles quickly → commit yields nothing.
4. **Mode mutual exclusion:** in input mode, press `MC_TRAIN_TOGG` → input disables, training enables. Verify: paddles no longer type; green/red flashes appear on commit.
5. **Training default prompt:** tap `.-` → green flash (A correct), prompt advances.
6. **Mode toggled mid-character:** in input mode, tap `.`, toggle to off mid-pause → confirm no stale commit on next toggle.
7. **Safe mode regression:** hold DEL at boot → existing safe-mode path still logs "SAFE MODE" and skips module activation.
8. **Leader / combo / tap-dance regressions:** confirm each of these existing features still fires as before.

**Step 7.4 — Commit:**

```bash
git add keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c keyboards/keychron/q3_max/iso_encoder/keymaps/keychron/keymap.c
git commit -m "feat(morse): place morse keycodes on Fn layer

Wires MC_DIT/MC_DAH/MC_INPUT_TOGG/MC_TRAIN_TOGG into the shipped
keymaps. Mac and Windows Fn layers mirror each other for consistency."
```

---

## Task 8: Update readme / docs (optional polish)

**File:** `keyboards/keychron/q3_max/readme.md`

Add a short section documenting:
- `MORSE_ENABLE` build flag (default on)
- The four keycodes and where they live in the default keymap
- Pointer to the design doc (`docs/plans/2026-04-24-morse-support-design.md`)
- Note that config is SRAM-only and must be re-pushed by the host after each power cycle
- QMKata SysEx ID 15 and sub-command table (copied from Task 6)

Commit:

```bash
git commit -am "docs(morse): readme section covering build flag and keycodes"
```

---

## Verification checklist (run before declaring done)

Use superpowers:verification-before-completion before claiming completion.

- [ ] `make keychron/q3_max/ansi_encoder:keychron` clean
- [ ] `make keychron/q3_max/iso_encoder:keychron` clean
- [ ] `make test:morse` passes
- [ ] Manual hardware test items 1-8 from Task 7.3 all pass
- [ ] Safe-mode regression: hold DEL at boot works
- [ ] Leader/combo/tap-dance regression: all still fire as before
- [ ] Design doc and impl plan both committed

## Out of scope (deliberate YAGNI)

- Host CLI/tooling for pushing morse config via QMKata (user's host tool will need its own work; not in this plan).
- Iambic keyer mode.
- EEPROM persistence of morse config.
- Status-driven blinking (layer, BT, battery).
- Training "lap complete" cue when prompt wraps.
- Audio/piezo output (no hardware).
- Per-key RGB morse output (full-matrix only in v1).

## Skills referenced

- @superpowers:executing-plans — task-by-task execution model
- @superpowers:test-driven-development — Task 2/3 is TDD (red before green)
- @superpowers:verification-before-completion — checklist before claiming done
- @superpowers:systematic-debugging — if any task's expected output doesn't match reality, apply this before guessing
