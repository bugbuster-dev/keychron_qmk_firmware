# Host-Offload Key Processing — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ship a `HOST_OFFLOAD_ENABLE` build flag on Keychron Q3 Max that, when toggled at runtime by the QMKata host daemon, turns the keyboard into a debounced-matrix + encoder + dip-switch event source and an HID-report forwarding transport, while preserving the existing QMK keymap as a local fallback.

**Architecture:** A new `host_offload` firmware module (under `keyboards/keychron/common/host_offload/`) owns a two-state runtime mode (LOCAL, HOST) and hooks into the existing matrix/encoder/dip/record pipeline at the earliest points so that in HOST mode, local processing short-circuits and per-edge events are published over the existing QMKata Raw HID sysex transport. A matching pair of host-side modules (`ActionEngine`, `HidReportBuilder`) in QMKata consume the events, run QMK-equivalent semantics, and inject resolved HID reports back into the keyboard via a new `ID_HID_REPORT_INJECT` sysex. The keyboard never exposes a new USB interface.

**Tech Stack:** C / ChibiOS / QMK (firmware); Python 3.11 / PyQt / pyfirmata2 (QMKata host daemon). Transport: Raw HID, QMKata sysex framing.

**Reference docs:**
- Design: `docs/plans/2026-04-24-host-offload-keyproc-design.md`
- Current on-MCU pipeline reference: `docs/plans/2026-04-16-q3-max-key-processing-design-final.md`
- Existing QMKata sysex protocol: `keyboards/keychron/q3_max/qmkata_sysex_handler.c`
- Existing host key-event consumer: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeyboard.py` (`sysex_pub_handler`, line 777+)
- Existing host KeyMachine: `/home/user/qmk/qmk-tools/qmk/QMKata/KeyMachine.py`

---

## Orientation for the implementing engineer

You do not need to know QMK internals beyond these facts:

1. **Matrix diff loop.** Each main-loop iteration, `matrix_task()` in `quantum/keyboard.c` compares the new cooked matrix (`matrix[]`) with the previous one and for each changed bit calls `action_exec(MAKE_KEYEVENT(row, col, pressed, time))`. That is the point where a press/release "edge" is born.

2. **Record chain.** `action_exec()` eventually calls `process_record()`, which calls the weak keyboard hook `process_record_kb()`. On Keychron boards, `process_record_kb()` lives at `keyboards/keychron/common/keychron_task.c:145`. It short-circuits the entire record chain if it returns `false`. This is our cheapest "disable local processing" hook.

3. **QMKata sysex.** Raw HID frames are parsed by QMKata (at `keyboards/keychron/qmkata/...`) which dispatches command bytes to `qmkata_sysex_handler()` in `keyboards/keychron/q3_max/qmkata_sysex_handler.c:159`. Adding a new sysex command = adding an `if (id == QMKATA_ID_FOO)` branch here. Outgoing frames are sent with `qmkata_send_sysex(cmd, buf, len)` (PUB/RESPONSE).

4. **Host-side QMKata.** `QMKataKeyboard.sysex_pub_handler()` already receives `ID_KEYPRESS_EVENT` and forwards to `KeyMachine.key_event(row, col, time, pressed)`. We extend both sides.

5. **HID report emission.** QMK exposes `host_keyboard_send(report_keyboard_t*)`, `host_nkro_send(report_nkro_t*)`, `host_consumer_send(uint16_t)`, `host_system_send(uint16_t)`, `host_mouse_send(report_mouse_t*)`. These take the report struct and write it to the correct USB HID endpoint. In HOST mode, we pass the host-daemon-produced bytes straight to these.

6. **Fallback guarantee.** LOCAL mode must be byte-for-byte identical to the current build. Every new code path is gated on `host_offload_is_host_mode()` which returns `false` at boot and only flips after a handshake.

**Relevant skills:**
- @superpowers:test-driven-development (all tasks follow red-green-refactor)
- @superpowers:verification-before-completion (never claim a task passes without the command output showing success)
- @superpowers:systematic-debugging (if a test misbehaves, do not start patching implementations blindly)
- @superpowers:executing-plans (overall driver)

**Working branch:** continue on `feat/combo-modules` unless instructed otherwise.

---

## Milestone map

- **M1 — Firmware scaffolding + mode state machine** (Tasks 1–4)
- **M2 — Firmware edge emission** (Tasks 5–7)
- **M3 — Firmware HID inject** (Tasks 8–9)
- **M4 — Firmware integration + build** (Tasks 10–12)
- **M5 — Host protocol client** (Tasks 13–15)
- **M6 — Host action engine (minimal)** (Tasks 16–19)
- **M7 — End-to-end bring-up** (Tasks 20–22)

Every task ends in a commit. Never batch. If the user says "stop and review", stop at the next commit boundary.

---

## M1 — Firmware scaffolding + mode state machine

### Task 1: Create `host_offload` module skeleton

**Files:**
- Create: `keyboards/keychron/common/host_offload/host_offload.h`
- Create: `keyboards/keychron/common/host_offload/host_offload.c`
- Create: `keyboards/keychron/common/host_offload/host_offload.mk`
- Create: `tests/host_offload/test_host_offload_mode.c`
- Create: `tests/host_offload/rules.mk`
- Create: `tests/host_offload/test.mk`

**Step 1.1: Write the failing mode-state test**

Write `tests/host_offload/test_host_offload_mode.c`. Test list (one `TEST()` each):

1. `boot_defaults_to_local` — after `host_offload_init()`, `host_offload_is_host_mode()` returns `false`.
2. `enter_transitions_to_host` — call `host_offload_on_mode_enter(proto_ver=1, flags=0)`; expect `host_offload_is_host_mode()` true, `last_reason == HOST_OFFLOAD_REASON_ENTER`.
3. `enter_rejects_unknown_proto_ver` — call with `proto_ver=2`; expect false, mode still LOCAL.
4. `leave_transitions_to_local` — enter then `host_offload_on_mode_leave()`; expect LOCAL, `last_reason == HOST_OFFLOAD_REASON_LEAVE`.
5. `ping_resets_watchdog` — enter, advance time 900ms, `host_offload_on_ping()`, advance 900ms, `host_offload_task()` called → still HOST.
6. `missed_ping_returns_to_local` — enter, advance time > `HOST_OFFLOAD_PING_TIMEOUT_MS` (default 2000), `host_offload_task()` → LOCAL, `last_reason == HOST_OFFLOAD_REASON_WATCHDOG`.
7. `transition_calls_clear_keyboard` — both directions invoke a mocked `clear_keyboard()` exactly once.

Use QMK's existing test harness (`tests/` uses Google Test via `build_test.mk`). Mock `timer_read32()` and `clear_keyboard()` with weak symbols in a `mocks.c`.

```c
// tests/host_offload/test_host_offload_mode.c
#include "gtest/gtest.h"
extern "C" {
#include "host_offload.h"
}

