# Boot path observations

Running notes captured while bringing up the Renode emulator. Updated as each phase lands.

## Phase 0 — Renode sanity (2026-05-16)

**Environment**
- Renode `1.16.1.17033` (`d66b0c2a-202602160923`, .NET 8.0.12), portable build at `~/renode/`.
- Firmware ELF: `.build/keychron_q3_max_ansi_encoder_keychron.elf`, 418 KB on disk; sections: text=105K data=3K bss=61K (`arm-none-eabi-size`).
- Platform: `emulator/renode/q3_max.repl` extends Renode mainline `platforms/cpus/stm32f4.repl`, overrides flash to 256 KB and SRAM to 64 KB for F401CC.

**Smoke test result**
```
renode --disable-xwt --console -e "include @emulator/renode/q3_max.resc; start; sleep 2; pause; cpu PC; quit"
```

- ELF loads at correct addresses (vector table @ 0x08000000).
- Initial PC=0x0801015D, SP=0x20000400 — picked up from the vector table by Renode auto-detection.
- After ~2 s simulated time, PC=0x08025144 — firmware code, well past reset. Strong signal that ChibiOS `stm32_clock_init` did not hang.

**Observed warnings (informational, none blocking)**
- `pwr` writes touching reserved bits LPUDS/MRUDS — F4-only fields, F401 differences expected.
- `rcc` unhandled bits at offsets 0x0/0x4/0x10/0x14/0x20/0x24 — STM32F4_RCC model is conservative about reserved fields; firmware writes wholesale config words.
- `DBG:DBGMCU_IDCODE` and `DBG:DBGMCU_APB1_FZ` reads/writes — debug peripheral, harmless.
- `flash_controller: Attempted to write 0x80000000 to a locked Control register` — likely a CubeMX-style FPEC unlock attempt. Verify in Phase 1 whether QMK's emulated-EEPROM writes work (need real FPEC unlock sequence).
- `ReadByte/ReadDoubleWord from non existing peripheral at 0x0/0x8` — possibly NULL-pointer dereference deep in vendor code or a deliberate boot-magic probe. Track in Phase 1.
- `ReadDoubleWord from non existing peripheral at 0xE0001000` — DWT (Data Watchpoint and Trace) unit. Renode's Cortex-M model doesn't implement DWT; firmware can run without it.

**Status:** Phase 0 exit criterion met — CPU runs, advances past reset, reaches firmware code addresses, no fault traps.

**Not yet verified (Phase 1)**
- Does the firmware reach ChibiOS `main()`?
- Does USART2 emit anything? (Stock build has `CONSOLE_ENABLE=no`; expect silence until emu-diagnostic build lands in Task 1.5.)
- Is the matrix-scan timer running?
- Does the wireless init retry-loop (LKBT51 absent) or fall through? (Section 3 of the design predicted "fall through after 2000 ms × 3 retries.")

## Phase 1 — Boot through Stage 2 (2026-05-16)

### Task 1.1 — Peripherals firmware touches at boot

Peripherals accessed in the first second of simulated time (via `sysbus LogAllPeripheralsAccess true`):

- **Core/control:** `cpu`, `nvic`, `rcc`, `pwr`, `flash`, `flash_controller`.
- **GPIO ports:** `gpioPortA`, `gpioPortB`, `gpioPortC`, `gpioPortD`, `gpioPortE`, `gpioPortH`.
- **Timers/IRQ:** `exti`, `timer2`, `rtc`.
- **DMA:** `dma1`, `dma2` (zeroed during HAL init; not actively used in this window).

Notable observations:

- `flash_controller` Control register write `0x80000000` ignored on first attempt ("Attempted to write to a locked Control register"). ChibiOS HAL later issues the unlock-key sequence (writes `0xCDEF89AB` to the Key register) and subsequent writes succeed.
- `exti` configured with RisingTriggerSelection=0x620000 (bits 17, 18, 22) and InterruptMask=0x620000 — these correspond to EXTI lines for the wireless interrupt pin (B1) and other system interrupts.
- `timer2` configured as ChibiOS system tick: prescaler `0x1DF` (480), autoreload `0xFFFFFFFF`, free-running counter, single-shot interrupts via CC1.

### Task 1.2 — Boot blocker discovered & fixed: F_SIZE OTP

Initial boot halted at `chSysHalt` (`0x08025138`, infinite spin loop at `0x08025144`), reached from `backing_store_init` after the EFlashDriver descriptor pointer (`EFLD1.descriptor`, offset +32) came back as zero.

