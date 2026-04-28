# Keychron Q3 Max — Code Overview

> Generated: 2026-04-28  
> Target: STM32F401 (ARM Cortex-M4)

---

## Architecture Overview

```
keyboards/keychron/q3_max/
├── Base Layer (shared configuration)
│   ├── q3_max.c              # Keyboard-level event handlers
│   ├── q3_max_user.c         # User overrides & default data
│   ├── config.h              # Base C configuration
│   ├── rules.mk              # Base build rules
│   ├── info.json             # QMK JSON keyboard definition
│   ├── board.h               # MCU GPIO pin overrides
│   ├── mcuconf.h / halconf.h # ChibiOS HAL configuration
│   ├── debug_user.c/h        # Debug utilities
│   └── dynld_func.h          # Dynamic module loader interface
│
├── Variant: ansi_encoder/    # 87 keys, ANSI layout (PID 0x0830)
│   ├── ansi_encoder.c        # RGB LED configuration
│   ├── config.h              # Variant-specific config
│   ├── rules.mk              # KEYCHRON_RGB_ENABLE
│   ├── keyboard.json         # Layout definition
│   └── keymaps/
│       ├── default/          # No VIA
│       └── keychron/         # Full VIA support
│
├── Variant: iso_encoder/     # 88 keys, ISO layout (PID 0x0831)
│   ├── iso_encoder.c
│   ├── config.h
│   ├── rules.mk
│   ├── keyboard.json
│   └── keymaps/
│       ├── default/
│       └── keychron/
│
└── via_json/                 # VIA layout editor definitions
    ├── q3_max_ansi_encoder.json
    └── q3_max_iso_encoder.json
```

### External Dependencies

```
keyboards/keychron/common/    # Shared Keychron infrastructure
├── wireless/                 # LKBT51 wireless stack
├── rgb/                      # Custom RGB implementation
├── debounce/                 # Custom debounce engine
├── combo/                    # Dynamic combo system
├── tap_dance/                # Dynamic tap dance
├── leader/                   # Dynamic leader keys
├── module/                   # Runtime module loader
├── language/                 # OS language toggle
├── snap_click/               # Key snap click
├── keychron_common.c/h       # Shared keyboard logic
├── keychron_raw_hid.c/h      # Raw HID transport
└── keychron_task.c/h         # Keyboard task scheduler

keyboards/keychron/qmkata/    # QMKata integration (Firmata over raw HID)
└── QMKata.*                  # Firmata parser, sysex handlers
```

---

## Core Components

### 1. Base Keyboard Layer (`q3_max.c`)

**Purpose**: Keyboard-level event handling and system overrides

**Key Functions**:
- `keyboard_post_init_kb()` - Initialize power-on indicator LED
- `keychron_task_kb()` - Run periodic keyboard tasks (power LED, wireless idle)
- `dip_switch_update_kb()` - Handle DIP switch (MAC/Win layer selection)
- `register_code16()` / `unregister_code16()` - **Critical fix**: Send modifiers sequentially for RDP compatibility

**Important Details**:
- Power-on indicator blinks for 3 seconds then turns off (battery low LED)
- DIP switch (pin C9) toggles between MAC layers (0/1) and Win layers (2/3)
- Modifier key fix ensures RDP clients correctly detect shortcuts

---

### 2. User Layer (`q3_max_user.c`)

**Purpose**: User overrides, module loading, QMKata integration

**Key Functions**:
- `keyboard_post_init_user()` - Initialize EEPROM-backed features (leader, tap dance, combos)
- `keyb_user_set_macwin_mode()` / `keyb_user_get_macwin_mode()` - Override MAC/Win mode
- `dip_switch_update_user()` - Track DIP switch state for MAC/Win mode
- `keychron_task_user()` - Call QMKata task handler
- `rgb_matrix_host_buf_render()` - Render RGB colors from host buffer

**Security Feature**:
- **Safe mode**: If DELETE key is held at boot, skip module activation to allow recovery from buggy modules

**VIA Macros**:
- 16 macro slots (default: `"py` copy, `"pp` paste)
- Loaded to EEPROM on first boot

---

### 3. RGB Matrix Configuration

**Driver**: SNLED27351 (12-channel LED driver over SPI1)

**Files**:
- `ansi_encoder/ansi_encoder.c` / `iso_encoder/iso_encoder.c`
- `qmkata_rgb_matrix_user.c` - Dynamic animation support

