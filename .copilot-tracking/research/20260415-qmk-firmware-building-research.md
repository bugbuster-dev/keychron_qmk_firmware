<!-- markdownlint-disable-file -->

# Task Research Notes: QMK Firmware Building

## Research Executed

### File Analysis

- `readme.md` (root)
  - Official Keychron QMK firmware repository for 63+ Keychron and Lemokey keyboards
  - Two main approaches: Keychron Launcher (no code) or build from source
  - Build commands: `qmk compile -kb <keyboard> -km <keymap>` or `make <keyboard>:<keymap>`

- `Makefile` (root)
  - Main build system using GNU Make
  - Syntax: `<keyboard>:<keymap>:<target>`
  - Key targets: `all` (compile), `flash` (compile and upload), `clean`, `distclean`
  - Supports parallel compilation with `-jx` flag

- `docs/getting_started_make_guide.md`
  - Detailed make command syntax and options
  - Target options: `all`, `flash`, `dfu`, `teensy`, `avrdude`, `dfu-util`, `bootloadhid`, `clean`, `distclean`
  - Developer targets: `show_path`, `dump_vars`, `objs-size`, `show_build_options`, `check-md5`

- `docs/newbs_building_firmware.md`
  - Step-by-step guide for building first firmware
  - QMK CLI commands: `qmk config`, `qmk new-keymap`, `qmk compile`
  - Environment setup with default keyboard and keymap configuration

- `docs/cli_commands.md`
  - Comprehensive QMK CLI documentation
  - `qmk compile` with options: `-kb <keyboard>`, `-km <keymap>`, `-c` (clean), `-j <num_jobs>` (parallel)
  - Directory-aware compilation (auto-detects keyboard/keymap from current path)

- `docs/faq_build.md`
  - Build troubleshooting and common issues
  - Linux udev rules for device permissions
  - Windows driver installation with Zadig
  - EEPROM reset procedures for ARM-based boards

- `keyboards/keychron/q3_max/readme.md` (example keyboard)
  - Board-specific build targets: `keychron/q3_max/ansi_encoder:keychron`
  - Flash commands: `make keychron/q3_max/ansi_encoder:keychron:flash`
  - Reset key procedure: Hold Esc or reset button, connect USB

- `requirements.txt`
  - Python dependencies: argcomplete, colorama, dotty-dict, hid, hjson, jsonschema, milc, pygments, pyserial, pyusb, pillow

### Code Search Results

- `make` command patterns
  - Found examples: `make keychron/q1_he/ansi_encoder:keychron`, `make keychron/k8_pro/ansi/rgb:keychron`
  - Flash pattern: `make keychron/v1_max/ansi_encoder:keychron:flash`

- Keyboard directory structure
  - Located at: `keyboards/keychron/` with 43+ board variants
  - Each board has subfolders for layout variants (e.g., `ansi_encoder`, `iso_encoder`)
  - Keymap files in `keymaps/` subdirectory (`.c` or `.json` format)

### External Research

- #fetch:https://docs.qmk.fm/#/getting_started_build_tools
  - QMK build environment setup documentation
  - Cross-platform support: Windows, macOS, Linux
  - QMK CLI installation: `python3 -m pip install qmk`

- #fetch:https://docs.qmk.fm/#/getting_started_make_guide
  - Make command reference and examples
  - Build targets and customization options

### Project Conventions

- Standards referenced: QMK firmware build conventions
- Instructions followed: Keychron QMK repository structure

## Key Discoveries

### Project Structure

```
keychron_qmk_firmware/
├── keyboards/keychron/          # Keychron board definitions
│   ├── q3_max/                  # Example: Q3 Max keyboard
│   │   ├── ansi_encoder/        # ANSI layout with encoder
│   │   │   ├── keymaps/         # Keymap files
│   │   │   │   └── keychron/    # Official keymap
│   │   │   │       └── keymap.c # Keymap source
│   │   ├── info.json            # Board metadata
│   │   ├── config.h             # Board configuration
│   │   └── rules.mk             # Build rules
├── .build/                      # Build output directory
├── Makefile                     # Main build file
└── paths.mk                     # Build paths
```

### Implementation Patterns

**Build Command Syntax:**
```bash
make <keyboard_path>:<keymap>:<target>
```

