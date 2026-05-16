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

**Option C** for v1.0. Roughly:

1. Map a `Memory.MappedMemory` at 0x50000000 size 0x4000 just to silence the "unimplemented register" noise.
2. Use `sysbus SetHookAfterPeripheralRead` on GINTSTS/DOEPINT0/DIEPINT0 — drive them from a Renode `Sequence` of timed events.
3. Manually set NVIC `pending` for IRQ 67 from the .resc/Python at the right moments.
4. For Phase 4: hook QMK's `host_keyboard_send` (and equivalents) directly. The HID report bytes are in `r1/r2` at call time.

Effort: 3–5 days instead of 12. Re-evaluate after we see what symbols actually carry the reports.

## Next step

**Task 3.2 modified scope:** wire enough of the peripheral that the firmware reaches `USB_ACTIVE` state and calls `keyboard_post_init` hooks that depend on it. Then Phase 4 can intercept `send_keyboard` directly.
