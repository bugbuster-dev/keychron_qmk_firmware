# QMK STM32F4 Hardware Emulator

Renode-based emulator for the unmodified Keychron Q3 Max QMK firmware (STM32F401 + LKBT51 SPI wireless co-MCU). Boots the real ELF, stubs the wireless co-MCU, injects matrix events from `info.json`, and captures HID reports.

**Status:** Phase 3 partial (boot, matrix injection, OTG FS stub, USB_ACTIVE shortcut, HID report capture).

**Design:** `docs/plans/2026-05-16-stm32f4-qmk-emulator-design.md`
**Implementation plan:** `docs/plans/2026-05-16-stm32f4-qmk-emulator-impl.md`
**Boot journal:** `docs/boot_path.md`

## What works

- **Boot:** Unmodified firmware reaches QMK main loop with matrix_scan running.
- **Matrix injection:** `sysbus.matrix ControlWrite` from scenarios injects keypresses; firmware's matrix scan sees them.
- **USB:** OTG FS PythonPeripheral handles W1C registers + GRSTCTL self-clear. USB_ACTIVE forced via `usb_endpoint_in_send` hook (v1.0 shortcut).
- **HID report capture:** `obqWriteTimeout` hook logs report bytes as hex strings.
- **Symbol resolution:** Firmware addresses resolved from ELF at runtime via `arm-none-eabi-nm` — no hard-coded addresses in scenarios.

## What does not work yet

- **LKBT51 SPI wiring:** Deferred (Task 2.3). `spiSend`/`spiExchange` patched to no-op; protocol stub (`Lkbt51Protocol`) exists but not wired to Renode SPI1.
- **Full USB enumeration:** Shortcutted (USB_ACTIVE forced on GAHBCFG.GINTMSK=1). No real bus-reset / SET_ADDRESS / SET_CONFIGURATION sequence.
- **HID report decoding:** Reports logged as hex strings only; no USB report descriptor parsing.
- **Real USB device on host OS:** Out of scope for v1.0. Planned for v2.0 via USB/IP.

## v1.0 scope

- Boot Q3 Max ANSI Encoder unmodified firmware.
- Stub LKBT51 over SPI1 with "no host paired, wired mode" replies.
- Drive matrix GPIOs from a Python injector.
- Minimal STM32 OTG FS device model + internal fake-host shim to reach USB-configured state.
- Capture and decode HID reports to log/file.

v1.0 does **not** expose a real USB HID device to the host OS. That arrives in v2.0 (USB/IP → `vhci-hcd` → `/dev/input/event*`).

## Dependencies

| Tool | Version | Purpose |
|---|---|---|
| Renode | 1.16.1 | MCU emulator |
| Python | 3.10+ | Peripheral stubs + scenario driver |
| `arm-none-eabi-gcc` | (already needed for QMK) | Build the firmware ELF |

Pinned versions in `.tool-versions`.

## Installing Renode

Portable tarball (no sudo needed):

```sh
curl -L -o /tmp/renode.tar.gz \
  https://github.com/renode/renode/releases/download/v1.16.1/renode-1.16.1.linux-portable-dotnet.tar.gz
tar -xzf /tmp/renode.tar.gz -C "$HOME"
mv "$HOME/renode_1.16.1-dotnet_portable" "$HOME/renode"
mkdir -p "$HOME/.local/bin"
ln -sf "$HOME/renode/renode" "$HOME/.local/bin/renode"
# Ensure ~/.local/bin is on PATH
```

Verify:

```sh
renode --version
# Renode v1.16.1.17033 ...
```

## Building the firmware

Stock build (the binary the emulator will boot in v1.0):

```sh
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron
```

Output ELF (consumed by Renode `.resc` script):

```
.build/keychron_q3_max_ansi_encoder_keychron.elf
```

Verified sizing (firmware fits comfortably in F401 256K flash + 64K SRAM):

```
text  data   bss
105K   3K   61K
```

Emu-diagnostic build (enables `EMULATOR_BUILD`, activates USART2 dbg_* tracing):
```sh
qmk compile -kb keychron/q3_max/ansi_encoder -km emu
```
The `emu` keymap is identical to `keychron` but adds `OPT_DEFS += -DEMULATOR_BUILD`.
Scenarios that assert on USART2 output (`REG`, `MAT`, `emu: load/unload`) require
this build. The stock `keychron` keymap produces no UART output in Renode.

## Running scenarios

```sh
# Boot smoke test (Phase 2):
python3 emulator/scenarios/boot_smoke.py
# End-to-end keypress → HID report (Phase 3):
python3 emulator/scenarios/type_hello.py
```

Scenarios resolve firmware symbol addresses from the ELF at runtime via `arm-none-eabi-nm`, so they survive firmware rebuilds without manual address updates.

## Layout

```
emulator/
├── README.md               (this file)
├── .tool-versions
├── renode/                 platform descriptions + Python peripherals
├── otg_fs/                 C# OTG FS device extension
├── scenarios/              Python demo & test scenarios
├── traces/                 reference USBmon captures from real hardware
└── docs/                   boot path notes, troubleshooting

keyboards/.../ansi_encoder/keymaps/emu/   (EMU_BUILD keymap, symlinks to keychron/)
```

Nothing under `emulator/` is consumed by the QMK build system. Everything outside `emulator/` is untouched.
