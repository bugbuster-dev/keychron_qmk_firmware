# Keychron Q3 Max — Build Configuration Reference

> Generated: 2026-04-26

---

## Directory Structure

```
keyboards/keychron/q3_max/
├── info.json                    # Base keyboard definition (shared)
├── config.h                     # Base C config (shared)
├── rules.mk                     # Base build rules (shared)
├── q3_max.c                     # Keyboard-level C source
├── q3_max_user.c                # User-level overrides
├── board.h                      # GPIO pin configuration overrides
├── mcuconf.h / halconf.h        # MCU/HAL configuration
├── debug_user.c / debug_user.h  # Debug utilities
├── dynld_func.h                 # Dynamic module loader
├── rgb_matrix_user.inc          # RGB matrix user code
├── qmkata_sysex_handler.c       # QMKata sysex handler
├── qmkata_rgb_matrix_user.c     # QMKata RGB integration
├── flash_layout.md              # Flash memory layout docs
├── readme.md                    # Build instructions
├── via_json/
│   ├── q3_max_ansi_encoder.json # VIA layout: ANSI (PID 0x0830)
│   └── q3_max_iso_encoder.json  # VIA layout: ISO  (PID 0x0831)
├── ansi_encoder/                # ANSI variant (87 keys)
│   ├── keyboard.json            # PID 0x0830, LAYOUT_tkl_ansi
│   ├── config.h                 # 87 LEDs, CAPS_LOCK at index 50
│   ├── rules.mk                 # KEYCHRON_RGB_ENABLE = yes
│   ├── ansi_encoder.c           # Variant-specific source
│   └── keymaps/
│       ├── default/keymap.c     # Default keymap (no VIA)
│       └── keychron/
│           ├── keymap.c         # Keychron keymap + tap dance
│           └── rules.mk         # VIA_ENABLE = yes
└── iso_encoder/                 # ISO variant (88 keys)
    ├── keyboard.json            # PID 0x0831, LAYOUT_iso_88
    ├── config.h                 # 88 LEDs, CAPS_LOCK at index 51
    ├── rules.mk                 # KEYCHRON_RGB_ENABLE = yes
    ├── iso_encoder.c            # Variant-specific source
    └── keymaps/
        ├── default/keymap.c     # Default keymap (no VIA)
        └── keychron/
            ├── keymap.c         # Keychron keymap
            └── rules.mk         # VIA_ENABLE = yes
```

---

## JSON Files Used During Compilation

| # | JSON File | Purpose | Triggered By |
|---|-----------|---------|-------------|
| 1 | `q3_max/info.json` | Base keyboard: MCU (STM32F401), matrix pins, encoder, RGB, EEPROM, features | **Every build** |
| 2 | `ansi_encoder/keyboard.json` | PID `0x0830`, ANSI layout (`LAYOUT_tkl_ansi`), 6×17 matrix | `keychron/q3_max/ansi_encoder:*` |
| 3 | `iso_encoder/keyboard.json` | PID `0x0831`, ISO layout (`LAYOUT_iso_88`), 6×17 matrix | `keychron/q3_max/iso_encoder:*` |
| 4 | `via_json/q3_max_ansi_encoder.json` | VIA editor layout for ANSI (PID 0x0830), custom keycodes, lighting menus | `...:keychron` (VIA enabled) |
| 5 | `via_json/q3_max_iso_encoder.json` | VIA editor layout for ISO (PID 0x0831), custom keycodes, lighting menus | `...:keychron` (VIA enabled) |

### How VIA JSON is Resolved

When `VIA_ENABLE = yes` is set in the keymap's `rules.mk`, QMK's build system automatically finds the correct VIA JSON by matching the **`productId`** field in the VIA JSON against the **`usb.pid`** from `keyboard.json`:

- **ANSI**: PID `0x0830` → `via_json/q3_max_ansi_encoder.json`
- **ISO**: PID `0x0831` → `via_json/q3_max_iso_encoder.json`

The VIA JSON files define:

- The visual keymap editor layout (key positions, sizes, colors)
- Custom keycodes (BTH1, BTH2, BTH3, 2.4G, BAT_LVL, etc.)
- Lighting menus (brightness slider, effect dropdown, speed slider, color picker)
- Matrix dimensions (6 rows × 17 columns)

---

## Build Commands