// --- mocks ---
static uint32_t g_mock_time_ms = 0;
static int g_clear_keyboard_calls = 0;
extern "C" uint32_t timer_read32(void) { return g_mock_time_ms; }
extern "C" void clear_keyboard(void) { g_clear_keyboard_calls++; }

class HostOffloadTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mock_time_ms = 0;
        g_clear_keyboard_calls = 0;
        host_offload_init();
    }
};

TEST_F(HostOffloadTest, boot_defaults_to_local) {
    EXPECT_FALSE(host_offload_is_host_mode());
}

TEST_F(HostOffloadTest, enter_transitions_to_host) {
    EXPECT_TRUE(host_offload_on_mode_enter(1, 0));
    EXPECT_TRUE(host_offload_is_host_mode());
    EXPECT_EQ(HOST_OFFLOAD_REASON_ENTER, host_offload_last_reason());
    EXPECT_EQ(1, g_clear_keyboard_calls);
}

TEST_F(HostOffloadTest, enter_rejects_unknown_proto_ver) {
    EXPECT_FALSE(host_offload_on_mode_enter(2, 0));
    EXPECT_FALSE(host_offload_is_host_mode());
}

TEST_F(HostOffloadTest, leave_transitions_to_local) {
    host_offload_on_mode_enter(1, 0);
    host_offload_on_mode_leave();
    EXPECT_FALSE(host_offload_is_host_mode());
    EXPECT_EQ(HOST_OFFLOAD_REASON_LEAVE, host_offload_last_reason());
    EXPECT_EQ(2, g_clear_keyboard_calls);
}

TEST_F(HostOffloadTest, ping_resets_watchdog) {
    host_offload_on_mode_enter(1, 0);
    g_mock_time_ms = 1900;
    host_offload_on_ping(0);
    g_mock_time_ms = 2800;
    host_offload_task();
    EXPECT_TRUE(host_offload_is_host_mode());
}

