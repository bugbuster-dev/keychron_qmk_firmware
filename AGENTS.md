# Keychron QMK Firmware — Agent Instructions

## Repo Overview

Keychron's fork of [qmk/qmk_firmware](https://github.com/qmk/qmk_firmware). Tracks upstream with Keychron-specific boards under `keyboards/keychron/` and `keyboards/lemokey/`. 63+ boards across Q, Q HE, Q Max, K, V, C Pro, S, X, and Lemokey series. Licensed GPLv2.

## Build Commands

```bash
# Compile (make or qmk CLI — both work):
make keychron/q3_max/ansi_encoder:keychron
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron

# Flash (DFU mode):
make keychron/q3_max/ansi_encoder:keychron:flash

# Clean build artifacts:
make clean          # removes .build/
make distclean      # also removes *.bin, *.hex, *.uf2 in root
```

Rule format: `make <keyboard>/<variant>:<keymap>[:target]`
- `keyboard` — path under `keyboards/` (e.g. `keychron/q3_max`)
- `variant` — layout subfolder (e.g. `ansi_encoder`, `iso_encoder`)
- `keymap` — folder under `keymaps/` (e.g. `keychron` for VIA, `default` for no VIA)
- `target` — optional: `flash`, `clean`, `dfu`, etc.

## Release Build

Use `scripts/release.sh` to build all firmware variants and SRAM modules, then package for GitHub release.

```bash
# Build + package only → .release/keychron-q3-max-<date>.tar.gz
./scripts/release.sh

# Also create git tag and push
./scripts/release.sh --tag v0.2.0

# Also create GitHub release (requires gh CLI)
./scripts/release.sh --release v0.2.0
```

Packages: Q3 Max ANSI + ISO `.bin` firmware, and SRAM modules (`sticky_combo`, `dyad`, `autotext`, `holdseq`, `vim_modal`).

## QMK Tools

