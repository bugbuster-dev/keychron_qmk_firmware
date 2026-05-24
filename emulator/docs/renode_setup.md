# Renode setup & emulator patches (Keychron Q3 Max)

How to install Renode and what patches/workarounds the `.resc` script
applies to make the stock Keychron Q3 Max firmware boot under emulation.

## Install Renode

### Linux (portable build, recommended)

```bash
cd ~/
wget https://github.com/renode/renode/releases/download/v1.16.1/renode-1.16.1.linux-portable.tar.gz
tar -xzf renode-1.16.1.linux-portable.tar.gz
mv renode_1.16.1_portable renode
echo 'export PATH="$HOME/renode:$PATH"' >> ~/.bashrc
source ~/.bashrc
renode --version   # expect 1.16.1
```

### Dependencies

- `arm-none-eabi-gcc` 13.x (build firmware)
- `arm-none-eabi-nm`, `arm-none-eabi-objdump` (used by sync scripts)
- Python 3.10+ with `qmk` CLI (build firmware)
- `mono` runtime (Renode requires .NET 8.0)

Verify: `arm-none-eabi-gcc --version && python3 -c "import qmk"`

## Repo layout

```
emulator/
├── renode/
│   ├── q3_max.resc        # Renode boot script (patches + hooks)
│   └── q3_max.repl        # Platform description (extends stm32f4.repl)
├── scenarios/
│   ├── boot_smoke.py      # Verifies boot reaches main loop
│   ├── sram_sticky_combo.py # Interactive SRAM behavior module harness
│   ├── test_sram_sticky_combo.py # Headless SRAM module regression tests
│   ├── type_hello.py      # End-to-end key injection
│   └── lib/
│       ├── renode_driver.py
│       └── symbol_resolver.py
└── scripts/
    ├── build_sram_module.py # Build relocated sticky_combo SRAM module
    └── sync_addrs.py      # Resync .resc addresses after firmware rebuild
```

## Build firmware

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
# Output: .build/keychron_q3_max_ansi_encoder_keychron.elf (~110 KB)
```

## Run emulator

**Interactive (with USART2 analyzer window):**
```bash
renode -e '$bin=@.build/keychron_q3_max_ansi_encoder_keychron.elf; \
           include @emulator/renode/q3_max.resc; start'
```

**Headless (terminal output only):**
```bash
renode --disable-xwt --plain -e \
  '$bin=@.build/keychron_q3_max_ansi_encoder_keychron.elf; \
   include @emulator/renode/q3_max.resc; \
   emulation RunFor "0:00:02.000"'
```

**At the Renode `(monitor)` prompt:**
```
sysbus WriteWord 0x200089f4 0x40       # inject KC_H (row=3, col=6)
sysbus WriteWord 0x200089f4 0x00       # release
sysbus ReadDoubleWord 0x200089e8 12    # read matrix[3]
pause / start / quit
```

## Address sync after rebuild

Firmware addresses shift on every rebuild. Sync with:
```bash
python3 emulator/scripts/sync_addrs.py
```

This updates:
- `emulator/renode/q3_max.resc` (boot-patch + hook addresses)
- `emulator/scenarios/lib/symbol_resolver.py` (`EXPECTED_ADDRESSES`)
- `emulator/scenarios/lib/renode_driver.py` (press_key hook PC)
- prints `g_module_sram` and `g_emu_module_cmd` for SRAM-module workflows

## SRAM sticky-combo module workflow

The interactive workflow for testing the compiled SRAM behavior module is:

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
python3 emulator/scripts/sync_addrs.py
python3 emulator/scripts/build_sram_module.py
python3 emulator/scenarios/sram_sticky_combo.py
```

`build_sram_module.py` builds
`qmk-tools/qmk/QMKata/module_examples/kbsm_sticky_combo/`, applies
R_ARM_ABS32 relocations against the firmware's actual `g_module_sram`
symbol, recomputes the MODL CRC, and writes:

```
.build/sticky_combo_module.bin
.build/sticky_combo_module.json
```

Do **not** trust the historical `0x2000F000` default for this firmware;
`g_module_sram` moves as `.bss` changes. The scenario refuses stale
module binaries whose sidecar JSON was relocated for a different slot
base.

The runtime loader path uses a separate staging buffer, not
`g_module_sram` directly:

1. Renode boots normally so ChibiOS clears `.bss`.
2. Host runs `sysbus LoadBinary @.build/sticky_combo_module.bin
   <g_emu_module_stage>`.
3. Host writes the module length to `g_emu_module_stage_len`.
4. Host writes command `1` to `g_emu_module_cmd`.
5. Firmware polls the command from `matrix_scan_kb`, calls
   `module_load(8, g_emu_module_stage, len)`, then the loader copies
   bytes into `g_module_sram`.

This staging buffer is required because `module_load_sram()` clears the
destination slot (`module_sram_clear()`) before copying. If the host
staged the bytes directly into `g_module_sram`, the clear step would
overwrite the source with `0xFF` before `memcpy`.

Runtime commands:

```
sysbus WriteByte <g_emu_module_cmd> 1   # load slot 8 from staged bytes
sysbus WriteByte <g_emu_module_cmd> 2   # unload slot 8
sysbus WriteByte <g_emu_module_cmd> 0   # clear edge detector before reusing cmd
```

Headless regression:

```bash
python3 emulator/scenarios/test_sram_sticky_combo.py
```

Current coverage:

- J+K combo arm consumes K.
- Timeout path (`500ms` Renode virtual ≈ `105ms` QMK timer) does not arm.
- Non-combo key L passes through both idle and active-combo states.
- Tap action in `ARMED_FOR_KEY1` fires KC_UP (`REG 52`).
- Tap action in `ARMED_FOR_KEY2` fires KC_DOWN (`REG 51`).
- Unload/reload works.

Note: Renode virtual time advances faster than QMK's 16-bit timer in
this emulation. The timeout test uses `500ms` virtual time to exceed the
module's `50ms` QMK window.

## Applied patches (in `q3_max.resc`)

### 1. F_SIZE OTP (F401CC reports 256 KB flash)

```
sysbus WriteWord 0x1FFF7A22 0x0100
```

ChibiOS reads `F_SIZE` half-word from OTP at `0x1FFF7A22` to size the
flash descriptor. Renode's STM32F4 model leaves this region as zero,
so `efl_lld_init` computes flash size = 0 and `backing_store_init`
panics. We write `0x0100` (= 256 KB) before reset.

### 2. `chSysPolledDelayX` no-op

```
sysbus WriteWord <chSysPolledDelayX> 0x4770   # bx lr
```

ChibiOS short delays busy-loop on the DWT CYCCNT (Cortex-M cycle
counter at `0xE0001004`). Renode's Cortex-M model does not implement
DWT, so reads return 0 and the loop spins forever. We replace the
first instruction with `bx lr` (return immediately). Callers tolerate
over-fast delays — it is a minimum, not a fixed sleep.

### 3. `spiSend` / `spiExchange` no-op

```
sysbus WriteDoubleWord <spiSend>     0x47702000   # movs r0,#0; bx lr
sysbus WriteDoubleWord <spiExchange> 0x47702000
```

ChibiOS SPI HAL initiates DMA transfers then blocks on a binary
semaphore signalled by the DMA TC ISR. Renode's DMA model does not
generate completion IRQs into our SPI peripheral, so the thread
sleeps forever. We patch both functions to return success with no
data transferred. Cost: silent RGB LED driver and LKBT51 SPI writes
(both out of v1.0 scope).

### 4. `rtc_enter_init` no-op

```
sysbus WriteWord <rtc_enter_init> 0x4770
```

