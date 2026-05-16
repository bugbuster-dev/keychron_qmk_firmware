# OTG FS Emulation Survey

**Task 3.1 — what's there, what's the gap.**

## What Renode 1.16.1 portable ships

- **No STM32 OTG FS device-mode peripheral class.** Searched the renode binary's strings for "OTG", "USB", "Synopsys" — nothing matching the F4 OTG FS hardware.
- One existing tag in `platforms/cpus/stm32f4.repl`:
  ```
  Tag <0x50000010, 0x5000003F> "USB:RESET" 0x80000000
  ```
  This covers GRSTCTL only, returning bit 31 (AHBIDL) set so the firmware's reset-wait spin exits.
- `platforms/cpus/stm32f412.repl` has a coarser tag:
  ```
  Tag <0x50000000, 0x5003FFFF> "USB_OTG_FS"
  ```
  Reads return 0 across the whole region. F412 must not boot far enough into ChibiOS USB driver for this to matter.

## What the firmware actually touches

Collected via `sysbus LogAllPeripheralsAccess true` for 1 simulated second.

### OTG_FS_GLOBAL block (0x50000000–0x500001FF)

| Offset | Register | Direction | Purpose |
|--------|----------|-----------|---------|
| 0x000  | GOTGCTL  | W         | OTG control — VBUS sense, host/device override |
| 0x008  | GAHBCFG  | R/W       | AHB config — global IRQ enable, DMA enable |
| 0x00C  | GUSBCFG  | R/W       | Core config — force device mode, PHY type |
| 0x010  | GRSTCTL  | R/W       | Reset control — core soft reset, FIFO flush. **Phase 1 tag returns 0x80000000.** |
| 0x014  | GINTSTS  | R/W1C     | Interrupt status — USBRST, ENUMDNE, RXFLVL, SOF, IEPINT, OEPINT |
| 0x018  | GINTMSK  | R/W       | Interrupt mask |
| 0x038  | GCCFG    | R/W       | General core config — PHY power-down disable, VBUS sense |

### OTG_FS_DEVICE block (0x50000800–0x500009FF)

| Offset | Register | Direction | Purpose |
|--------|----------|-----------|---------|
| 0x800  | DCFG       | R/W      | Device config — speed, address |
| 0x810  | DSTS       | R        | Device status — enum speed, frame number |
| 0x814  | DIEPMSK    | R/W      | IN-EP interrupt mask |
| 0x81C  | DAINTMSK   | R/W      | All-EP interrupt mask |
| 0x900  | DIEPCTL0   | R/W      | IN-EP0 control |
| 0x908  | DIEPINT0   | R/W1C    | IN-EP0 interrupt — XFRC, TOC |
| 0x920+ | DIEPCTL1-3 | R/W      | IN-EP1..3 control (keyboard, mouse, extrakeys, raw, console) |
| 0x928+ | DIEPINT1-3 | R/W1C    | IN-EP1..3 interrupt |
| 0xB00  | DOEPCTL0   | R/W      | OUT-EP0 control |
| 0xB08  | DOEPINT0   | R/W1C    | OUT-EP0 interrupt — XFRC, STUP |
| 0xB20+ | DOEPCTL1-3 | R/W      | OUT-EP1..3 control |
| 0xB28+ | DOEPINT1-3 | R/W1C    | OUT-EP1..3 interrupt |

### OTG_FS_PWRCLK block (0x50000E00–0x50000EFF)

| Offset | Register | Direction | Purpose |
|--------|----------|-----------|---------|
| 0xE00  | PCGCCTL  | R/W       | Power/clock gating — stop PHY clock, gate HCLK |

### FIFO regions (0x50001000+)

Not yet seen in the trace because firmware hasn't reached the point of pushing report bytes. We'll need:

- **RX FIFO at 0x50001000** — single shared FIFO for all OUT endpoints. Firmware reads via `volatile uint32_t* fifo = (void*)0x50001000`.
- **TX FIFO N at 0x50001000 + N*0x1000** (aliased) — one per IN endpoint. Firmware writes via `*(volatile uint32_t*)(0x50001000 + 0x1000*N) = word`.

## What the firmware does today (with our existing tag)

Per the boot trace:

1. `usb_lld_start` runs, programs GUSBCFG/GCCFG/GAHBCFG → silently absorbed (writes to unimplemented registers).
2. Spins on GRSTCTL bit 31 — exits because tag returns 0x80000000.
3. Writes GRSTCTL.CSRST=1, polls bit 0 — exits because tag returns 0x80000000 with bit 0 = 0.
4. Configures DIEPCTL0–3 and DOEPCTL0–3 → silently absorbed.
5. Sets DCTL.SDIS=0 (soft connect) → silently absorbed.
6. Enables global IRQ via GAHBCFG.GINTMSK=1 → silently absorbed.
7. Returns. **From this point on, firmware waits for OTG_FS_IRQ (NVIC line 67) to fire.**