Host-side tooling repo: `~/qmk/qmk-tools/` (https://github.com/bugbuster-dev/qmk-tools)

Contains: QMKata GUI, SRAM module build pipeline (`ModuleBuild.py`), module examples (`kbsm_dyad`, `kbsm_holdseq`, `kbsm_autotext`), and authoring documentation.

```bash
# Run QMKata GUI
python3 ~/qmk/qmk-tools/qmk/QMKata/QMKata.py

# Build SRAM module (from firmware repo)
python3 emulator/scripts/build_sram_module.py --feature <name>
```

## Q3 Max Quick Reference

- **MCU**: STM32F401, **Bootloader**: stm32-dfu, **USB VID**: `0x3434`
- **Variants**: `ansi_encoder` (PID `0x0830`, 87 keys), `iso_encoder` (PID `0x0831`, 88 keys)
- **Keymaps**: `keychron` (VIA enabled), `default` (no VIA)
- **RGB driver**: SNLED27351 over SPI1 (2 chips, 12-channel scan)
- **Wireless**: LKBT51 — 3 BT profiles + 2.4G
- **DIP switch**: Pin C9 toggles MAC layers (0/1) vs Win layers (2/3)
- **Reset to bootloader**: Disconnect USB, toggle mode switch to "Cable", hold Esc (or reset button under spacebar), plug USB
- **Flash layout**: IVT at S0 (`0x08000000`), EEPROM cache at S1, module reserve at S2-S3, firmware body at S4-S5. See `keyboards/keychron/q3_max/flash_layout.md`
- **EEPROM**: Embedded flash wear-leveling, 3KB logical / 6KB physical

## Build Include Chain (rules.mk Resolution)

Board `rules.mk` files include shared infrastructure. Resolution order for Q3 Max:

```
q3_max/rules.mk
  ├── keyboards/keychron/common/wireless/wireless.mk      # LKBT51 stack
  ├── keyboards/keychron/common/keychron_common.mk         # debounce, language, snap_click, rgb, combo, tap_dance, leader, module_loader
  └── keyboards/keychron/qmkata/qmkata.mk                  # Firmata over raw HID

<variant>/rules.mk  (e.g. ansi_encoder/rules.mk)
  └── KEYCHRON_RGB_ENABLE = yes  → triggers rgb/rgb.mk

keymaps/<keymap>/rules.mk  (e.g. keychron/rules.mk)
  └── VIA_ENABLE = yes
```

**Keychron common features** are conditionally pulled in via `keychron_common.mk` based on flags:
- `DEBOUNCE_TYPE=custom` → `debounce/debounce.mk`
- `KEYCHRON_RGB_ENABLE=yes` + `RGB_MATRIX_ENABLE=yes` → `rgb/rgb.mk`
- `COMBO_ENABLE=yes` → `combo/combo_eeprom.mk`
- `TAP_DANCE_ENABLE=yes` → `tap_dance/tap_dance_eeprom.mk`
- `LEADER_ENABLE=yes` → `leader/leader_eeprom.mk`
- `MODULE_LOADER_ENABLE=yes` → `module/module_loader.mk`

## Key Source Files (Q3 Max)

| File | Purpose |
|------|---------|
| `q3_max.c` | Keyboard-level handlers: DIP switch, RDP modifier fix, power LED |
| `q3_max_user.c` | User overrides: module safe mode, VIA macros, QMKata task |
| `ansi_encoder/ansi_encoder.c` | ANSI RGB LED config (87 LEDs, CAPS at index 50) |
| `iso_encoder/iso_encoder.c` | ISO RGB LED config (88 LEDs, CAPS at index 51) |
| `ansi_encoder/keymaps/keychron/keymap.c` | Full keymap: 4 layers, tap dance, combos, leader keys |
| `qmkata_sysex_handler.c` | QMKata sysex command handlers |
| `config.h` | SPI, RGB, wireless pin config; optional `SYSCLK_84MHZ` |

## Key Source Files (Shared Keychron)

| Path | Purpose |
|------|---------|
| `keyboards/keychron/common/keychron_common.c/h` | Shared keyboard logic |
| `keyboards/keychron/common/keychron_task.c/h` | Periodic task scheduler |
| `keyboards/keychron/common/keychron_raw_hid.c/h` | Raw HID transport |
| `keyboards/keychron/common/wireless/` | LKBT51 wireless stack (BT, 2.4G, battery, LPM) |
| `keyboards/keychron/common/debounce/` | Custom debounce engine |
| `keyboards/keychron/common/module/` | Runtime .qmk module loader |
| `keyboards/keychron/qmkata/` | Firmata protocol parser |

## QMKata (Firmata over Raw HID)

Development protocol for runtime debugging and module management. Supported sysex IDs:
- `RGB_MATRIX_BUF` — set per-LED colors from host
- `DEFAULT_LAYER` — get/set active layer
- `MACWIN_MODE` — get/set MAC/Win mode
- `STATUS` — battery, DIP switch, matrix state
- `COMBO` / `TAP_DANCE` / `LEADER` — dynamic feature management
- `MODULE` — load/unload runtime .qmk modules
- `CLI` — memory/EEPROM read/write

## Safe Mode

Hold DELETE at boot to skip module activation. Recovers from buggy `.qmk` modules.

## Submodules

Required for building. Sync with:
```bash
qmk git-submodule
# or
git submodule update --init --recursive
```

Key submodules: `lib/chibios`, `lib/chibios-contrib`, `lib/lufa`, `lib/lvgl`, `lib/pico-sdk`, `lib/googletest`.

## Testing & Linting

```bash
# C unit tests (requires submodules):
qmk test-c

# Python tests:
qmk pytest

# Lint a keyboard:
qmk lint --keyboard keychron/q3_max
qmk info -l --keyboard keychron/q3_max

# Format (core C, Python, text):
qmk format-c --core-only -a
qmk format-python -a
qmk format-text <files>
```

Python linting uses flake8 (ignore E501, E231; max complexity 16). Formatting uses yapf (column limit 256, 4-space indent). Config in `setup.cfg`.

## Architecture Notes

- **Layer selection**: DIP switch selects MAC (layers 0/1) vs Win (layers 2/3). FN key toggles base↔FN within the selected OS pair.
- **Dynamic features** (tap dance, combos, leader keys) are EEPROM-persisted via wear-leveling driver. Defaults loaded on first boot.
- **Modifier fix**: `register_code16`/`unregister_code16` overridden in `q3_max.c` to send modifiers sequentially for RDP compatibility.
- **Host RGB buffer**: Host sets per-LED colors via QMKata; rendered in `rgb_matrix_host_buf_render()` during `keychron_task_kb()`.
- **Keychron Launcher**: Browser-based config tool at https://launcher.keychron.com/ — no firmware build needed for basic remapping.

## QMK CLI Setup

```bash
python3 -m pip install qmk
qmk setup --clone-url git@github.com:Keychron/qmk_firmware.git
```

See [QMK build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) for full instructions.

## Contributing

See `docs/contributing.md`. Key rules:
- Compile locally before submitting: `make <kb>:<keymap>` must succeed
- Separate PRs per logical unit; no keymaps or userspace contributions
- Format code before committing; check whitespace with `git diff --check`