**Configurations**:
| Variant | LED Count | CAPS_LOCK Index | Layout Macro |
|---------|-----------|-----------------|--------------|
| ANSI    | 87        | 50              | `LAYOUT_tkl_ansi` |
| ISO     | 88        | 51              | `LAYOUT_iso_88` |

**LED Configuration**:
- 2 drivers (0 and 1) with separate R/G/B pin mappings
- Custom `g_led_config` matrix-to-LED mapping
- Default per-key HSV colors (RED for modifiers, BLUE for home row, YELLOW for others)
- Mixed RGB regions for gradient effects

---

### 4. Variant-Specific Source (`ansi_encoder.c` / `iso_encoder.c`)

**Purpose**: Define LED matrix for each layout variant

**Key Data**:
```c
snled27351_led_t g_snled27351_leds[]  // Driver, R/G/B pin locations
led_config_t g_led_config             // Matrix-to-LED index mapping
HSV default_per_key_led[]             // Default RGB colors
uint8_t default_region[]              // Mixed RGB region flags
```

**Differences**:
- **ANSI**: 87 LEDs, missing LED at position 13 (no numpad 0), CAPS at index 50
- **ISO**: 88 LEDs, includes ISO Enter (spans rows 2-3) and ISO Backslash, CAPS at index 51

---

### 5. Keymap (`ansi_encoder/keymaps/keychron/keymap.c`)

**Purpose**: User-defined keymap with dynamic features

**Layer Structure**:
| Layer | Name | Purpose |
|-------|------|---------|
| 0 | `MAC_BASE` | macOS default (BT switching, RGB controls) |
| 1 | `MAC_FN` | macOS FN layer (F1-F12, RGB controls) |
| 2 | `WIN_BASE` | Windows default (Win key support) |
| 3 | `WIN_FN` | Windows FN layer (numpad on ANSI) |

**Keymap Features**:
- `MO()` momentary layer toggle (FN keys)
- DIP switch selects MAC (0/1) or Win (2/3) base layers
- Encoder controls volume (base) or RGB value (FN)

**Dynamic Features**:
- **Tap Dance**: `TD(TD_ESC)` - Single tap ESC, double tap Ctrl+Alt+Home
- **Combos**: Default: Ctrl+A (ZX), Ctrl+C (XS), Ctrl+V (CV), Ctrl+X (VF), Ctrl+Z (XD)
- **Leader Keys**: Default: `VC` = Media Center, `VP` = Print Screen
- All dynamic features are EEPROM-persistent

**Process Record Flow**:
```
process_record_user()
  → module_dispatch_process_record()      [module loader]
  → leader tracking                       [leader key]
  → process_record_keychron_common()      [Keychron features]
```

---

### 6. Build System (`rules.mk` Chain)

**Resolution Order** (top → bottom):
```
q3_max/rules.mk
  ├── wireless/wireless.mk                # LKBT51 stack
  │   └── LKBT51, battery, RTC, LPM
  │
  ├── keychron_common.mk                  # Shared logic
  │   ├── debounce/debounce.mk
  │   ├── language/language.mk
  │   ├── snap_click/snap_click.mk
  │   ├── rgb/rgb.mk                      [KEYCHRON_RGB_ENABLE]
  │   ├── combo/combo_eeprom.mk           [DYNAMIC_COMBO_ENABLE]
  │   ├── tap_dance/tap_dance_eeprom.mk   [DYNAMIC_TAP_DANCE_ENABLE]
  │   ├── leader/leader_eeprom.mk         [DYNAMIC_LEADER_ENABLE]
  │   └── module/module_loader.mk         [MODULE_LOADER_ENABLE]
  │
  └── qmkata/qmkata.mk                    # Firmata over raw HID
```

**Variant `rules.mk`**:
- `ansi_encoder/rules.mk`: Sets `KEYCHRON_RGB_ENABLE = yes`
- Triggers `rgb/rgb.mk` via `keychron_common.mk`

**Keymap `rules.mk`**:
- `keychron/rules.mk`: Sets `VIA_ENABLE = yes`

---

### 7. QMKata Integration

**Purpose**: Firmata protocol over raw HID for development/debugging

**Files**:
- `qmkata/QMKata.{h,cpp}` - Firmata parser (Arduino-derived)
- `qmkata_sysex_handler.c` - Sysex command handlers
- `qmkata_rgb_matrix_user.c` - Dynamic RGB animation support

