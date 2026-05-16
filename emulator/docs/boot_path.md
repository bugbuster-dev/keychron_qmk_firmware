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