ChibiOS RTC HAL initialises the calendar by setting `RTC_ISR.INIT`
(bit 7) then polling `RTC_ISR.INITF` (bit 6). We replaced Renode's
`STM32F4_RTC` peripheral with plain memory (the real model crashes
on the firmware's `DateRegister=0` writes), so `INITF` never asserts
and the poll spins forever. Q3 Max uses RTC only for sleep
timestamps which are not exercised in v1.0.

### 5. NVIC `RETTOBASE` workaround

```
sysbus SetHookAfterPeripheralRead sysbus.nvic """
if offset == 0xD04:
    value = value | 0x800
"""
```

Renode's NVIC model returns `ICSR` bit 11 (`RETTOBASE`) = 0
unconditionally. ChibiOS port-armv7m's `__port_irq_epilogue` tests
this bit to decide whether to run the preemption check on ISR exit.
With bit 11 stuck at 0, ChibiOS skips the preemption check and
sleeping threads are never resumed — boot hangs in the first
`chThdSleep`. We patch the bit on every `ICSR` read.

### 6. USART2 enable for `dprintf` tracing

```
sysbus WriteDoubleWord 0x40023840 0x00020000   # RCC APB1ENR bit 17 = USART2 clock
sysbus WriteDoubleWord 0x4000440c 0x0000200c   # USART2 CR1: UE=bit13, TE=bit3
```

Pre-enables USART2 so firmware can write directly to its data
register (`0x40004404`) without bus faults from unclocked peripheral
access. QMKata's `sendchar()` is patched to tee output to USART2 in
addition to QMKata raw HID — all `xprintf`/`dprintf` calls show up
in the Renode `usart2` analyzer.

### 7. `USBD1.state` force-active

```
cpu AddHook <usb_endpoint_in_send> """
sb.WriteByte(<USBD1>, 4)
sb.WriteByte(<USBD1> + 0x4F, 1)
"""
```

ChibiOS's OTG FS peripheral sets `USBD1.state = USB_ACTIVE` (4) when
`GAHBCFG.GINTMSK` is enabled, but `usbStart` then resets it to 2
(`USB_READY`) before HID reports queue up. We patch it back to 4 on
every entry to `usb_endpoint_in_send` so its `USB_ACTIVE` gate
passes and the report bytes flow into the TX FIFO.

### 8. HID-EP-WRITE capture

```
sysbus.cpu AddHook <obqWriteTimeout> """
... ReadByte(ptr + i) for i in range(sz) ...
self.Log(LogLevel.Warning, 'HID-EP-WRITE ep=... sz=... %s' % bytes_str)
"""
```

`usb_endpoint_in_send` pushes report data into the per-EP output
queue via `obqWriteTimeout`. The ChibiOS USB ISR would normally
drain the queue into the OTG FS TX FIFO, but we never deliver USB
IRQs in v1.0. Instead we hook `obqWriteTimeout` entry, read its
argument buffer (`r1`, size in `r2`), and log the bytes as a hex
string. This is the headless equivalent of seeing HID reports on
a real host.

### 9. RTC peripheral → plain memory (in `.repl`)

```
rtc: @ none
fake_rtc: Memory.MappedMemory @ sysbus 0x40002800
    size: 0x400
```

The STM32F4_RTC model in Renode crashes on `DateRegister=0` writes
(year=month=day=0 is not a representable `DateTime`). The Q3 Max
wireless code touches RTC for sleep state. We remove the model and
remount that address range as plain memory so register reads/writes
are silent.

### 10. OTG FS Python peripheral (inline in `.repl`)

```
otg_fs: Python.PythonPeripheral @ sysbus 0x50000000
    size: 0x4000
    initable: true
    script: '''<inlined IronPython 2.7 script>'''
```

Maps the full OTG FS region (Global + Device + PWRCLK + FIFOs = 16 KB),
replacing the upstream `stm32f4.repl` `USB:RESET` tag which only
covered `0x50000010..0x5000003F`. The inline script:

- Handles W1C (write-1-to-clear) interrupt-status registers (`GINTSTS`,
  `DIEPINT(N)`, `DOEPINT(N)`).
- Models `GRSTCTL` "instant completion" — clears `CSRST/HSRST/FCRST/
  RXFFLSH/TXFFLSH` immediately and keeps `AHBIDL=1`.
- Captures per-EP TX FIFO writes (offsets `0x1000..0x6FFF`) into
  per-EP queues (`ep_in_buffers[ep]`).
- Detects soft-connect via `DCTL.SDIS=0` (offset `0x804`).
- Forces `USBD1.state = USB_ACTIVE` + `configuration = 1` on
  `GAHBCFG.GINTMSK=1` (firmware finished `usb_lld_start`) — the v1.0
  shortcut that bypasses the full bus-reset / `SET_ADDRESS` /
  `GET_DESCRIPTOR` / `SET_CONFIGURATION` enumeration sequence we would
  otherwise have to drive via NVIC IRQ injection.

Background notes in `emulator/otg_fs/SURVEY.md`. IronPython 2.7.12
syntax (no f-strings, no Python 3 typing). Inlined as `script:`
rather than `filename:` because Renode resolves relative `filename:`
against its install dir, not cwd.

### 11. Matrix injector stub (inline in `.repl`)

```
matrix: Python.PythonPeripheral @ sysbus 0x70000000
    size: 0x100
    initable: true
    script: '''<minimal stub>'''
```

Originally intended for keypress injection via `sysbus.matrix
ControlWrite (row*32+col) 1/0`. Now reduced to a trigger for
`IsInit` logging — key injection works by patching the firmware's
global matrix SRAM directly via CPU hooks (`press_key()` in
`renode_driver.py`). The `IsWrite`/`IsRead` handlers are no-ops.

## Standalone Python peripheral files (not currently loaded)

These live under `emulator/renode/peripherals/` but are **not
referenced** by the current `q3_max.repl`:

| File | Purpose | Status |
|------|---------|--------|
| `matrix_injector.py` | Pre-injection version of the matrix peripheral | Superseded by inline stub + CPU hook injection |
| `lkbt51_stub.py` | LKBT51 wireless co-MCU SPI device stub | Phase 2 — wireless emulation not yet wired up |
| `test_matrix_injector.py` | Pytest unit tests for the injector | Tests still pass; module dead-code |
| `test_lkbt51_stub.py` | Pytest unit tests for LKBT51 stub | Tests still pass; module dead-code |

To re-enable, change `script: '''...'''` in `q3_max.repl` to
`filename: '@emulator/renode/peripherals/...py'` (absolute path needed
because Renode resolves relative filenames against its install dir).

## No patches to Renode itself

All emulation customisation lives in this repo. Renode 1.16.1 is used
as the stock portable build — no source modifications, no plugins, no
custom assemblies. Adaptation happens entirely through:

- `emulator/renode/q3_max.repl` (platform description)
- `emulator/renode/q3_max.resc` (boot script with `sysbus` writes and
  `cpu AddHook` Python snippets)
- Inline `PythonPeripheral` scripts in `.repl` (IronPython 2.7.12)

The "patches" in §1–8 are firmware-memory writes (`bx lr` over
function prologues) or CPU-PC hooks executed from outside the firmware
image; they never touch Renode's binaries.

## Firmware-side changes for emulation

These are checked into the firmware tree (not generated by `.resc`):

- `keyboards/keychron/q3_max/q3_max_user.c` — defines `dbg_putc`,
  `dbg_print`, `dbg_hex8`, the SRAM-module staging buffer and command
  byte. Overrides `matrix_scan_kb` to log `matrix[3]` deltas to USART2
  and poll `g_emu_module_cmd`.
- `keyboards/keychron/qmkata/QMKata.cpp` — `sendchar()` calls
  `dbg_putc()` after pushing to QMKata stream (tee).
- `quantum/action.c` — `register_code()` prefix prints `REG xx` to
  USART2 on each keycode registration.
- `keyboards/keychron/common/module/kbsm_env.c` — `env->xprintf`
  now formats through `xprintf`, so SRAM modules can emit diagnostics
  through the same QMKata + USART2 tee.

## Verify install

```bash
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
python3 emulator/scenarios/boot_smoke.py
```

Expected output:
```
main() reached: True
keyboard_init reached: True
matrix_init reached: True
wireless_init reached: True
matrix_scan iterations: ~2570
protocol_keyboard_task iterations: ~2566
boot_smoke: OK
```

## Symbols used (resolved from ELF at scenario startup)

| Symbol | Purpose |
|--------|---------|
| `chSysPolledDelayX` | Patch to `bx lr` |
| `spiSend`, `spiExchange` | Patch to `movs r0,#0; bx lr` |
| `rtc_enter_init` | Patch to `bx lr` |
| `usb_endpoint_in_send` | Hook to force `USBD1.state = USB_ACTIVE` |
| `obqWriteTimeout` | Hook to log HID-EP-WRITE bytes |
| `USBD1` | Target of state-force write |
| `matrix_scan` | Diagnostic hook + injection-point offset |
| `protocol_keyboard_task`, `main`, `keyboard_init`, `matrix_init`, `wireless_init` | Boot diagnostic hooks |
| `g_module_sram` | SRAM slot-8 base for module relocation |
| `g_emu_module_stage` | Renode staging buffer for module bytes |
| `g_emu_module_stage_len` | Staged module byte length |
| `g_emu_module_cmd` | Runtime load/unload command byte |