**Supported Commands**:
| ID | Command | Description |
|----|---------|-------------|
| `QMKATA_ID_RGB_MATRIX_BUF` | SET | Set per-LED colors from host |
| `QMKATA_ID_DEFAULT_LAYER` | GET/SET | Query/set active layer |
| `QMKATA_ID_MACWIN_MODE` | GET/SET | Query/set MAC/Win mode |
| `QMKATA_ID_CONFIG` | GET/SET | Read/write config structs |
| `QMKATA_ID_STATUS` | GET | Battery, DIP switch, matrix state |
| `QMKATA_ID_COMBO` | GET/SET | Dynamic combo management |
| `QMKATA_ID_TAP_DANCE` | GET/SET | Dynamic tap dance management |
| `QMKATA_ID_LEADER` | GET/SET | Dynamic leader key management |
| `QMKATA_ID_MODULE` | GET/SET/DEL | Load/unload runtime modules |
| `QMKATA_ID_CLI` | SET | Memory/EEPROM read/write |

**Dynamic Animation Framework**:
- Load `.qmk` modules with custom animation code
- `dynld_func.h` defines math callback interface (ARM M0 software fallbacks)
- `dynld_rgb_animation()` - Entry point for custom animations

---

### 8. Debug System

**Files**: `debug_user.c/h`

**Debug Flags** (stored in `debug_config_user_t`):
| Flag | Purpose |
|------|---------|
| `qmkata` | QMKata protocol debugging |
| `stats` | Performance statistics (RGB render, QMKata task) |
| `user_anim` | User animation debugging |
| `module` | Module loader debugging |

**Usage**:
```c
DBG_USR(qmkata, "message %d\n", value);
```

**Devel Config** (`devel_config_t`):
| Field | Purpose |
|-------|---------|
| `pub_keypress` | Publish keypress events |
| `process_keypress` | Log process_record calls |

---

## Hardware Configuration

### MCU & Peripherals
- **MCU**: STM32F401 (ARM Cortex-M4, 84MHz)
- **Bootloader**: stm32-dfu
- **SPI**: SPI1 for SNLED27351 (SCK=A5, MISO=A6, MOSI=A7)
- **I2C**: Not used (LED driver uses SPI)
- **ADC**: Battery voltage monitoring

### Matrix
- **Rows**: C12, D2, B3, B4, B5, B6 (6 rows)
- **Cols**: C6, C7, C8, A14, A15, C10-C15, A0-A2 (17 cols)
- **Diode Direction**: ROW2COL
- **Debounce**: 15ms (custom implementation)

### Inputs
- **Rotary Encoder**: Pin A=B15, B=B14
- **DIP Switch**: Pin C9 (MAC/Win toggle)

### Wireless (LKBT51)
- **Reset**: C4
- **Mode Select**: A9 (BT), A10 (2.4G)
- **Interrupt**: B1 (wireless → MCU), A4 (MCU → wireless)
- **USB Sense**: B0
- **Battery Charging**: B13
- **Battery Low LED**: A8
- **BT Host Devices**: 3 (plus 1 2.4G device)

### RGB Driver (SNLED27351)
- **Shutdown**: B7
- **SPI CS Pins**: B8, B9 (2 drivers)
- **Phase**: 12-channel scan
- **Current Tune**: 0x34 (16mA per channel)

---

## Feature Flags

### Enabled Features
| Feature | Description |
|---------|-------------|
| `DEBOUNCE` | 15ms custom debounce |
| `BOOTMAGIC` | Boot magic (magic combination) |
| `EXTRAKEY` | System control (volume, power) |
| `MOUSEKEY` | Mouse emulation |
| `DIP_SWITCH` | Hardware DIP switch support |
| `ENCODER` / `ENCODER_MAP` | Rotary encoder + per-layer mapping |
| `NKRO` | N-key roll-over |
| `RGB_MATRIX` | Per-key RGB (25 animations) |
| `RAW` | Raw HID support |
| `SEND_STRING` | String sending |
| `DYNAMIC_TAP_DANCE` | EEPROM-persisted tap dance |
| `DYNAMIC_COMBO` | EEPROM-persisted combos |
| `DYNAMIC_LEADER` | EEPROM-persisted leader keys |
| `MODULE_LOADER` | Runtime .qmk module loading |
| `QMKATA` | Firmata over raw HID |
| `LK_WIRELESS` | LKBT51 wireless stack |
| `EECONFIG` | Wear-leveling EEPROM (3KB logical) |

---

## Build Commands