TEST_F(HostOffloadTest, missed_ping_returns_to_local) {
    host_offload_on_mode_enter(1, 0);
    g_mock_time_ms = 2100;
    host_offload_task();
    EXPECT_FALSE(host_offload_is_host_mode());
    EXPECT_EQ(HOST_OFFLOAD_REASON_WATCHDOG, host_offload_last_reason());
}
```

**Step 1.2: Run to verify failure**

```
make -f tests/host_offload/rules.mk test
```
Expected: compile error "host_offload.h: No such file".

**Step 1.3: Write header**

`host_offload.h`:
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HOST_OFFLOAD_LOCAL = 0,
    HOST_OFFLOAD_HOST  = 1,
} host_offload_mode_t;

typedef enum {
    HOST_OFFLOAD_REASON_NONE      = 0,
    HOST_OFFLOAD_REASON_ENTER     = 1,
    HOST_OFFLOAD_REASON_LEAVE     = 2,
    HOST_OFFLOAD_REASON_WATCHDOG  = 3,
    HOST_OFFLOAD_REASON_OVERFLOW  = 4,
    HOST_OFFLOAD_REASON_SUSPEND   = 5,
    HOST_OFFLOAD_REASON_PROTO_ERR = 6,
} host_offload_reason_t;

#define HOST_OFFLOAD_PROTO_VER       1
#define HOST_OFFLOAD_PING_TIMEOUT_MS 2000

void                  host_offload_init(void);
void                  host_offload_task(void);
bool                  host_offload_is_host_mode(void);
host_offload_reason_t host_offload_last_reason(void);

bool host_offload_on_mode_enter(uint8_t proto_ver, uint8_t flags);
void host_offload_on_mode_leave(void);
void host_offload_on_ping(uint32_t host_time_ms);
void host_offload_force_local(host_offload_reason_t reason);
```

**Step 1.4: Write minimal implementation**

`host_offload.c`:
```c
#include "host_offload.h"
#include "timer.h"

extern void clear_keyboard(void);

static host_offload_mode_t   s_mode;
static host_offload_reason_t s_last_reason;
static uint32_t              s_last_ping_ms;

static void set_mode(host_offload_mode_t m, host_offload_reason_t reason) {
    if (s_mode == m) { s_last_reason = reason; return; }
    s_mode = m;
    s_last_reason = reason;
    clear_keyboard();
    s_last_ping_ms = timer_read32();
}

void host_offload_init(void) {
    s_mode = HOST_OFFLOAD_LOCAL;
    s_last_reason = HOST_OFFLOAD_REASON_NONE;
    s_last_ping_ms = 0;
}

bool host_offload_is_host_mode(void)             { return s_mode == HOST_OFFLOAD_HOST; }
host_offload_reason_t host_offload_last_reason(void) { return s_last_reason; }

bool host_offload_on_mode_enter(uint8_t proto_ver, uint8_t flags) {
    (void)flags;
    if (proto_ver != HOST_OFFLOAD_PROTO_VER) return false;
    set_mode(HOST_OFFLOAD_HOST, HOST_OFFLOAD_REASON_ENTER);
    return true;
}

void host_offload_on_mode_leave(void) {
    set_mode(HOST_OFFLOAD_LOCAL, HOST_OFFLOAD_REASON_LEAVE);
}

void host_offload_on_ping(uint32_t host_time_ms) {
    (void)host_time_ms;
    s_last_ping_ms = timer_read32();
}

void host_offload_force_local(host_offload_reason_t reason) {
    set_mode(HOST_OFFLOAD_LOCAL, reason);
}

void host_offload_task(void) {
    if (s_mode != HOST_OFFLOAD_HOST) return;
    if ((timer_read32() - s_last_ping_ms) > HOST_OFFLOAD_PING_TIMEOUT_MS) {
        set_mode(HOST_OFFLOAD_LOCAL, HOST_OFFLOAD_REASON_WATCHDOG);
    }
}
```

**Step 1.5: Run tests, verify green**

```
make -f tests/host_offload/rules.mk test
```
Expected: 7 tests pass.

**Step 1.6: Commit**

```bash
git add keyboards/keychron/common/host_offload/ tests/host_offload/
git commit -m "feat(host_offload): mode state machine with ping watchdog"
```