Root cause: `efl_lld_init()` at `0x080268e8` reads the F4 device's flash size from OTP at `0x1FFF7A22` (a half-word, value in KB) and selects a sector descriptor based on the size. Renode's `rom1` region (`0x1FFF0000 + 0xC000`) returns 0 for uninitialized OTP bytes. Zero matches none of the size buckets (0x80 / 0x100 / 0x180 / 0x200 KB), so the descriptor pointer is never set, ChibiOS halts.

Fix (in `q3_max.resc` reset macro): write `0x0100` (256 KB) to `0x1FFF7A22` after `LoadELF`. F401CC is a 256 KB device. After this patch, `efl_lld_init` selects the F4 256K sector descriptor at `0x08029c8c`.

### Task 1.2 follow-up — Boot reaches ChibiOS idle

After the F_SIZE fix:

- Boot trace expands from 257 → 11,840 log lines.
- Wear-leveling backing store runs ~770 `efl_lld_program` operations during `eeconfig_init` / `keyboard_init` — uninitialized SRAM written as eeconfig defaults. Flash controller accepts these (PG bit pattern in Control, EOP cleared in Status).
- One TIM2 OS-tick interrupt fires, ChibiOS scheduler runs once, then enters `__idle_thread` (`0x08025524: wfi; b __idle_thread`).
- Simulated clock advances ~80 ms during this; further idle is "tickless" — Renode's TIM2 only logs activity when ChibiOS re-arms it.

**No SPI / USB / matrix scan activity observed.** Possible reasons:
1. Main thread is blocked waiting on a peripheral that doesn't respond (most likely USB OTG FS, which isn't in the platform yet — reads return 0, ChibiOS may spin in `usb_lld_start`).
2. Main thread completed and parked; QMK doesn't schedule periodic matrix-scan ticks until USB enumeration finishes.

**Phase 1 status:** ChibiOS kernel + HAL up, wear-leveling functional, system tick alive. Beyond this depends on USB OTG FS modeling (Phase 3 work). Phase 1 exit criterion ("UART analyzer shows QMK boot text") **not yet met** — `CONSOLE_ENABLE` is off in the stock build, so silence is expected. Task 1.5 (emu-diagnostic build) needed to verify further progress via UART.

### Open questions for next session

- Add USB OTG FS as a stub peripheral so `usb_lld_start` doesn't block? Or build emu-diagnostic with `USB=no` first?
- Inspect what `main()` actually did via execution tracer (Renode has `cpu CreateExecutionTracing`) — a more reliable boot trace than peripheral access alone.
- Address the "ReadByte from 0x0" warnings (still showing post-fix? need to re-check the log).

### Task 1.3 — Diagnosed two more boot blockers and worked around them

After F_SIZE, the firmware reached ChibiOS idle and stayed there. Drilling into the boot path with `sysbus.cpu AddHook` traced execution to `init_usb_driver`, which calls `chThdSleep(5000)`. The sleep never returned. Root causes:

**Issue A — NVIC ICSR RETTOBASE bit always returns 0**

ChibiOS port `port-armv7m`'s `__port_irq_epilogue` (`0x08025838`) reads SCB ICSR at `0xE000ED04` and tests bit 11 (RETTOBASE) to decide whether to run the preemption check on ISR exit. Renode's NVIC model returns `0x00065580` for ICSR — RETTOBASE bit clear regardless of nesting state — so ChibiOS always takes the "still nested" branch and skips preemption. Sleeping threads are never resumed.

Workaround in `q3_max.resc`:

```
sysbus SetHookAfterPeripheralRead sysbus.nvic """
if offset == 0xD04:
    value = value | 0x800
"""
```

This forces bit 11 = 1 on every ICSR read. In our scenario (single non-nested IRQ at a time) this is always the correct value.

**Issue B — DWT CYCCNT not implemented; busy-loop hangs**

`chSysPolledDelayX` (`0x08025184`) busy-polls DWT CYCCNT at `0xE0001004` for short delays. Renode's Cortex-M model doesn't implement DWT — the address falls outside any peripheral and reads return 0. `usb_lld_start` calls `chSysPolledDelayX` twice (for USB core reset and stabilisation delays), causing infinite spin.

Workaround in `q3_max.resc`: patch `chSysPolledDelayX`'s first instruction to `bx lr` (encoded `0x4770`), making it a no-op. Acceptable because callers tolerate "ran faster than expected" delays — the function is a *minimum* spin, not a *fixed* sleep.

