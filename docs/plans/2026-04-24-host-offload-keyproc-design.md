# Host-Offload Key Processing — Design

**Date:** 2026-04-24
**Target:** Keychron Q3 Max (ANSI Encoder variant); architecture extensible to other Keychron boards
**Firmware base:** this branch (`feat/combo-modules`)
**Host daemon:** [QMKata](file:///home/user/qmk/qmk-tools/qmk/QMKata)

---

## 1. Purpose

Move QMK's key-event processing (action dispatch, layers, tap-hold, combos, tap dance, leader, macros, keychron custom keycodes, encoder/dip-switch mapping) from the MCU to the QMKata host daemon. The keyboard becomes:

- an input **source** — emits debounced per-key edges, encoder ticks, dip-switch changes;
- an HID **transport** — accepts fully-resolved HID reports from the host and forwards them to USB/BT HID endpoints.

All QMK semantics live on the host.

Reference of the current on-MCU pipeline we are offloading: `docs/plans/2026-04-16-q3-max-key-processing-design-final.md`.

---

## 2. Goals / Non-goals

**Goals**

- Run the full QMK feature set from the host (action engine, layers, tap-hold, combos, tap dance, leader, macros, custom keychron keycodes, OS/Siri/TASK combos).
- Keyboard still works standalone (BIOS, boot, OS without daemon, daemon crash) via the existing QMK keymap as a **local fallback**.
- Runtime mode switch between LOCAL (existing QMK behaviour) and HOST (dumb source + HID transport).
- Single USB device, no kernel drivers, no virtual-HID on host. BIOS-friendly, games/remote-desktop friendly.
- Reuse existing QMKata Raw HID + sysex plumbing and pyfirmata2 framing.

**Non-goals**

- No wireless support in v1 (BT/2.4G paths stay LOCAL-only; daemon only activates over USB). Design must not preclude later wireless support.
- No new HID descriptors, no new USB endpoints — everything rides existing Raw HID.
- No per-app/scripting features in v1; KeyMachine on host is the extension point once the plumbing is done.
- No reimplementation of QMK's action engine on the host from scratch — KeyMachine/QMKata owns the semantics, using whatever primitives it already has (`KeyMachine.py`) plus the new event stream.

---

## 3. Architecture

### 3.1 Two runtime modes, one firmware image

```
┌──────────────────────── FIRMWARE (STM32F401 / ChibiOS) ────────────────────────┐
│                                                                                │
│  matrix_scan_custom ─► debounce ─► cooked matrix[]                             │
│                                         │                                      │
│                            diff vs previous matrix                             │
│                                         │                                      │
│             ┌───────────────────────────┼─────────────────────────────┐        │
│             │                           │                             │        │
│             ▼                           ▼                             ▼        │
│       LOCAL MODE                  HOST MODE                     always         │
│    action_exec()              qmkata_emit_key_edge()     switch_events() for   │
│    process_record chain       (col,row,time,pressed)       RGB/LED matrix      │
│    layers, tap-hold, …              │                                          │
│    register_code,…                  │                                          │
│         │                           ▼                                          │
│         │                  ┌─────────────────────┐                             │
│         │                  │ QMKata sysex TX      │ ── ID_KEYPRESS_EVENT ──┐   │
│         │                  │ (Raw HID)            │ ── ID_ENCODER_EVENT ───┤   │
│         │                  │                      │ ── ID_DIPSW_EVENT  ────┤   │
│         │                  └─────────────────────┘                        │   │
│         │                                                                 │   │
│         │                  ┌─────────────────────┐                        │   │
│         │                  │ QMKata sysex RX      │ ◄── ID_HID_REPORT ────┤   │
│         │                  │ (Raw HID)            │       (new)           │   │
│         │                  └─────────────────────┘                        │   │
│         │                                │                                │   │
│         │                   host_keyboard_send / host_nkro_send /         │   │
│         │                   host_consumer_send / host_system_send /       │   │
│         │                   host_mouse_send                               │   │
│         ▼                                │                                │   │
│  ────────────────── USB HID keyboard/mouse/consumer endpoints ───────►    │   │
│                                                                      OS ◄─┘   │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
                                  ▲      │
                        Raw HID IN│      │Raw HID OUT
                                  │      ▼
                 ┌──────────────────────────────────────────┐
                 │         QMKata daemon (host)             │
                 │                                          │
                 │  QMKataKeyboard.sysex_pub_handler        │
                 │       │                                  │
                 │       ▼                                  │
                 │  KeyMachine + action engine              │
                 │  (layers, tap-hold, combos, tap-dance,   │
                 │   leader, macros, keychron keycodes,     │
                 │   encoder/dip-switch map)                │
                 │       │                                  │
                 │       ▼                                  │
                 │  Resolved HID reports                    │
                 │  ─► sysex TX  ID_HID_REPORT_INJECT       │
                 └──────────────────────────────────────────┘
```

### 3.2 Modes

| Mode    | Keyboard-side action engine | Matrix edge events to host | HID reports from host |
|---------|-----------------------------|-----------------------------|------------------------|
| LOCAL   | **runs**                    | **not sent**                | **ignored** (rejected) |
| HOST    | **suppressed**              | **sent**                    | **forwarded to HID**  |

Mode switches at runtime only — a single firmware image supports both. Default at boot: **LOCAL**. Entering HOST requires the host to explicitly request it after a handshake.

### 3.3 Mode transitions

```
                 boot
                  │
                  ▼
             ┌─────────┐  ID_HOST_MODE_ENTER (after handshake OK)
             │  LOCAL  │ ───────────────────────────────────────►┐
             │         │                                         │
             │ QMK     │◄─ ID_HOST_MODE_LEAVE ───────────────────┤
             │ keymap  │◄─ host watchdog timeout (no ping for T) ┤
             │ active  │◄─ USB suspend / bus reset               │
             └─────────┘                                         │
                                                                 ▼
                                                         ┌─────────────┐
                                                         │    HOST     │
                                                         │             │
                                                         │ local action│
                                                         │ engine off  │
                                                         │ edges→host  │
                                                         │ HID←host    │
                                                         └─────────────┘
```

Guarantees:

- Handshake is **required** before HOST is allowed. Prevents a stale/foreign Raw HID writer from accidentally muting the keyboard.
- **Watchdog:** host must send a keep-alive ping every `HOST_MODE_PING_MS` (default 1000 ms). Miss → firmware returns to LOCAL and flushes all currently-held keys (`clear_keyboard()`), to avoid stuck modifiers.
- **USB suspend / bus reset / BT switch** → forced exit to LOCAL.
- Mode is **not persisted** in EEPROM. Always starts LOCAL.

### 3.4 Fallback semantics in practice

Because LOCAL uses the existing QMK keymap (including VIA dynamic keymap, debounce, encoder map, tap dance, combos, leader), the user gets a fully-working keyboard at all times without the daemon. HOST mode is an *enhancement*, not a requirement.

---

## 4. Protocol

Rides existing QMKata sysex framing over Raw HID (already used for VIA-coexistent telemetry). Multibyte fields little-endian.

### 4.1 New sysex command IDs (extensions to `QMKataKeybCmd`)

| Name                     | Dir  | ID (proposed) | Payload (after seqnum/id) |
|--------------------------|------|---------------|---------------------------|
| `ID_HOST_MODE_ENTER`     | h→kb | new           | `uint8_t proto_ver` (1) + `uint8_t flags` |
| `ID_HOST_MODE_LEAVE`     | h→kb | new           | — |
| `ID_HOST_MODE_PING`      | h→kb | new           | `uint32_t host_time_ms` |
| `ID_HOST_MODE_STATUS`    | kb→h | new (PUB)     | `uint8_t mode` + `uint8_t last_reason` |
| `ID_KEYPRESS_EVENT`      | kb→h | **existing**  | `u8 col, u8 row, u16 time, u8 type, u8 pressed` — type=`KEY`/`ENCODER_CW`/`ENCODER_CCW`/`DIPSW` |
| `ID_HID_REPORT_INJECT`   | h→kb | new           | `u8 report_kind` + raw report bytes |

`report_kind` values:

| Value | Meaning                | Payload                                                 |
|-------|------------------------|---------------------------------------------------------|
| 0x01  | KEYBOARD 6KRO          | 8 bytes (report_id, mods, reserved, keys[6])            |
| 0x02  | KEYBOARD NKRO          | sizeof(`report_nkro_t`)                                 |
| 0x03  | CONSUMER               | `uint16_t usage`                                        |
| 0x04  | SYSTEM                 | `uint16_t usage`                                        |
| 0x05  | MOUSE                  | sizeof(`report_mouse_t`)                                |

Reports are **opaque to firmware**: the host builds them exactly as QMK would and firmware writes them to the corresponding endpoint via `host_keyboard_send / host_nkro_send / host_consumer_send / host_system_send / host_mouse_send`.

### 4.2 Extension of `ID_KEYPRESS_EVENT`

Current firmware only stores a snapshot (`STATUS_ID_MATRIX`) and there is no per-edge publisher. We need to publish per-edge events. The existing on-host parser already expects the fields above (`QMKataKeyboard.py:786`), so the wire format is preserved; we only add a **`type` discriminator** at byte offset 5 to also carry encoder and dip-switch events on the same command ID. Existing keypress events keep `type = 0` for back-compat.

Values:

| `type` | Meaning                 | Notes                                      |
|--------|-------------------------|--------------------------------------------|
| 0      | matrix key edge         | `pressed` = 0/1                            |
| 1      | encoder CW tick         | `col` = encoder index, `row` = 0           |
| 2      | encoder CCW tick        | `col` = encoder index, `row` = 0           |
| 3      | dip-switch change       | `col` = switch index, `pressed` = active   |

### 4.3 Handshake

1. Host opens Raw HID, sends existing `REPORT_FIRMWARE` → firmware replies with `qmkata version/firmware_version` (already implemented).
2. Host checks firmware version advertises capability bit `HOST_OFFLOAD` (new bit in the existing firmware status / features response).
3. Host sends `ID_HOST_MODE_ENTER proto_ver=1`. Firmware answers `ID_HOST_MODE_STATUS mode=HOST`.
4. Host begins pinging; firmware begins publishing edges.

### 4.4 Flow-control / back-pressure

Raw HID IN pipe is single-producer; edges are queued in a small ring (capacity `KEY_EDGE_QUEUE_LEN`, default 32 edges). On overflow firmware forces `LOCAL` mode and publishes `ID_HOST_MODE_STATUS last_reason=OVERFLOW` so the daemon can reset.

---

## 5. Firmware changes (summary)

Code organisation stays within the existing QMKata tree under `keyboards/keychron/q3_max/` and the shared `keyboards/keychron/common/`. No changes to `quantum/` core.

### 5.1 New module: `host_offload`

Single pair of files under `keyboards/keychron/common/host_offload/`:

- `host_offload.c/h` — mode state machine, edge emitter, HID inject dispatcher, ping watchdog.

Public API (called from existing hooks):

```c
typedef enum { HOST_OFFLOAD_LOCAL = 0, HOST_OFFLOAD_HOST = 1 } host_offload_mode_t;

bool     host_offload_is_host_mode(void);                                // fast path
void     host_offload_task(void);                                        // watchdog, called from housekeeping_task_kb()

// Emit hooks (called from matrix/encoder/dip paths)
void     host_offload_emit_key_edge(uint8_t row, uint8_t col, bool pressed, uint16_t time);
void     host_offload_emit_encoder(uint8_t index, bool clockwise, uint16_t time);
void     host_offload_emit_dip(uint8_t index, bool active, uint16_t time);

// Called from process_record_quantum() — returns true to short-circuit normal processing.
bool     host_offload_intercept_record(uint16_t keycode, keyrecord_t *record);

// Sysex handler entry points (wired into qmkata_sysex_handler.c)
void     host_offload_sysex_mode_enter(uint8_t seqnum, const uint8_t *buf, uint8_t len);
void     host_offload_sysex_mode_leave(uint8_t seqnum);
void     host_offload_sysex_ping(uint8_t seqnum, const uint8_t *buf, uint8_t len);
void     host_offload_sysex_hid_inject(uint8_t seqnum, const uint8_t *buf, uint8_t len);
```

### 5.2 Integration points

- `keyboards/keychron/common/matrix.c`: after the existing diff loop, call `host_offload_emit_key_edge()` on each changed bit. Reuse the same loop that currently feeds `action_exec()` / `switch_events()`.
- `quantum/process_record_quantum` chain entry (`process_record_kb` in `keychron_task.c`): first call is `host_offload_intercept_record()`; if HOST mode, return `false` to short-circuit the entire chain. This is a single 2-line change.
- `encoder_task` user hook: emit CW/CCW event; if HOST, return `false` so no local action.
- `dip_switch_update_kb`: emit dip event; if HOST, skip the default-layer toggle.
- `qmkata_sysex_handler.c`: add four new case entries in the sysex switch for the new command IDs; add the `HOST_OFFLOAD` capability bit to the existing status/features response.
- `housekeeping_task_kb()`: call `host_offload_task()` for the watchdog.

Nothing in `quantum/` is modified. All behaviour changes sit behind `host_offload_is_host_mode()` early-outs, so LOCAL mode is byte-for-byte identical to today's build (including `register_code16()` override, Q3-Max-specific hooks, etc.).

### 5.3 HID inject

`host_offload_sysex_hid_inject()` validates `report_kind` + length, then calls the matching `host_*_send` function with the payload memcpy'd into the corresponding `report_*_t` struct. No keycode translation, no modifier manipulation — wire bytes go straight to the endpoint.

The host is responsible for producing valid, debounced, rollover-correct reports. Firmware will not second-guess (no mod-merging, no NKRO/6KRO transcoding, no OS-specific tweaks).

### 5.4 Safety nets

- On LOCAL→HOST transition: firmware calls `clear_keyboard()` so no stuck keys leak across modes.
- On HOST→LOCAL transition (any reason): firmware calls `clear_keyboard()`, then the next matrix scan regenerates any still-pressed keys through the normal pipeline.
- While HOST, the edge emitter **still runs `switch_events()`** so RGB matrix reactive effects continue to work locally.
- USB suspend handler forces LOCAL before the suspend path completes.

### 5.5 Flash/RAM budget (estimate)

- Code: ~1.5 KB (`host_offload.c` + sysex glue).
- RAM: `KEY_EDGE_QUEUE_LEN * 6 B` = 192 B for edge ring + ~32 B state.

Well within STM32F401 headroom documented in the reference doc §14.3.

---

## 6. Host changes (summary)

### 6.1 `QMKataKeyboard.py`

- Add enum entries for the new command IDs in `QMKataKeybCmd`.
- Extend `sysex_pub_handler()` `ID_KEYPRESS_EVENT` branch to dispatch on `type` (matrix / encoder / dipsw) to `KeyMachine`.
- Add `enter_host_mode()`, `leave_host_mode()`, `ping_host_mode()` helpers; start a 500 ms ping timer while in HOST mode.
- Add `send_hid_report(kind, payload)` that wraps `ID_HID_REPORT_INJECT`.

### 6.2 `KeyMachine.py` (and new `ActionEngine.py`)

`KeyMachine` today already has combos, sequences, and a `key_event(row, col, time, pressed)` entry. For v1 we extend it, and split the QMK-like semantics into a new module:

- `ActionEngine.py` — maintains layer stack, default layer, mod state, tap-hold state machine, combo/tap-dance/leader resolution, custom keychron keycode translation (OS keys, Siri, TASK/FILE/SNAP/WLCK/MLCK combos, OS toggle/select). Exposes `on_key_edge(row, col, pressed, t)`, `on_encoder(idx, cw, t)`, `on_dip(idx, active, t)`. Output is a sequence of abstract actions (`register_code`, `layer_on/off`, `host_consumer`, `host_system`, `host_mouse`, `clear_keyboard`).
- `HidReportBuilder.py` — owns the 6KRO/NKRO mod+keys state, consumer/system one-shot usages, mouse accumulator. Translates abstract actions into wire-format reports and hands them to `QMKataKeyboard.send_hid_report(...)`.

The keymap itself is **read from the keyboard's dynamic keymap** (already exposed via QMKata `STATUS` / `DYNLD` plumbing, see `qmkata_sysex_handler.c:369`). On entering HOST mode the daemon pulls the current dynamic keymap + debounce config + combo/tap-dance/leader tables from EEPROM via existing sysex commands, so user's VIA/VIA-like customization keeps working.

### 6.3 QMKata UI

New "Host offload" tab with:

- Live status (LOCAL / HOST, last_reason, edges/sec, reports/sec).
- Manual enter/leave buttons.
- Edge log (ring buffer view).
- Latency histogram (edge-timestamp → host-receive → inject → kb-receive).

---

## 7. Data flow (host mode)

```
user presses A (row=1, col=2)
         │
  firmware matrix scan + debounce
         │
  host_offload_emit_key_edge(1, 2, pressed=1, t=42310)
         │
  qmkata_send_sysex(ID_KEYPRESS_EVENT, {col=2,row=1,time=42310,type=0,pressed=1})
         │
  USB Raw HID (≈1 ms poll)
         │
  QMKataKeyboard.sysex_pub_handler → KeyMachine.key_event → ActionEngine.on_key_edge
         │
  ActionEngine resolves: layer 0, action_for_key → KC_A
         │
  HidReportBuilder.register_code(KC_A) → updates internal keyboard report
         │
  QMKataKeyboard.send_hid_report(KEYBOARD_6KRO, {mods=0, keys=[0x04,0,0,0,0,0]})
         │
  qmkata_sysex (ID_HID_REPORT_INJECT)
         │
  USB Raw HID (≈1 ms poll)
         │
  host_offload_sysex_hid_inject → host_keyboard_send
         │
  USB HID keyboard endpoint → OS
```

### 7.1 Latency budget

| Stage                                         | Time     |
|-----------------------------------------------|----------|
| matrix scan + eager debounce                  | ~0.1 ms  |
| edge→host Raw HID poll                        | ~1 ms    |
| host action engine + report build             | ~0.1 ms  |
| host→kb Raw HID poll                          | ~1 ms    |
| HID keyboard endpoint poll                    | ~1 ms    |
| **Total press latency (median)**              | **~3.2 ms** |

Worst-case ~5 ms depending on poll alignment. Release follows debounce window as today (default 50 ms eager release); the round-trip adds the same ~3 ms.

---

## 8. Error handling

| Event                               | Firmware behaviour                            | Host behaviour                                 |
|-------------------------------------|-----------------------------------------------|------------------------------------------------|
| Daemon exits cleanly                | `ID_HOST_MODE_LEAVE` → LOCAL, clear_keyboard  | —                                              |
| Daemon crashes / SIGKILL            | Ping watchdog fires → LOCAL, clear_keyboard   | On restart, re-handshake                        |
| Edge queue overflow                 | Force LOCAL, `ID_HOST_MODE_STATUS overflow`   | Reset engine state, re-handshake                |
| Malformed `ID_HID_REPORT_INJECT`    | Drop, increment error counter, keep HOST      | —                                              |
| USB suspend / resume                | Force LOCAL on suspend; remain LOCAL on resume| Wait for `REPORT_FIRMWARE` round-trip before re-entering HOST |
| BT/2.4G switch (wireless paths)     | Force LOCAL                                   | Do not enter HOST on non-USB transport (v1)     |
| Bootloader / DFU                    | N/A                                           | —                                              |
| Stuck key across mode change        | `clear_keyboard()` on every transition        | `HidReportBuilder.reset()` on state changes     |

---

## 9. Testing strategy

### 9.1 Firmware (HIL + unit)

- **Mode state machine:** unit-test `host_offload.c` state transitions with a mocked sysex transport. Property: LOCAL is reachable from every state; `clear_keyboard()` is called on every entry/exit transition.
- **HID inject:** unit-test `host_offload_sysex_hid_inject()` dispatches each `report_kind` to the correct `host_*_send` stub.
- **Edge emitter:** unit-test matrix-diff loop calls `emit_key_edge` exactly once per changed bit, in LOCAL and HOST; in LOCAL no edges are emitted.
- **HIL:** bench test on physical Q3 Max — press each key in HOST mode, verify host receives exactly one press edge + one release edge per physical event, no duplicates, no drops.

### 9.2 Host

- `ActionEngine` unit tests: cover layers, tap-hold timings, combos, tap dance, leader — each test is an event-stream transcript → expected HID-report-stream transcript.
- Integration: replay a recorded edge-stream against the full daemon; diff produced HID reports against the firmware-local output produced by the same stream running through a local QMK simulator build.

### 9.3 End-to-end

- Latency measurement: firmware timestamps outgoing edges with a monotonic counter; daemon timestamps inject reports with the same counter after round-trip. Target p95 < 5 ms.
- Soak test: 24 h typing simulation driven by a keystroke generator, verify no stuck keys, no mode flips, no edge drops.

---

## 10. Rollout

1. Land `host_offload` module + sysex commands in firmware, feature-gated behind `HOST_OFFLOAD_ENABLE = yes` in `rules.mk`. Default off.
2. Land host-side `ActionEngine` + `HidReportBuilder` + UI tab in QMKata, behind a config flag.
3. Dogfood on ANSI Q3 Max. Flip firmware default to `HOST_OFFLOAD_ENABLE = yes` once stable.
4. Extend to ISO Q3 Max and other Keychron targets by only adding the `HOST_OFFLOAD` build flag — the module is keyboard-agnostic.

---

## 11. Open questions for implementation plan

- Exact command-ID numbers (must not clash with existing `QMKataKeybCmd` IDs in `QMKata.h`).
- Whether to reuse `STATUS_ID_MATRIX` snapshot path at all in HOST mode, or rely purely on edges (recommendation: keep snapshots for resync / daemon-startup only).
- Ring buffer memory: static-allocated or built on an existing QMK queue primitive?
- Where the `HOST_OFFLOAD` capability bit lives in the existing features response.

These are resolved in the implementation plan.