```bash
# ANSI with VIA support
make keychron/q3_max/ansi_encoder:keychron

# ISO with VIA support
make keychron/q3_max/iso_encoder:keychron

# ANSI default (no VIA)
make keychron/q3_max/ansi_encoder:default

# ISO default (no VIA)
make keychron/q3_max/iso_encoder:default

# Flash
make keychron/q3_max/ansi_encoder:keychron:flash
make keychron/q3_max/iso_encoder:keychron:flash
```

---

## Key Architectural Decisions

### 1. Layer Selection Strategy
- **DIP switch** (hardware) selects MAC vs Win base layer
- **FN key** (software) toggles between base and FN layer
- **4 layers total** per OS (base + FN)
- DIP switch state stored in `s_keyb_switch_macwin_mode`

### 2. Dynamic Feature Persistence
- All dynamic features use EEPROM via wear-leveling driver
- Default values loaded on first boot (empty EEPROM check)
- `combo_eeprom_init()`, `tap_dance_eeprom_init()`, `leader_eeprom_init()`

### 3. Safe Mode
- DELETE key held at boot → skip module activation
- Allows recovery from buggy `.qmk` modules
- Implemented in `keyboard_post_init_user()`

### 4. Host RGB Buffer
- Host can set per-LED colors via `QMKATA_ID_RGB_MATRIX_BUF`
- Buffered in `g_rgb_matrix_host_buf` with duration counter
- Rendered in `rgb_matrix_host_buf_render()` during `keychron_task_kb()`

### 5. Modifier Key Fix for RDP
- Override `register_code16()` / `unregister_code16()`
- Split modifier bits into separate HID reports
- Ensures RDP clients see sequential modifier presses

---

## File Reference

### Essential Files

| File | Purpose |
|------|---------|
| `q3_max.c` | Keyboard-level handlers, DIP switch, modifier fix |
| `q3_max_user.c` | User overrides, module safe mode, VIA macros |
| `ansi_encoder/ansi_encoder.c` | ANSI RGB LED configuration |
| `iso_encoder/iso_encoder.c` | ISO RGB LED configuration |
| `ansi_encoder/keymaps/keychron/keymap.c` | User keymap with dynamic features |
| `qmkata_sysex_handler.c` | QMKata sysex command handlers |
| `qmkata_rgb_matrix_user.c` | Dynamic animation framework |
| `debug_user.c/h` | Debug utilities |

### Configuration Files

| File | Purpose |
|------|---------|
| `config.h` | Base C configuration (SPI, RGB, wireless) |
| `info.json` | QMK JSON definition (matrix, features) |
| `board.h` | GPIO pin configuration overrides |
| `mcuconf.h` | ChibiOS MCU configuration |
| `halconf.h` | ChibiOS HAL configuration |

### Build Files

| File | Purpose |
|------|---------|
| `rules.mk` | Base build rules |
| `ansi_encoder/rules.mk` | Variant-specific RGB enable |
| `keychron/rules.mk` | VIA enable |
| `ansi_encoder/keyboard.json` | ANSI layout definition |
| `iso_encoder/keyboard.json` | ISO layout definition |

### External Imports

| Path | Purpose |
|------|---------|
| `keyboards/keychron/common/keychron_common.*` | Shared Keychron logic |
| `keyboards/keychron/common/wireless/` | LKBT51 wireless stack |
| `keyboards/keychron/qmkata/QMKata.*` | Firmata protocol |
| `quantum/` | QMK core |

---

## Development Workflow

### Adding a New Dynamic Feature
1. Add EEPROM storage in `q3_max_user.c` → `keyboard_post_init_user()`
2. Implement EEPROM read/write in feature-specific `.eep` files
3. Add QMKata handlers in `qmkata_sysex_handler.c`

### Adding a New Module Hook
1. Define hook ID in `QMKata.h`
2. Implement handler in module loader
3. Update `dynld_func.h` with function pointer typedef

### Debugging
```bash
# Enable debug flags
make keychron/q3_max/ansi_encoder:keychron DEBUG=1

# In code
DBG_USR(qmkata, "debug message\n");
```

---

## Notes

- **EEPROM Size**: 3KB logical / 6KB physical (wear-leveling)
- **Module Flash**: 2 sectors (S2: 0x08008000, S3: 0x0800C000), 4 slots each
- **RGB Animations**: 25 pre-compiled effects in `rgb_matrix.c`
- **Battery Monitoring**: LKBT51 reports voltage/percentage via wireless stack