**QMK CLI Syntax:**
```bash
qmk compile -kb <keyboard_path> -km <keymap>
```

**Directory-Aware Compilation:**
```bash
cd keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron
qmk compile  # Auto-detects keyboard and keymap
```

### Complete Examples

**Basic Compilation (Make):**
```bash
# Compile specific keyboard and keymap
make keychron/q3_max/ansi_encoder:keychron

# Compile and flash to keyboard
make keychron/q3_max/ansi_encoder:keychron:flash

# Clean build artifacts
make keychron/q3_max/ansi_encoder:keychron:clean
```

**Basic Compilation (QMK CLI):**
```bash
# Install QMK CLI
python3 -m pip install qmk

# Setup environment
qmk setup Keychron/qmk_firmware

# Compile firmware
qmk compile -kb keychron/q3_max/ansi_encoder -km keychron

# Flash to keyboard
qmk flash -kb keychron/q3_max/ansi_encoder -km keychron
```

**Advanced Compilation:**
```bash
# Parallel compilation (4 jobs)
qmk compile -j 4 -kb keychron/q3_max/ansi_encoder -km keychron

# Clean and compile
qmk compile -c -kb keychron/q3_max/ansi_encoder -km keychron

# Compile all keymaps for a keyboard
make keychron/q3_max/ansi_encoder:all
```

### Configuration Examples

**QMK Config (set defaults):**
```bash
# Set default keyboard
qmk config user.keyboard=keychron/q3_max/ansi_encoder

# Set default keymap
qmk config user.keymap=keychron

# Then compile without arguments
qmk compile
```

**Create New Keymap:**
```bash
# Create new keymap from default
qmk new-keymap -kb keychron/q3_max/ansi_encoder

# Creates: keyboards/keychron/q3_max/ansi_encoder/keymaps/<username>/
```

### Technical Requirements

**Build Dependencies:**
- Python 3.8+ with pip
- QMK CLI (`pip install qmk`)
- GNU Make
- Platform-specific toolchains:
  - **AVR**: avr-gcc, avr-objcopy, avr-objdump, dfu-programmer
  - **ARM**: arm-none-eabi-gcc, arm-none-eabi-objcopy, openocd/dfu-util
  - **RP2040**: arm-none-eabi-gcc, rp2040-tools

**Platform Requirements:**
- **Linux**: udev rules for device access (`/etc/udev/rules.d/50-qmk.rules`)
- **Windows**: Zadig for DFU drivers or QMK Driver Installer
- **macOS**: No additional drivers typically needed

**Build Output:**
- `.hex` files for AVR microcontrollers
- `.bin` files for ARM/RP2040 microcontrollers
- Output location: `.build/` directory or root folder

## Recommended Approach

**For Quick Firmware Builds:**
1. Use QMK CLI for automated environment setup and cross-platform compatibility
2. Install: `python3 -m pip install qmk`
3. Setup: `qmk setup Keychron/qmk_firmware`
4. Compile: `qmk compile -kb <keyboard> -km <keymap>`
5. Flash: `qmk flash -kb <keyboard> -km <keymap>`

**For Advanced Control:**
1. Use `make` directly for fine-grained build control
2. Syntax: `make <keyboard>:<keymap>:<target>`
3. Enable parallel builds: `make -j4 <keyboard>:<keymap>`

**For Non-Technical Users:**
1. Use Keychron Launcher (browser-based, no compilation needed)
2. Visit: https://launcher.keychron.com/
3. Requires Chromium-based browser (Chrome, Edge, Brave, Opera, Vivaldi)

## Implementation Guidance

- **Objectives**: Build custom QMK firmware for Keychron keyboards
- **Key Tasks**:
  1. Install build dependencies (QMK CLI or platform toolchains)
  2. Select keyboard path and keymap
  3. Compile firmware using `qmk compile` or `make`
  4. Flash to keyboard using `qmk flash` or `make :flash`
  5. Reset keyboard if needed (hold Esc + connect USB)
- **Dependencies**:
  - Python 3.8+ and pip
  - QMK CLI (recommended) or GNU Make + platform toolchains
  - USB connection to keyboard
  - Proper USB permissions (Linux udev rules, Windows drivers)
- **Success Criteria**:
  - Build completes without errors
  - Firmware file (.hex/.bin) generated
  - Keyboard responds after flashing
  - Custom keymap functions correctly
