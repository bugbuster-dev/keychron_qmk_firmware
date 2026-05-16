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