---

### Task 2: Edge ring buffer

**Files:**
- Modify: `keyboards/keychron/common/host_offload/host_offload.h`
- Modify: `keyboards/keychron/common/host_offload/host_offload.c`
- Create: `tests/host_offload/test_edge_queue.c`

**Step 2.1: Write failing queue tests**

Test list:
1. `queue_init_empty` — after init, `host_offload_edge_dequeue(&e)` returns false.
2. `enqueue_then_dequeue_round_trips` — enqueue `(row=1,col=2,time=100,type=0,pressed=1)`, dequeue, bytes identical.
3. `fifo_order` — enqueue 4, dequeue 4, order preserved.
4. `overflow_forces_local` — enqueue `KEY_EDGE_QUEUE_LEN + 1` edges while in HOST mode; expect mode flips to LOCAL with reason `OVERFLOW` and queue is reset.
5. `emit_api_noop_in_local` — in LOCAL, `host_offload_emit_key_edge()` does not enqueue.

**Step 2.2: Run, verify fail.** Expected: "undeclared" errors for new APIs.

**Step 2.3: Add API**

Append to header:
```c
#define HOST_OFFLOAD_KEY_EDGE_QUEUE_LEN 32

typedef struct __attribute__((packed)) {
    uint8_t  col;
    uint8_t  row;
    uint16_t time;
    uint8_t  type;     // 0=key, 1=enc_cw, 2=enc_ccw, 3=dipsw
    uint8_t  pressed;
} host_offload_edge_t;

void host_offload_emit_key_edge(uint8_t row, uint8_t col, bool pressed, uint16_t time);
void host_offload_emit_encoder (uint8_t index, bool clockwise, uint16_t time);
void host_offload_emit_dip     (uint8_t index, bool active,    uint16_t time);
bool host_offload_edge_dequeue(host_offload_edge_t *out);
```

Append to impl: static ring buffer, single-producer/single-consumer, overflow detection calls `host_offload_force_local(OVERFLOW)` and empties the queue. All three `emit_*` bail early if not `is_host_mode()`.

**Step 2.4: Run tests, verify green.**

**Step 2.5: Commit**

```bash
git commit -am "feat(host_offload): edge ring buffer + emit API"
```

---

### Task 3: Intercept hook for record chain

**Files:**
- Modify: `keyboards/keychron/common/host_offload/host_offload.h`
- Modify: `keyboards/keychron/common/host_offload/host_offload.c`
- Create: `tests/host_offload/test_intercept.c`

**Step 3.1: Tests**

1. `intercept_local_returns_true` — `host_offload_intercept_record(KC_A, &rec)` returns true in LOCAL (keep normal processing).
2. `intercept_host_returns_false` — returns false in HOST (short-circuit).
3. `intercept_host_increments_counter` — counter exposed via `host_offload_stats_intercepted()`.

**Step 3.2–3.5:** Standard red-green. Add:

```c
// header
bool    host_offload_intercept_record(uint16_t keycode, void *record);
uint32_t host_offload_stats_intercepted(void);
```