The IRQ never fires because no peripheral is generating it. Firmware sits with USB in "powered on, waiting for bus reset" state forever.

## The minimum to make Phase 3 work

We need to:

1. **Map the OTG FS region as a real Renode peripheral** (not just plain memory) so we can model W1C semantics and trigger NVIC line 67.
2. **Implement W1C on GINTSTS/DIEPINTx/DOEPINTx** — writes of 1 clear the corresponding bit.
3. **Provide a "fake host" engine** that, after some grace period, sets GINTSTS.USBRST and raises IRQ 67.
4. **Implement EP0 SETUP delivery** — push 8 bytes into "RX FIFO", set GINTSTS.RXFLVL, then DOEPINT[0].STUP, raise IRQ.
5. **Implement EP0 IN response** — when firmware writes EP0 TX FIFO and sets DIEPCTL0.EPENA, capture those bytes and signal DIEPINT[0].XFRC.
6. **Sequence the enumeration**: device descriptor → SET_ADDRESS → config descriptor → string descriptors → SET_CONFIGURATION(1).
7. **After SET_CONFIGURATION**, firmware starts populating EP1..5 TX FIFOs with HID reports. Capture those for Phase 4.

## Implementation choices

### Option A — Pure Python via `Python.PythonPeripheral`

`Python.PythonPeripheral` in Renode 1.16.1 supports a single contiguous register range with a Python `request.Value` getter/setter and per-offset routing inside the script. Pros: no Renode source build; iterable; fast turnaround. Cons: Python overhead per register access (every CPU read/write goes through CPython); fragile across Renode versions.

### Option B — Patch firmware to skip USB

Apply the same "patch the wait" approach we used for SPI: hook `usb_lld_start`'s post-init path and immediately call back into ChibiOS's `_usb_isr_invoke_event_cb(USB_EVENT_CONFIGURED)`. The firmware enters the "USB configured" state without any real bus events. Then **Phase 4-alt** (intercept `host_keyboard_send`) captures reports directly from CPU registers at the symbol level.

