# STM32F4 QMK Hardware Emulator — Design

**Status:** Approved (brainstorming complete, ready for implementation planning)
**Date:** 2026-05-16
**Target hardware:** Keychron Q3 Max (STM32F401 + LKBT51 SPI wireless co-MCU)
**Working name:** `qmk-stm32f4-emu`

---

## 1. Scope & Goals

**One-line goal.** Run the unmodified Keychron Q3 Max QMK firmware binary under Renode, with a stubbed LKBT51 SPI co-MCU, capturing the HID reports the firmware emits so QMK behavior can be exercised end-to-end without flashing hardware.

### v1.0 success criterion

Criterion (b) from brainstorming: firmware boots through the full QMK key-processing pipeline; HID reports are captured and logged. No host-OS HID device, no "type into editor" demo in v1.0.

### In scope (v1.0)

- Boot target: `keychron/q3_max/ansi_encoder` default keymap.
- Cortex-M4F core; STM32F401 peripherals required by boot: RCC, FLASH, GPIO A/B/C/D, NVIC, SysTick, TIM (matrix scan), SPI1 (LKBT51), USB OTG FS (device mode, minimal), EXTI.
- LKBT51 SPI-slave stub responding "no host paired, wired mode."
- Matrix injection via scripted GPIO pin-state changes, layout sourced from `info.json`.
- HID report capture & decode (keyboard 6KRO + NKRO, mouse, consumer/system, raw HID).
- Two demo scenarios: `boot_smoke.py`, `type_hello.py`, `combo_test.py`.

### Out of scope (v1.0)

- USB/IP bridge to Linux `vhci-hcd` (deferred to v2.0).
- Real `/dev/input/eventN` keyboard / "type into gedit" demo (v2.0).
- Q3 HE (Hall-effect ADC matrix), V3 Max (v2.2–v2.3).
- RGB matrix visual rendering (v2.1).
- Bluetooth-mode HID path / scriptable LKBT51 host pairing (v2.5).
- Cycle-accurate timing.
- DFU bootloader.
- Encoder rotation injection (v2.4).
- macOS/Windows host support.

---

## 2. Architecture

### Process topology

```
┌───────────────────────────────────────────────────────────────────┐
│  Linux host                                                       │
│                                                                   │
│   ┌────────────────────────────────────────────────────────────┐  │
│   │  Renode process                                            │  │
│   │                                                            │  │
│   │   ┌──────────────────────────────────────────────────┐     │  │
│   │   │  Cortex-M4F (TLib JIT) — boots unmodified .elf   │     │  │
│   │   └───┬──────────────────────────────┬───────────────┘     │  │
│   │       │                              │                     │  │
│   │   ┌───▼────────┐  ┌────────────┐  ┌──▼──────────┐  ┌─────┐ │  │
│   │   │STM32F401   │  │ Flash ctlr │  │ GPIO A/B/C/D│  │ TIM │ │  │
│   │   │RCC/SysCfg  │  │ (EEPROM)   │  │  EXTI       │  │ SPI │ │  │
│   │   └────────────┘  └────────────┘  └──┬──────────┘  └──┬──┘ │  │
│   │                                      │                │    │  │
│   │   ┌──────────────────────────────┐  ┌▼────────────┐ ┌─▼──┐ │  │
│   │   │ OTG FS device (minimal)      │  │ Matrix      │ │LKB-│ │  │
│   │   │ + internal fake host driver  │  │ injector    │ │T51 │ │  │
│   │   │ + HID report logger          │  │ (Python)    │ │stub│ │  │
│   │   └───────────▲──────────────────┘  └─────▲───────┘ └────┘ │  │
│   │               │                            │                │  │
│   └───────────────┼────────────────────────────┼────────────────┘  │
│                   │                            │                   │
│         logs to stdout/file        Python scenario driver          │
└───────────────────────────────────────────────────────────────────┘
```

### Components