```bash
# ANSI variant with VIA support:
make keychron/q3_max/ansi_encoder:keychron

# ISO variant with VIA support:
make keychron/q3_max/iso_encoder:keychron

# ANSI with default keymap (no VIA):
make keychron/q3_max/ansi_encoder:default

# ISO with default keymap (no VIA):
make keychron/q3_max/iso_encoder:default

# Flash directly:
make keychron/q3_max/ansi_encoder:keychron:flash
make keychron/q3_max/iso_encoder:keychron:flash
```

---

## Build Include Chain (rules.mk Resolution Order)

```
q3_max/rules.mk
  ├── keyboards/keychron/common/wireless/wireless.mk
  │   └── Wireless stack: LKBT51, battery, RTC, LPM, transport, indicators
  │
  ├── keyboards/keychron/common/keychron_common.mk
  │   ├── debounce/debounce.mk              # Custom debounce
  │   ├── language/language.mk              # OS language toggle
  │   ├── snap_click/snap_click.mk          # Snap click feature
  │   ├── rgb/rgb.mk                        # Custom RGB (per-key, mixed, retail demo)
  │   ├── combo/combo_eeprom.mk             # Dynamic combo EEPROM persistence
  │   ├── tap_dance/tap_dance_eeprom.mk     # Dynamic tap dance EEPROM persistence
  │   ├── leader/leader_eeprom.mk           # Dynamic leader EEPROM persistence
  │   └── module/module_loader.mk           # Runtime module loading
  │
  └── keyboards/keychron/qmkata/qmkata.mk
      └── QMKata: Firmata parser, console, sysex handler

ansi_encoder/rules.mk / iso_encoder/rules.mk
  └── KEYCHRON_RGB_ENABLE = yes
      └── Triggers rgb/rgb.mk inclusion (from keychron_common.mk)

keymaps/keychron/rules.mk
  └── VIA_ENABLE = yes
      └── Enables VIA support; triggers VIA JSON compilation
```

---

## Key Architectural Details

| Property | Value |
|----------|-------|
| **MCU** | STM32F401 |
| **Bootloader** | stm32-dfu |
| **USB VID** | `0x3434` |
| **ANSI PID** | `0x0830` |
| **ISO PID** | `0x0831` |
| **Device Version** | 1.1.1 (ANSI) / 1.1.0 (ISO) |
| **Matrix Size** | 6 rows × 17 cols (102 possible positions) |
| **Diode Direction** | ROW2COL |
| **ANSI LED Count** | 87 (SNLED27351, 12 channels) |
| **ISO LED Count** | 88 (SNLED27351, 12 channels) |
| **RGB Driver** | SNLED27351 over SPI1 (SCK=A5, MISO=A6, MOSI=A7) |
| **Encoder** | 1 rotary encoder (A=B15, B=B14) |
| **DIP Switch** | Pin C9 (toggles MAC/Win base layers) |
| **Wireless Chip** | LKBT51 (3 BT hosts + 2.4G) |
| **EEPROM** | Embedded flash wear-leveling (3KB logical / 6KB physical) |
| **Debounce** | Custom (10ms) |

### Enabled Features

- Bootmagic, Extrakey, Mousekey, NKRO
- RGB Matrix (25 animation effects)
- Rotary Encoder + Encoder Map
- Tap Dance (dynamic, EEPROM-persisted)
- Combos (dynamic, EEPROM-persisted)
- Leader Keys (dynamic, EEPROM-persisted)
- Module Loader (runtime .qmk module loading)
- QMKata (Firmata over raw HID)
- Wireless (Bluetooth 3-profile + 2.4G)
- Factory Test Mode
- Adaptive NKRO
- VIA Insecure (allows unsigned firmware)

### Layout Variants

| Variant | Layout Macro | Keys | Extra Key vs ANSI |
|---------|-------------|------|-------------------|
| ANSI Encoder | `LAYOUT_tkl_ansi` | 87 | — |
| ISO Encoder | `LAYOUT_iso_88` | 88 | ISO Enter (matrix [2,13], spans rows 2-3) + ISO Backslash (matrix [4,1]) |

### Keymap Layers (4 layers per keymap)

| Layer Index | Name | Purpose |
|-------------|------|---------|
| 0 | `MAC_BASE` | macOS default layout |
| 1 | `MAC_FN` | macOS FN layer (BT switching, RGB controls) |
| 2 | `WIN_BASE` | Windows default layout |
| 3 | `WIN_FN` | Windows FN layer (BT switching, RGB controls, numpad on ANSI) |

The DIP switch selects between layers 0/1 (MAC) and 2/3 (Win). The FN key (MO) toggles between the base and FN layer within the selected OS pair.