Pros: zero peripheral work, days not weeks. Cons: USB stack untested; if we ever want v2.0 USB/IP, all this work gets thrown away anyway… which is fine because it WILL be thrown away (v2.0 needs a proper C# OTG FS peripheral plugin).

### Option C — Hybrid

A thin Python peripheral that only handles the IRQ trigger + enumeration state machine, plus firmware-symbol hooks for capturing reports. Gets us:
- Real bus reset + enumeration (so QMK's "USB connected" indicator works, post-init hooks fire correctly)
- Symbol-level report capture (faster Phase 4)

## Recommendation

### Why QMKata changes the calculation

The Q3 Max firmware integrates **QMKata** (`keyboards/keychron/qmkata/`), a bidirectional sysex-over-raw-HID protocol used during your firmware development for:

- Get/set keymap layers, combos, leader sequences, tap dances at runtime
- Load and delete modules (SRAM and flash slots)
- Subscribe to keyboard-side event publishers
- Query module / config / build state

The transport is **`raw_hid_send` outgoing + `raw_hid_receive` incoming**, both via QMK's USB **raw endpoint (EP4)**. This is two-way: host sends a sysex `GET_*`/`SET_*` command, keyboard parses it and responds.

For scenarios to exercise QMKata, the emulator needs:

- **EP4 OUT injection** — push host-originated sysex bytes into the firmware's raw-HID OUT FIFO so `raw_hid_receive` is called with the right data
- **EP4 IN sniffing** — capture bytes the firmware writes via `raw_hid_send` so the scenario can decode responses
- **USB state machine alive enough** that `qmkata_start()` fires from `keyboard_post_init_kb` and `qmkata_task()` runs in the main loop

Option B (symbol-level capture only) handles the one-way HID-keyboard case but not the EP4 OUT injection path — `raw_hid_receive` is dispatched from inside `tmk_core/protocol/chibios/usb_main.c` after a real USB EP4 OUT transaction completes. You could hook the function and inject bytes directly, but you'd bypass QMK's USB-state gating and timing, and QMKata's protocol parser may break in subtle ways.

### Decision: **Option C** (hybrid)

A thin Python peripheral that:

1. Maps the OTG FS region as `Memory.MappedMemory` for silence on
   reads/writes we don't model.
2. Implements W1C semantics + IRQ generation only for the handful
   of registers ChibiOS actually polls during enumeration.
3. Drives a scripted enumeration sequence (USBRST → ENUMDNE →
   GET_DESCRIPTOR → SET_ADDRESS → SET_CONFIGURATION).
4. Exposes an EP4-OUT inject API for scenarios.
5. Exposes an EP-IN capture API for scenarios (used by Phase 4's
   HID report logger AND QMKata response capture).

Effort: 5–8 days. Still under the plan's 12-day cut-line. Delivers:
- One-way HID-keyboard report capture (Phase 4 unblocked)
- Two-way QMKata sysex (the actually-interesting scenarios)
- A natural-looking `USB_ACTIVE` state inside QMK

Throws away when v2.0 lands proper C# USB/IP, but that's also true
of Option B's symbol hooks, so the throwaway cost is equal.

## Next step

**Task 3.2:** wire the OTG FS Python peripheral as described in Option C. Sub-tasks:

1. Map MappedMemory at 0x50000000 + W1C hook wrapper class.
2. Implement enumeration state machine (USBRST → ENUMDNE → SETUP/IN cycle for 4 standard descriptors).
3. Wire NVIC IRQ 67 (OTG_FS) — confirm Renode's NVIC accepts raise-from-Python.
4. Surface inject/capture APIs to scenario layer.

## Showstopper probe results (2026-05-17)

Before committing to Option C, verified Renode 1.16.1 portable
supports the three capabilities Option C depends on:

### Showstopper 1: state persistence in PythonPeripheral — RESOLVED

A Python.PythonPeripheral with `initable: true` runs the script
once with `request.IsInit` set; local variables created in that
branch persist across subsequent `IsRead` / `IsWrite` calls.
Tested with a dict-based register store:

```python
if request.IsInit:
    state = {"regs": {}, "calls": 0}
elif request.IsRead:
    state["calls"] += 1
    request.Value = state["regs"].get(request.Offset, 0)
elif request.IsWrite:
    state["regs"][request.Offset] = request.Value
```

Reads return previously-written values from the dict. The `calls`
counter increments monotonically. Works as expected.

### Showstopper 2: multi-register dispatch — RESOLVED

A single `PythonPeripheral` with `size: 0x4000` covers the entire
OTG FS region. `request.Offset` correctly identifies the accessed
register; the script dispatches with normal `if` / `elif`. Tested
with reads at four distinct offsets, each returning its own value.

### Showstopper 3: raising NVIC IRQ from Python — RESOLVED

PythonPeripheral itself has zero GPIO outputs by default
(`self.HasGPIO()` returns False, `self.GetGPIOs()` returns empty),
so the standard `-> nvic@67` repl syntax does not work. **But**
the script can reach into the machine and call NVIC directly:

```python
m = self.GetMachine()
nvic = m["sysbus.nvic"]
nvic.OnGPIO(67, True)    # assert IRQ line 67
nvic.OnGPIO(67, False)   # deassert
# or one-shot:
nvic.SetPendingIRQ(67)
```

Verified `OnGPIO(67, True/False)` and `SetPendingIRQ(67)` all
execute without exception. Actual IRQ delivery to a running CPU
will be verified during Task 3.2 integration.

### Constraint discovered: IronPython 2.7.12

Renode's Python engine is **IronPython 2.7.12** (Python 2 syntax).
Peripheral scripts must NOT use:

- f-strings (`f"..."`) — use `"%x" % v` or `"{}".format(v)`
- `from __future__ import annotations` — Python 3 only
- Type hints — Python 3 only
- `print()` as statement vs function — IronPython accepts both,
  but use `self.Log(LogLevel.Warning, ...)` for emulator output

The `Lkbt51Protocol` and `MatrixState` unit-tested classes
already use modern Python 3 syntax. They will need a thin IronPython
2.7-compatible wrapper layer when imported into Renode peripherals
(or the modern syntax stripped from the modules used inside the
peripheral). Probably easiest: keep Python 3 sources for tests
and write parallel `_irpy` adapter files.

### API reference (PascalCase, IronPython 2.7)

| Property | Type | Notes |
|----------|------|-------|
| `request.IsInit`  | bool | true on first call (script load) |
| `request.IsRead`  | bool | true on register read |
| `request.IsWrite` | bool | true on register write |
| `request.IsUser`  | bool | user-invoked (e.g., from monitor) |
| `request.Type`    | enum | textual type for logging |
| `request.Offset`  | int  | byte offset within peripheral region |
| `request.Value`   | int  | read: script sets; write: script reads |
| `self.Log(level, msg)` | — | level from `LogLevel.{Noisy,Info,Warning,Error}` |
| `self.GetMachine()` | Machine | full machine handle |
| `m["sysbus.nvic"]` | NVIC | get NVIC by path |
| `nvic.OnGPIO(line, bool)` | — | assert/deassert IRQ line |
| `nvic.SetPendingIRQ(line)` | — | one-shot pending |

## Conclusion

**No showstoppers.** Option C is feasible in Renode 1.16.1 portable
without building Renode from source. Estimated effort revised down
to **4–6 days** for Task 3.2 (was 5–8 in the original survey).
Ready to begin implementation.