1. **Renode platform description (`.repl`).** SoC layout, memory regions, peripherals, GPIO wiring to matrix injector and LKBT51 stub.
2. **SoC peripheral models.** Renode mainline STM32F4 components (RCC, GPIO, EXTI, TIM, SPI, FLASH).
3. **LKBT51 SPI-slave stub (Python, ~300 LOC).** Responds to handshake + state-register reads; ACKs/discards key-report SPI writes; toggles `B1` to signal replies.
4. **Matrix injector (Python, ~200 LOC).** Reads `keyboards/keychron/q3_max/info.json`; drives row-input GPIOs based on currently-pressed key set.
5. **OTG FS device-mode minimal model (C#).** Extends Renode mainline; covers register decode, FIFO model, EP0 control, IN/OUT transfers, RESET/ENUMDNE/XFRC interrupts, SOF synthesis.
6. **Internal fake-host shim (C#).** Drives the firmware-side USB device through reset → SET_ADDRESS → GET_DESCRIPTOR → SET_CONFIGURATION so it reaches "configured" state.
7. **HID report logger (Python, ~150 LOC).** Hooks IN-EP FIFO writes, decodes per QMK descriptor set, logs to stdout/file.
8. **Scenario driver (Python).** Outside Renode; uses Renode's robot/scripting interface to start machine, wait for UART markers, inject matrix events, assert on captured reports.

### Deferred to v2.0 (3-layer USB)

The full v2.0 USB stack — L1 USB/IP server, L2 generic device façade, L3 full OTG FS fidelity — is designed but not built in v1.0. v1.0 ships with L3-minimal + an internal fake host + report sniffer. v2.0 replaces the fake host with L1/L2 and reaches criterion (a).

### Key shape decisions

- **Renode over QEMU:** Renode has more mature STM32 peripheral models and an extensible peripheral framework; QEMU would require writing the OTG FS device model from scratch.
- **Python for stubs, C# for the OTG FS extension:** Iteration speed where iteration matters; C# only where Renode's plugin boundary requires it.
- **Matrix layout from `info.json`:** Single source of truth; no duplication.
- **Top-level `emulator/` directory:** Purely additive; QMK tree untouched.
- **3-layer USB split (L1/L2/L3):** ~300 extra LOC vs collapsed, pays off in v2.0 test isolation and portability.

---

## 3. Boot path & peripheral coverage

### Stage 0 — Reset → ChibiOS early init

Cortex-M4F core, NVIC, SysTick, SCB, FLASH (read, ACR), RCC (HSE + PLL → 84 MHz sysclk), PWR. All Renode mainline. Risk: RCC PLL ready-bit timing under ChibiOS `stm32_clock_init` — Phase 1 gate.

### Stage 1 — ChibiOS kernel start, board init

GPIO port clock gates, alternate-function muxing for SPI1 (A5/A6/A7), wireless interrupt pins (A4 out, B1 in EXTI). All mainline.

### Stage 2 — Matrix init

`MATRIX_ROW_PINS` / `MATRIX_COL_PINS` from `config.h`; matrix scan timer (TIM2 typical). Matrix injector hooks into row/col GPIOs.

### Stage 3 — LKBT51 wireless init

From `keychron_wireless_common.c:64` → `wireless_init()` → `lkbt51.c`:

- `spiStart(&SPID1, &spicfg)` (master, CPOL=0/CPHA=0 assumed, baud div 16 @ APB2).
- A4 push-pull output; B1 input pull-up with EXTI falling-edge.
- Handshake: A4 toggled to wake/reset co-MCU.
- First SPI transaction likely `LKBT51_CMD_GET_MODULE_INFO (0x40)`.
- State polled via `LKBT51_CMD_READ_STATE_REG (0x25)`.
- 2000 ms timeout, 3 retries (`LKBT51_COMM_TIMEOUT_MS`, `LKBT51_TX_RETRY_COUNT`) — firmware continues even if stub silent.

LKBT51 command opcodes the stub must handle:
- `0x40 GET_MODULE_INFO` → plausible fw/hw version.
- `0x25 READ_STATE_REG` → "no host paired, no connection."
- `0x61 HAND_SHAKE_TOKEN` → completes handshake.
- `0x11–0x17 SEND_KB / NKRO / CONSUMER / SYSTEM / FN / MOUSE / BOOT_KB` → ACK + discard.
- `0x31–0x33 BATTERY_*` → "charging, 100%."
- Others → ACK + discard.

### Stage 4 — USB OTG FS device init

Largest unknown. Firmware drives `GOTGCTL`, `GAHBCFG`, `GUSBCFG`, `GRSTCTL`, `GINTSTS`, `GINTMSK`, `GRXFSIZ`, `DIEPTXFx`, `DCFG`, `DCTL`, per-EP `DIEPCTLx`/`DOEPCTLx`, FIFOs.

v1.0 model coverage: device reset, address-set, IN/OUT EP config, FIFO read/write, EP0 control plumbing, RESET/ENUMDNE/XFRC interrupts, 1 kHz SOF. Out: low-power, suspend/resume, OTG host mode, ISO.

Strategy: pass *enough* for ChibiOS + QMK + the internal fake-host shim to reach "configured" state. Cut-line at 12 working days — fall back to firmware-symbol-level capture if exceeded.

### Stage 5 — RGB matrix init

SPI2 or driver IC writes go to null sink. No rendering in v1.0.

### Stage 6 — Steady state

Matrix scan + key processing + USB IN dispatch + occasional FLASH erase/program for emulated EEPROM (last sector ~0x0801C000 on F401CC). Renode FLASH model expected sufficient; verify during Phase 1.

### Effort buckets

| Bucket | Items | Effort |
|---|---|---|
| Free (mainline) | M4F, NVIC, SysTick, RCC, GPIO, EXTI, TIM, SPI master, FLASH | 0 |
| Small Python | LKBT51 stub, matrix injector, HID logger | 1–2 weeks |
| Verify & patch C# | FLASH erase/program, RCC PLL timing | days |
| Large C# | OTG FS device + fake-host shim | 1.5–2 weeks (v1.0 minimal) |
| Out of scope v1.0 | USB/IP, RGB IC, encoder, Hall ADC | — |

---

## 4. USB approach (v1.0 minimal + v2.0 roadmap)

### v1.0 — L3-minimal + internal fake host + report logger

Single in-Renode block:
- OTG FS device-mode register model: register decode, per-EP FIFOs, packet accounting (`DIEPTSIZ`/`DOEPTSIZ`), interrupts (RESET, ENUMDNE, XFRC), synthesized SOF.
- Internal fake-host shim: drives the firmware through bus reset → SET_ADDRESS → GET_DESCRIPTOR(device, config, string) → SET_CONFIGURATION(1). Just enough to reach configured state.
- HID report logger: hooks IN-EP FIFO writes; decodes QMK descriptor set (5 endpoints: keyboard, mouse, extrakeys, raw, console).

No USB/IP server. No real Linux HID device.

### v2.0 — Full 3-layer split (designed, not built in v1.0)

```
L4. QMK firmware (unchanged)
L3. STM32 OTG FS device model (full fidelity)
L2. Generic USB device façade (per-EP queues, address tracking)
L1. USB/IP TCP server (USBIP_CMD_SUBMIT / UNLINK / DEVLIST / IMPORT)
    ↓ TCP 3240
Linux host: usbip-utils → vhci-hcd → hid-generic → /dev/input/eventN
```

L1 estimated 600–900 LOC C#; L2 estimated 300–500 LOC C#; L3 expansion estimated 1000+ LOC over the v1.0 model.

### Known risks (v1.0)

1. FIFO timing → firmware stalls (most likely). Mitigation: heavy tracing.
2. EP0 set-address race. Mitigation: relax ordering.
3. SOF required for some kernel paths. Mitigation: synthesize unconditionally once configured. (v2.0 only — v1.0 internal fake host doesn't care.)
4. Multi-interface HID (5 endpoints). Per-EP queues in design.
5. NKRO uses different report ID, shared EP1. Verify with descriptor dump.

### Reference trace

A USBmon capture of a real Q3 Max enumerating against a Linux host will be supplied. Used to validate L3 register-level fidelity and to seed v2.0 USB/IP development.

---

## 5. Repository layout & build integration

### Layout (all new under top-level `emulator/`)

```
emulator/
├── README.md
├── renode/
│   ├── q3_max.repl
│   ├── q3_max.resc
│   └── peripherals/
│       ├── lkbt51_stub.py        (~300 LOC)
│       ├── matrix_injector.py    (~200 LOC)
│       └── hid_report_logger.py  (~150 LOC)
├── otg_fs/
│   ├── Stm32OtgFsDevice.cs       (extends Renode mainline)
│   └── README.md
├── scenarios/
│   ├── boot_smoke.py
│   ├── type_hello.py
│   ├── combo_test.py
│   └── lib/
│       ├── layout.py             (info.json → row/col)
│       ├── renode_driver.py
│       └── report_assertions.py
├── traces/
│   ├── real_hw_usbmon.pcapng     (supplied)
│   └── real_hw_README.md
├── build/
│   ├── emu_keymap/keymap.c       (optional, with extra logging)
│   └── rules.mk                  (CONSOLE_ENABLE, DEBUG_MATRIX_SCAN_RATE_ENABLE, -DEMULATOR_BUILD)
└── docs/
    ├── design.md                 (this file, copied or linked)
    ├── boot_path.md
    └── troubleshooting.md
```

### Build profiles

1. **Stock:** `qmk compile -kb keychron/q3_max/ansi_encoder -km keychron`. The emulator must boot this binary.
2. **Emu-diagnostic:** Same target, plus `CONSOLE_ENABLE=yes`, `DEBUG_MATRIX_SCAN_RATE_ENABLE=yes`, `-DEMULATOR_BUILD`. Build-flag changes only, no source changes.

Renode script accepts the ELF path:

```
renode emulator/renode/q3_max.resc \
    --variable elf=.build/keychron_q3_max_ansi_encoder_keychron.elf
```

No `Makefile`, `paths.mk`, `builddefs/`, `platforms/`, `tmk_core/`, `quantum/`, `tests/`, `keyboards/` changes.

### Layout from info.json

```python
layout = Layout.from_info_json(
    "keyboards/keychron/q3_max/info.json",
    variant="ansi_encoder",
)
layout.coords("KC_H")  # → (row, col)
```

### Scenario style

Plain Python (not pytest). Exit-coded for future CI integration.

### Host dependencies (v1.0)

- Renode (pinned version in README)
- Python 3.10+
- `arm-none-eabi-gcc` (already needed for QMK)
- `gdb-multiarch` (optional)

v2.0 adds: `usbip` userspace, `vhci-hcd` kernel module.

---

## 6. Phasing & milestones

| Phase | Days | Goal | Exit criterion |
|---|---|---|---|
| 0 — Renode sanity | 2 | Toolchain runs; PC advances past reset | Renode opens, CPU executes |
| 1 — Boot through Stage 2 | 4–6 | Firmware reaches `main()`, UART output | UART analyzer shows QMK boot banner |
| 2 — LKBT51 stub + matrix injector | 4–6 | Wireless init clean; matrix scan sees presses | `boot_smoke.py` green |
| 3 — OTG FS minimal + fake host | 6–10 | USB stack reaches configured state | UART log shows configured; firmware writes IN-EP reports |
| 4 — HID report logger | 3–4 | Reports captured & decoded | `type_hello.py` green |
| 5 — Polish, docs, second scenario | 3–4 | Usable by others | `combo_test.py` green; README complete |

**Total: 22–32 working days = 4.5–6.5 calendar weeks at full focus, 3–5 weeks realistic.**

### Cut-line

Phase 3 has an explicit 12-working-day cut-line. If exceeded, fall back to firmware-symbol-level capture (hook `report_keyboard` and equivalents via ELF debug info), shipping v1.0 without OTG FS fidelity. v2.0 takes on the full USB stack.

### v2.0+ roadmap (not committed)

- **v2.0:** L1 USB/IP + L2 façade + full L3 → `/dev/input/eventN` → gedit demo. +4–6 weeks.
- **v2.1:** RGB matrix render-to-window.
- **v2.2:** Q3 HE (Hall-effect ADC).
- **v2.3:** V3 Max.
- **v2.4:** Encoder rotation injection.
- **v2.5:** Wireless-mode HID path; LKBT51 stub becomes scenario-scriptable.

### Risk register

| Risk | Phase | Severity | Mitigation |
|---|---|---|---|
| Renode STM32F401 RCC PLL timing wrong | 1 | High | Patch RCC model |
| ChibiOS clock init hangs | 1 | High | Phase 1 gate — fail fast |
| LKBT51 protocol details missed | 2 | Medium | Read `lkbt51.c`; protocol is open |
| OTG FS scope balloon | 3 | High | 12-day cut-line; symbol-level fallback |
| QMK HID descriptor surface > 5 EP | 4 | Low | Per-EP queues already in design |
| `info.json` label → matrix mapping ambiguous | 2 | Low | Verify Phase 2 day 1 |
| Renode version drift dev vs CI | 5 | Low | Pin version |

---

## Decisions log (brainstorming Q&A)

1. **Emulator type:** Full MCU emulator (option 3).
2. **MCU family:** STM32F401 (Q3 Max), well-supported part path.
3. **Emulator tool:** Renode over QEMU (USB device mode is decisive).
4. **Boot target:** Active-development board (Q3 Max ANSI Encoder default keymap).
5. **Wireless co-MCU:** Stub LKBT51 in Renode; firmware binary unmodified.
6. **USB/IP layer (v1.0):** In-Renode (5a) — designed but deferred to v2.0.
7. **Stub language:** Python for LKBT51 and matrix injector.
8. **Layout source:** `info.json` (single source of truth).
9. **USB risk framing:** Land Stages 0–3 first; OTG FS is gated.
10. **LKBT51 scriptability:** Static "no host paired" in v1.0; scriptable in v2.5.
11. **RGB rendering:** Deferred to v2.1.
12. **v1.0 USB scope:** Log HID reports only (criterion b); USB/IP + gedit in v2.0.
13. **USBmon trace:** Will be captured from real hardware.
14. **`CONSOLE_ENABLE` + `DEBUG_MATRIX_SCAN_RATE` for emu build:** OK.
15. **Top-level `emulator/` dir:** Yes, separate from QMK tree.
16. **Emu-diagnostic build:** `emulator/build/rules.mk` (not a keymap variant).
17. **Scenario style:** Plain Python (not pytest).

---

## Next steps

1. This design committed to git.
2. Invoke `writing-plans` skill to produce a detailed implementation plan (separate document).
3. Implementation kicks off at Phase 0.