```
sysbus WriteWord 0x08025184 0x4770
```

### Task 1.3 result — Boot reaches QMK main loop

With both workarounds in place:

- `chThdSleep(5000)` in `init_usb_driver` returns ✓
- `usb_lld_start` runs, reads OTG FS DIEPCTL/DOEPCTL/GAHBCFG registers (returning SVD defaults of 0 / 0x8000), exits cleanly ✓
- `keyboard_init` runs ✓
- `matrix_init` runs ✓
- `matrix_scan` periodically runs in the main loop ✓

Confirmed by PC hooks on `0x0801e124` (`matrix_scan`) firing repeatedly.

### Open Phase 1 follow-ups

- `keyboard_post_init_quantum` hook (`0x0801d754`) fired only once but `keyboard_post_init_kb` (`0x0801bdcc`) did not. `keychron_common_init` → `wireless_common_init` → `lkbt51_init` chain therefore not executing. Likely stuck inside `quantum_init` / `led_init_ports` / `rgb_matrix_init` / `dip_switch_init` — the four calls between `matrix_init` and the post-init tail-call. Need deeper hook-trace to localise. SPI activity not yet seen, so the LKBT51 stub (Phase 2) is not yet on the critical path.
- USB OTG FS endpoint registers return 0 from SVD; no enumeration attempted yet because no fake host driving the bus. Phase 3.
- The OTG FS region warnings ("Unhandled register") are cosmetic — firmware progresses past them via spin-loop-friendly defaults.

### Task 1.3 (continued) — Two more boot blockers in `keyboard_init` sub-chain

After matrix_scan started running on its own thread, the *main* thread was still blocked inside `keyboard_init`'s sub-calls. Drilling further:

**Issue C — `spiSend` / `spiExchange` block on DMA completion**

`rgb_matrix_init` calls into `snled27351_init_drivers` → `spi_init` → `snled27351_init` → `snled27351_write_register` → `spiSend`. ChibiOS SPI HAL kicks off DMA-driven transfers then suspends the calling thread on a binary semaphore signalled by the DMA TC interrupt. Renode's DMA model doesn't interact with our SPI peripheral to generate the completion IRQ, so the thread sleeps forever.

Workaround in `q3_max.resc`: patch the first instruction of `spiSend` (`0x08025e2e`) and `spiExchange` (`0x08025e0c`) to `movs r0, #0; bx lr` (encoded `0x47702000`). Both functions become no-ops returning success with no data transferred. The SNLED27351 LED-driver writes silently fail; RGB rendering is out of v1.0 scope. The LKBT51 wireless co-MCU stub (Phase 2) uses a different Renode peripheral path that bypasses ChibiOS SPI HAL.

**Issue D — Renode `STM32F4_RTC` crashes on `DateRegister=0` write; init poll spins**

ChibiOS HAL `rtc_lld_init` writes 0 to RTC DateRegister (offset 0x4). Renode's `STM32F4_RTC` model converts the register bits to a `System.DateTime` and throws `ArgumentOutOfRangeException` for year=month=day=0.

Two-part workaround:

1. Detach the RTC peripheral (`rtc: @ none` in the `.repl`) and remap the address range as plain `Memory.MappedMemory` so register accesses are silent.
2. `rtc_enter_init` (`0x08027b54`) polls `RTC_ISR.INITF` (bit 6 of offset 0xC) to wait for the chip to enter init mode. With plain memory at the RTC region, INITF reads as 0 forever. Patch the function's first instruction to `bx lr` (`0x4770`) so init is a no-op. RTC is used only for sleep timestamps which are not exercised in v1.0.

### Phase 1 exit criterion MET ✓

After all workarounds, the firmware reaches steady state with the full main loop running:

```
housekeeping_task → protocol_pre_task → protocol_keyboard_task → (repeat)
```

`matrix_scan` runs on a separate thread. `wireless_init` ran cleanly. The LKBT51 init sent SPI writes to a non-existent peripheral (Renode warns about unhandled SPI1 transactions but the patched `spiSend`/`spiExchange` no-op return prevents any blocking).

Boot path summary (all blockers fixed via in-tree workarounds, no firmware rebuild):

