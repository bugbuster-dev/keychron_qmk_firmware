# QMK STM32F4 Hardware Emulator

Renode-based emulator for the unmodified Keychron Q3 Max QMK firmware (STM32F401 + LKBT51 SPI wireless co-MCU). Boots the real ELF, stubs the wireless co-MCU, injects matrix events from `info.json`, and captures HID reports.

**Status:** Phase 0 (toolchain sanity).

**Design:** `docs/plans/2026-05-16-stm32f4-qmk-emulator-design.md`
**Implementation plan:** `docs/plans/2026-05-16-stm32f4-qmk-emulator-impl.md`

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

Emu-diagnostic build (extra logging via `CONSOLE_ENABLE` + `DEBUG_MATRIX_SCAN_RATE_ENABLE`): see `build/README.md` (added in Phase 1, Task 1.5).

## Running scenarios

(Populated as Phases 2–5 land.)

```sh
# Phase 2:
python emulator/scenarios/boot_smoke.py
# Phase 4:
python emulator/scenarios/type_hello.py
# Phase 5:
python emulator/scenarios/combo_test.py
```

## Layout

```
emulator/
├── README.md               (this file)
├── .tool-versions
├── renode/                 platform descriptions + Python peripherals
├── otg_fs/                 C# OTG FS device extension
├── scenarios/              Python demo & test scenarios
├── traces/                 reference USBmon captures from real hardware
├── build/                  emu-diagnostic build profile
└── docs/                   boot path notes, troubleshooting
```

Nothing under `emulator/` is consumed by the QMK build system. Everything outside `emulator/` is untouched.