Implementation just checks `is_host_mode()`, increments counter, returns `!is_host_mode()`. (We don't need the record struct for v1; the `void*` parameter is future-proofing.)

**Step 3.6: Commit**

```bash
git commit -am "feat(host_offload): record-chain intercept hook"
```

---

### Task 4: Suspend hook

**Files:**
- Modify: `host_offload.c/h`
- Modify: `tests/host_offload/test_host_offload_mode.c`

**Step 4.1: Test** — `suspend_forces_local`: enter HOST, call `host_offload_on_suspend()`, expect LOCAL with reason `SUSPEND`.

**Step 4.2–4.5:** Add one-liner `void host_offload_on_suspend(void) { host_offload_force_local(HOST_OFFLOAD_REASON_SUSPEND); }`.

**Step 4.6: Commit**

```bash
git commit -am "feat(host_offload): suspend forces LOCAL mode"
```

---

## M2 — Firmware edge emission

### Task 5: Wire edge emitter into matrix diff loop

Q3 Max uses `matrix_scan_custom()` + the core `matrix_task()` in `quantum/keyboard.c`. The per-edge `action_exec(MAKE_KEYEVENT(...))` call is inside `matrix_task()`. We cannot patch `quantum/`. Solution: hook at `process_record_kb()` entry (first thing called for every edge) so we see every edge the matrix loop would have processed, emit, and short-circuit.

**Files:**
- Modify: `keyboards/keychron/common/keychron_task.c:145-151`
- No new tests — covered by Task 3 + later E2E.

**Step 5.1: Read current `process_record_kb`:**

```c
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record)) return false;
    if (!process_record_keychron(keycode, record)) return false;
    return true;
}
```

**Step 5.2: Modify to:**

```c
#ifdef HOST_OFFLOAD_ENABLE
#include "host_offload.h"
#endif

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef HOST_OFFLOAD_ENABLE
    if (host_offload_is_host_mode()) {
        host_offload_emit_key_edge(
            record->event.key.row,
            record->event.key.col,
            record->event.pressed,
            record->event.time);
        return false;  // suppress local processing
    }
#endif
    if (!process_record_user(keycode, record)) return false;
    if (!process_record_keychron(keycode, record)) return false;
    return true;
}
```

**Step 5.3:** Nothing to run locally — this is verified by Task 10 build + Task 20 HIL.

**Step 5.4: Commit**

```bash
git commit -am "feat(host_offload): emit + short-circuit in process_record_kb"
```

---

### Task 6: Encoder emission

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c` (add `encoder_update_kb` override or use existing hook).
- Actually: `keyboards/keychron/common/keychron_common.c` has encoder handling — confirm by grepping.

**Step 6.1:** Grep `grep -rn "encoder_update_kb\|encoder_update_user" keyboards/keychron`.

**Step 6.2:** Add the emit+short-circuit in the earliest keyboard-level encoder hook. If no keyboard-level hook exists, add `encoder_update_kb(uint8_t index, bool clockwise)` in `keychron_task.c` adjacent to `process_record_kb` with the same `#ifdef HOST_OFFLOAD_ENABLE` pattern, returning `false` in HOST (which tells QMK not to run the default encoder_map).

**Step 6.3: Commit**

```bash
git commit -am "feat(host_offload): encoder tick emission"
```

---

### Task 7: Dip-switch emission

**Files:**
- Modify: `keyboards/keychron/q3_max/q3_max.c:24-33` (`dip_switch_update_kb`).

**Step 7.1:**

```c
bool dip_switch_update_kb(uint8_t index, bool active) {
#ifdef HOST_OFFLOAD_ENABLE
    if (host_offload_is_host_mode()) {
        host_offload_emit_dip(index, active, timer_read());
        return false;
    }
#endif
    if (dip_switch_update_user(index, active)) return true;
    if (index == 0) default_layer_set(1UL << (active ? 2 : 0));
    return true;
}
```

**Step 7.2: Commit**

```bash
git commit -am "feat(host_offload): dip-switch change emission"
```

---

## M3 — Firmware HID inject

### Task 8: HID inject dispatcher

**Files:**
- Modify: `host_offload.h/c`
- Create: `tests/host_offload/test_hid_inject.c`

**Step 8.1: Tests** — for each `report_kind` (keyboard 6KRO, nkro, consumer, system, mouse), feed a known byte blob and assert the correct `host_*_send` mock was called with the expected bytes. Also: in LOCAL mode, inject is ignored (counter only). Malformed length → rejected, `stats_inject_errors` incremented.

**Step 8.2–8.4:** Add:

```c
// header
typedef enum {
    HOST_OFFLOAD_REPORT_KEYBOARD_6KRO = 0x01,
    HOST_OFFLOAD_REPORT_KEYBOARD_NKRO = 0x02,
    HOST_OFFLOAD_REPORT_CONSUMER      = 0x03,
    HOST_OFFLOAD_REPORT_SYSTEM        = 0x04,
    HOST_OFFLOAD_REPORT_MOUSE         = 0x05,
} host_offload_report_kind_t;

void     host_offload_on_hid_inject(uint8_t kind, const uint8_t *payload, uint8_t len);
uint32_t host_offload_stats_inject_ok(void);
uint32_t host_offload_stats_inject_err(void);
```

Implementation validates length against the corresponding struct size, memcpys into a local struct, calls the corresponding `host_*_send`. Reject when `!is_host_mode()`.

**Step 8.5: Commit**

```bash
git commit -am "feat(host_offload): HID inject dispatcher for all report kinds"
```

---

### Task 9: Suspend hook into USB driver

**Files:**
- Modify: `keyboards/keychron/common/host_offload/host_offload.c` — expose `host_offload_on_suspend()` from a weak `suspend_power_down_kb` or `housekeeping_task_kb` polling `USB_DRIVER.state`.
- Prefer: hook into `suspend_power_down_kb()` (QMK weak hook fired on USB suspend). Add it in `keychron_task.c`.

**Step 9.1:** Add (in `keychron_task.c`):

```c
#ifdef HOST_OFFLOAD_ENABLE
void suspend_power_down_kb(void) {
    host_offload_on_suspend();
    suspend_power_down_user();
}
#endif
```

**Step 9.2: Commit**

```bash
git commit -am "feat(host_offload): force LOCAL on USB suspend"
```

---

## M4 — Firmware integration + build

### Task 10: Sysex command IDs + handler wiring

**Files:**
- Modify: `keyboards/keychron/q3_max/qmkata_sysex_handler.c` (add new IDs and dispatch).
- Look for the existing `enum QMKATA_ID_*` (likely in `QMKata.h` in the qmkata subdir) — grep first and pick the next free ID values. Document chosen values in the header right beside the existing ones.

**Step 10.1:** Find existing ID enum:
```
grep -rn "QMKATA_ID_" keyboards/keychron/qmkata/ keyboards/keychron/q3_max/
```
Append new IDs:
- `QMKATA_ID_HOST_OFFLOAD`     (used with SET subcommand 1=ENTER, 2=LEAVE, 3=PING)
- `QMKATA_ID_HID_REPORT_INJECT` (SET)
- `QMKATA_ID_KEYPRESS_EVENT`   (PUB) — **may already exist**; confirm.
- `QMKATA_ID_HOST_OFFLOAD_STATUS` (PUB)

**Step 10.2:** Wire into `qmkata_sysex_handler()` (line 159 of `qmkata_sysex_handler.c`):

```c
#ifdef HOST_OFFLOAD_ENABLE
    if (cmd == QMKATA_CMD_SET) {
        if (id == QMKATA_ID_HOST_OFFLOAD)     _qmkata_handle_host_offload_cmd(seqnum, buf, len);
        if (id == QMKATA_ID_HID_REPORT_INJECT) _qmkata_handle_hid_inject(seqnum, buf, len);
    }
#endif
```

Implement `_qmkata_handle_host_offload_cmd()` to route subcommand byte to `host_offload_on_mode_enter / leave / ping`, and send back an `ID_HOST_OFFLOAD_STATUS` via `qmkata_send_sysex(QMKATA_CMD_RESPONSE, ...)`.

Implement `_qmkata_handle_hid_inject()` to extract `kind` and call `host_offload_on_hid_inject()`.

**Step 10.3:** Add a new PUB emitter `_qmkata_pub_key_edge(const host_offload_edge_t *e)` that packs into the existing 7-byte wire format (`col, row, u16 time, type, pressed`) and calls `qmkata_send_sysex(QMKATA_CMD_PUB, ..., QMKATA_ID_KEYPRESS_EVENT)`.

**Step 10.4:** Add `host_offload_flush_edges()` (new API in header) that drains the ring with `host_offload_edge_dequeue()` and publishes each. Call it from `housekeeping_task_kb` (`keychron_task.c`) and also from a new `host_offload_task()` tick.

**Step 10.5:** Commit
```bash
git commit -am "feat(host_offload): QMKata sysex command wiring"
```

---

### Task 11: Build flag

**Files:**
- Modify: `keyboards/keychron/q3_max/rules.mk`
- Modify: `keyboards/keychron/common/common_features.mk` (if that's where feature opt-ins live; grep first).
- Create: `keyboards/keychron/common/host_offload/rules.mk`

**Step 11.1:** In `rules.mk`:
```make
HOST_OFFLOAD_ENABLE ?= yes
ifeq ($(strip $(HOST_OFFLOAD_ENABLE)), yes)
    OPT_DEFS += -DHOST_OFFLOAD_ENABLE
    SRC += $(KEYBOARD_PATH_4)/common/host_offload/host_offload.c
endif
```

(Use the correct `KEYBOARD_PATH_N` — grep an existing Keychron common include to see the convention.)

**Step 11.2:** Build both ways:
```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron -e HOST_OFFLOAD_ENABLE=no
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron -e HOST_OFFLOAD_ENABLE=yes
```
Expected: both succeed. Record binary size delta in the commit message.

**Step 11.3: Commit**
```bash
git commit -am "build(q3_max): add HOST_OFFLOAD_ENABLE flag (default yes)"
```

---

### Task 12: Integrate `host_offload_task` + init

**Files:**
- Modify: `keyboards/keychron/common/keychron_task.c:128-143` — add `host_offload_task()` call.
- Modify: `keyboards/keychron/q3_max/q3_max_user.c` (or wherever `keyboard_post_init_user()` lives) — add `host_offload_init()`.

**Step 12.1:** Grep `keyboard_post_init_user` under `keyboards/keychron/q3_max/` to find the right file.

**Step 12.2:** Add:
```c
#ifdef HOST_OFFLOAD_ENABLE
    host_offload_init();
#endif
```

And in `keychron_task()`:
```c
#ifdef HOST_OFFLOAD_ENABLE
    host_offload_task();
    host_offload_flush_edges();
#endif
```

**Step 12.3:** Rebuild. Flash to a spare Q3 Max if available. At minimum: boot, confirm the keyboard types normally (LOCAL mode untouched).

**Step 12.4: Commit**
```bash
git commit -am "feat(host_offload): init + task hook on q3_max"
```

---

## M5 — Host protocol client

Host changes live in `/home/user/qmk/qmk-tools/qmk/QMKata/` — separate repo. Open it alongside; commits there are independent from the firmware repo.

### Task 13: Extend `QMKataKeyboard` with protocol IDs

**Files:**
- Modify: `/home/user/qmk/qmk-tools/qmk/QMKata/QMKataKeyboard.py`

**Step 13.1:** Add new constants to `class QMKataKeybCmd` — use the exact numeric values chosen in Task 10. Match order.

**Step 13.2:** In `sysex_pub_handler`, extend the `ID_KEYPRESS_EVENT` handler to also dispatch `type == 1/2/3` to new stubs `key_machine.encoder_event(idx, cw, time)` and `key_machine.dip_event(idx, active, time)`.

**Step 13.3:** Add new method `host_offload_enter(self, proto_ver=1, flags=0)` that sends `QMKATA_CMD_SET, ID_HOST_OFFLOAD, subcmd=1, proto_ver, flags`; `host_offload_leave()`; `host_offload_ping()`; `send_hid_report(kind, payload: bytes)`.

**Step 13.4:** Add a `ID_HOST_OFFLOAD_STATUS` response handler in `sysex_response_handler` that updates `self.host_offload_mode` and `self.host_offload_last_reason`.

**Step 13.5:** Commit (in qmk-tools repo):
```bash
git -C /home/user/qmk/qmk-tools add qmk/QMKata/QMKataKeyboard.py
git -C /home/user/qmk/qmk-tools commit -m "feat(qmkata): host-offload sysex client"
```

---

### Task 14: Ping timer

**Files:**
- Modify: `QMKataKeyboard.py`

**Step 14.1:** Start a QTimer (500 ms) that calls `host_offload_ping()` whenever `self.host_offload_mode == HOST`. Stop on LEAVE.

**Step 14.2: Commit**

---

### Task 15: Stub KeyMachine event APIs

**Files:**
- Modify: `KeyMachine.py`

**Step 15.1:** Add `encoder_event(idx, cw, time)` and `dip_event(idx, active, time)` that just log for now. The real engine is Task 16–19.

**Step 15.2: Commit**

---

## M6 — Host action engine (minimal)

Scope the first version to: layers (default + momentary MO), basic keycodes, modifier merging, NKRO report. No tap-hold, no combo/tap-dance/leader in v1 — those are follow-ups. This keeps the first green-light small.

### Task 16: `HidReportBuilder`

**Files:**
- Create: `/home/user/qmk/qmk-tools/qmk/QMKata/HidReportBuilder.py`
- Create: `/home/user/qmk/qmk-tools/qmk/QMKata/test_hid_report_builder.py`

**Step 16.1: Tests** covering: register_code adds to 6KRO keys + updates mods byte for modifier keycodes; unregister_code removes; rollover overflow triggers NKRO report; reset() clears all.

**Step 16.2:** Implement `HidReportBuilder` with `register_code(code)`, `unregister_code(code)`, `host_consumer(usage)`, `host_system(usage)`, `clear_keyboard()`, `build_keyboard_report() -> (kind, bytes)`. Follows QMK `report.c` rules (look at `tmk_core/protocol/report.c` for reference but reimplement in Python).

**Step 16.3: Commit**

---

### Task 17: `ActionEngine` skeleton

**Files:**
- Create: `ActionEngine.py`
- Create: `test_action_engine.py`

**Step 17.1: Tests:**
1. Single layer 0 plain key press → release → 2 reports (key in, key out).
2. MO(1) held, access layer-1 keycode, release MO → layer reverts.
3. Modifier keycode merges into mods byte.
4. Unknown keycode at position → no report.

**Step 17.2:** Implement `ActionEngine(keymap, report_builder)` with `on_key_edge(row, col, pressed, time)` that does layer resolution (default | active), looks up `keymap[layer][row][col]`, and calls report_builder.

Keymap source for v1: hardcoded in tests; real daemon pulls from keyboard via existing `STATUS` / dynamic-keymap sysex (Task 21).

**Step 17.3: Commit**

---

### Task 18: Wire engine into `KeyMachine`

**Step 18.1:** In `KeyMachine.__init__`, construct `HidReportBuilder` + `ActionEngine`. `key_event()` now delegates to `engine.on_key_edge()` and calls `self.keyb.send_hid_report(*builder.build_keyboard_report())` after each edge.

**Step 18.2:** Commit.

---

### Task 19: QMKata UI tab

**Files:**
- Create: `HostOffloadTab.py`
- Modify: `QMKata.py`

**Step 19.1:** Minimal tab: "Enter HOST", "Leave HOST" buttons, status label, edge counter, inject counter. No fancy histograms yet.

**Step 19.2:** Commit.

---

## M7 — End-to-end bring-up

### Task 20: HIL smoke test

On a physical Q3 Max flashed with Task 12 firmware + qmk-tools with Task 19 daemon:

**Step 20.1:** Launch QMKata, confirm firmware version reports HOST_OFFLOAD capability.

**Step 20.2:** Click Enter HOST. Status goes to HOST.

**Step 20.3:** Press `A` — expect exactly one matrix edge received, one HID report emitted. Verify OS sees "a".

**Step 20.4:** Press `Shift+A` — expect "A".

**Step 20.5:** Kill the daemon — firmware should return to LOCAL within 2 s; keyboard still types as before.

**Step 20.6:** Write a short test report in `docs/plans/2026-04-24-host-offload-keyproc-bringup.md` with observed behaviour and any anomalies.

**Step 20.7:** Commit the test report.

---

### Task 21: Dynamic keymap pull

**Step 21.1:** On Enter HOST, have `KeyMachine` read the current VIA dynamic keymap from the keyboard via existing QMKata sysex (see `qmkata_sysex_handler.c:369` — dynamic-keymap read path) and feed it into `ActionEngine`.

**Step 21.2:** Verify the user's VIA customizations are honoured in HOST mode.

**Step 21.3:** Commit.

---

### Task 22: Latency measurement

**Step 22.1:** Add a `timer_read()`-based round-trip marker to the ping path. Daemon logs min/median/p95/max round-trip. Target: p95 < 5 ms.

**Step 22.2:** If p95 > 5 ms, do NOT paper over it — open a debug task and use @superpowers:systematic-debugging. The likely culprits are QMKata sysex batching delays or pyfirmata2 sampler-thread wakeups; investigate before optimizing.

**Step 22.3:** Commit measurement report.

---

## Deferred to follow-up plans

Explicitly out of scope for this plan (tracked as separate future plans):

- Tap-hold, tap-dance, combo, leader engines on host (v2).
- Keychron custom keycodes (KC_LOPTN/Siri/TASK/FILE/SNAP/WLCK/MLCK) on host.
- Wireless (BT/2.4G) support.
- macOS / Linux packaging of the daemon.
- RGB-per-layer driven from host.

---

## DRY / YAGNI / TDD reminders

- Do not reimplement `quantum/action_*.c` on the firmware side. The whole point is to *skip* it in HOST mode.
- Do not expose new USB interfaces. Everything rides existing Raw HID.
- Do not persist HOST mode in EEPROM. Every boot starts LOCAL.
- Do not add "nice to have" features inside this plan. Land the pipeline first; semantics later.
- Every task ends with a green test run AND a commit. If you can't make something pass, stop and invoke @superpowers:systematic-debugging.