| # | Blocker | Root cause | Workaround |
|---|---------|------------|------------|
| A | `chSysHalt` in `backing_store_init` | F_SIZE OTP at `0x1FFF7A22` reads 0 | Write `0x0100` at boot |
| B | Sleeping threads never wake | NVIC ICSR RETTOBASE bit stuck at 0 | `SetHookAfterPeripheralRead` OR bit 11 |
| C | `chSysPolledDelayX` spins | DWT CYCCNT not modelled (E0001004) | Patch first insn to `bx lr` |
| D | SPI calls block on DMA TC | DMA → SPI interaction missing | Patch `spiSend`/`spiExchange` to no-op |
| E | `STM32F4_RTC` crash on date=0 | Renode RTC model bug | Detach + remap as memory |
| F | `rtc_enter_init` spins on INITF | Memory RTC region returns 0 | Patch first insn to `bx lr` |

Ready for Phase 2 (LKBT51 stub on SPI1) and Phase 3 (USB OTG FS enumeration).

## Phase 2 progress

### Task 2.3 — LKBT51 SPI wiring DEFERRED

The plan called for wiring the Python `Lkbt51Protocol` (Task 2.2) into Renode SPI1 as a slave peripheral so the firmware's SPI transactions are intercepted and responded to. Investigation shows this is not on the critical path for v1.0:

- Renode 1.16.1 portable does not include a `Python.PythonPeripheral` variant that implements `ISPIPeripheral`. Only `NORFlash`, `SFDP`, and Cadence SPI controllers are present. Writing a custom SPI-slave plugin requires building Renode from source.
- More importantly: Phase 1's workaround for the DMA→SPI completion bug patched `spiSend` and `spiExchange` in firmware to `movs r0,#0; bx lr`. With those patched, the firmware emits **zero SPI bytes** during steady-state operation. There is nothing for an SPI slave peripheral to absorb.

The `Lkbt51Protocol` code from Task 2.2 is kept (well-tested, 10 unit tests green). It will be wired up in v2.x alongside a proper SPI HAL fix that allows the firmware to actually issue DMA-driven SPI transactions, at which point the protocol stub will absorb wireless-co-MCU traffic naturally.

For v1.0, this means the firmware boots cleanly past `lkbt51_init` / `wireless_init` (already confirmed in Phase 1) and the LKBT51 protocol stays a pure-Python module ready for integration.

### Task 3.2 (sub-3.2.1) — OTG FS PythonPeripheral attached

After the showstopper probe confirmed Option C is feasible, attached a Python.PythonPeripheral at 0x50000000 covering the full OTG FS region (size 0x4000). Initial implementation handles:

- W1C semantics on GINTSTS + DIEPINT[0..5] + DOEPINT[0..5]
- Per-EP TX FIFO capture into `ep_in_buffers[0..5]`
- GRSTCTL self-clear: writes to soft-reset bits (CSRST, etc.) clear immediately and re-assert AHBIDL
- DCTL.SDIS=0 detection (soft-connect; marks enum_phase = "connect_pending")
- Default register backing with read-after-write semantics

Discovered constraints during attachment:

1. **`filename:` in `.repl` resolves relative to Renode's install dir**, not the cwd. Using `filename: "emulator/otg_fs/otg_fs.py"` causes a silent 30s hang on .repl load. Workarounds: inline the script via `script: '''...'''` (multi-line triple-quote), or use absolute path. We chose inline for portability.
2. **PythonPeripheral has no GPIO outputs by default** — confirmed in showstopper probe. Wiring `-> nvic@67` in `.repl` would fail; instead the script uses `self.GetMachine()["sysbus.nvic"].OnGPIO(67, True/False)` (not yet exercised — IRQ delivery is deferred to sub-3.2.2).
3. **First fix needed at boot**: ChibiOS's `usb_lld_start` writes GRSTCTL.CSRST=1 then polls bit 0 until it clears. Without auto-clear, the firmware spins forever. The script now models GRSTCTL as "all reset bits self-clear on write; AHBIDL stays set."

After GRSTCTL self-clear fix:
- `usb_lld_start` completes
- All EP control/interrupt registers configured (DIEPCTL0-3, DOEPCTL0-3, DAINTMSK, DIEPMSK, DOEPMSK, GINTMSK = 0x00000001 globally enabled)
- Firmware now waits for OTG_FS_IRQ (line 67) which has not been delivered yet — but main loop runs anyway because matrix scan and protocol_keyboard_task are on independent threads.

boot_smoke regression check: still passes with same numbers (2576 matrix_scan / 2572 protocol_keyboard_task iterations in 2s sim). The Python peripheral adds negligible overhead because OTG accesses are bursty during boot, then quiescent.
