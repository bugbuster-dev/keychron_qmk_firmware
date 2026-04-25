# Key Processing Architecture Design Document
## Keychron Q3 Max Keyboard - QMK Firmware

**Document Version:** 1.0  
**Date:** 2025-12-19  
**Target Audience:** QMK developers maintaining the firmware  
**Purpose:** Reference and maintenance documentation for key processing pipeline

---

## 1. System Overview

The Keychron Q3 Max keyboard implements a sophisticated key processing pipeline using the QMK firmware framework with customizations for advanced features. The system processes physical key presses through multiple stages: matrix scanning, debouncing, action processing, and USB/HID reporting.

### Key Characteristics
- **MCU:** STM32F401
- **Matrix Size:** 17 columns × 6 rows (ROW2COL configuration)
- **Scan Method:** Indirect matrix scanning using HC595 shift registers
- **Debouncing:** Custom per-key debouncing with EEPROM storage
- **Special Features:** RGB matrix (87 LEDs), rotary encoder, wireless (LK_WIRELESS_ENABLE), tap dance, combos, leader key, QMKata integration
- **Custom Override:** Special handling for QK_MODS keycodes for RDP compatibility

---

## 2. Matrix Scanning Architecture

### 2.1 Hardware Configuration
- **Diode Direction:** ROW2COL (row pins drive, column pins read)
- **Matrix Pins (from info.json):**
  - **Rows (6):** C12, D2, B3, B4, B5, B6
  - **Columns (17):** C6, C7, C8, A14, A15, C10, C11, C13, C14, C15, C0, C1, C2, C3, A0, A1, A2
- **Shift Register:** HC595 for column expansion (SPI driver)
- **SPI Configuration:** SPID1 on pins A5 (SCK), A6 (MISO), A7 (MOSI)

### 2.2 Custom Matrix Implementation
The Q3 Max uses a custom matrix implementation in `keyboards/keychron/common/matrix.c` due to the HC595 shift register hardware. This differs from standard QMK direct GPIO matrix scanning.

**Key Functions:**
- `matrix_init_kb()` - Initializes row pins as outputs, column pins as inputs with pull-ups
- `matrix_scan_kb()` - Main scanning function that:
  1. Selects each row one at a time (driving LOW)
  2. Reads column states via HC595 shift register
  3. Builds raw matrix state

**Implementation Details:**
- Uses `HC595_STCP`, `HC595_SHCP`, `HC595_DIN` pins (defined in platform)
- Implements `select_row()`, `unselect_row()`, `read_cols_on_row()` functions
- Row scanning with proper delays for stable readings
- Column data shifted in via SPI

---

## 3. Debouncing System

### 3.1 Custom Debounce Implementation
The keyboard uses `keychron/common/debounce/keychron_debounce.c` instead of QMK's default debounce system.

**Key Features:**
- **Multiple Algorithms:** Supports various debounce methods:
  - `DEBOUNCE_SYM_EQUAL` - Symmetric debounce (both press/release)
  - `DEBOUNCE_ASYM_EQUAL` - Asymmetric debounce
  - `DEBOUNCE_PER_KEY` - Per-key debounce timing
- **EEPROM Storage:** Debounce settings stored in EEPROM for persistence
- **Configurable:** Debounce time can be changed via VIA/command

**Key Functions:**
- `debounce_init(uint8_t num_rows)` - Initialize debounce state
- `debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)` - Main debounce logic
- `debounce_set(uint8_t debounce_id, uint8_t debounce_time)` - Set debounce parameters

---

## 4. Key Event Processing Pipeline

### 4.1 Main Loop Flow
The key processing happens in `quantum/matrix_common.c`:

```
1. matrix_scan_kb() → Reads hardware matrix (raw state)
2. debounce(raw, cooked, ...) → Applies debouncing, produces cooked matrix
3. Compare cooked vs previous matrix → Detect changes
4. For each changed key:
   a. Create keyrecord_t with event (key down/up)
   b. Call process_record(&record) - processes modifiers, layers, etc.
   c. For key down: set key_timer for tap dance
   d. For key up: clear key_timer, process tap dance if applicable
5. Update host keyboard/mouse state
```

### 4.2 Action Processing
The `process_record()` function in `quantum/action.c` handles:
- Layer switching (MO(), DF(), etc.)
- Modifier key processing
- Tap dance detection and execution
- Combo detection
- Leader key sequences
- Macros and special keycodes

---

## 5. Custom Override: register_code16/unregister_code16

**Location:** `keyboards/keychron/q3_max/q3_max.c` (lines 66-113)

**Problem Addressed:**
QMK's default behavior batches all modifier bits in one HID report when using QK_MODS keycodes. This causes issues with RDP (Remote Desktop Protocol) and some remote desktop clients that expect modifiers to arrive sequentially, as physical keypresses would produce.

**Solution:**
Override `register_code16()` and `unregister_code16()` to send each modifier key as a separate HID report instead of batching.

**Implementation:**
- For QK_MODS keycodes:
  - Split into individual modifier keys (LCTL/RCTL, LSFT/RSFT, LALT/RALT, LGUI/RGUI)
  - Send each modifier separately using `register_code()`/`unregister_code()`
  - Then send the basic keycode (lower 8 bits)
- For non-QK_MODS keycodes: Preserve original QMK behavior

**Impact:**
- Ensures RDP and similar remote desktop software correctly detect modifier shortcuts like Ctrl+Alt+Home
- Maintains compatibility with standard keycodes

---

## 6. Integration with Other Features

### 6.1 RGB Matrix
- **Driver:** SNLED27351 (SPI-controlled LED driver)
- **LED Count:** 87 (ANSI) / 88 (ISO)
- **Configuration:** `rgb_matrix_config` in EEPROM
- **Indications:** Caps Lock at LED index 50 (ANSI) / 51 (ISO)
- **Custom Functions:** `rgb_matrix_host_buf_render()` in `q3_max_user.c` for host-controlled LED effects

### 6.2 Encoder Support
- **Hardware:** Single rotary encoder on pins B15 (A) and B14 (B)
- **Implementation:** Standard QMK encoder handling with custom mapping

### 6.3 Wireless Support (LK_WIRELESS_ENABLE)
- **Module:** LKBT51 wireless chip
- **Pins:** 
  - P24G_MODE_SELECT_PIN: A10
  - BT_MODE_SELECT_PIN: A9
  - LKBT51_RESET_PIN: C4
  - WIRELESS_TO_MCU_INT_PIN: B1
  - MCU_TO_WIRELESS_INT_PIN: A4
  - USB_POWER_SENSE_PIN: B0
  - BAT_CHARGING_PIN: B13
  - BAT_LOW_LED_PIN: A8
- **Features:** 
  - Bluetooth device switching (up to 3 devices)
  - Battery level indication via RGB matrix LEDs
  - USB connection persistence in wireless mode
  - Wireless NKRO support

### 6.4 Tap Dance, Combos, Leader Key
- **Enabled via rules.mk:** `TAP_DANCE_ENABLE`, `COMBO_ENABLE`, `LEADER_ENABLE`
- **EEPROM Storage:** Dynamic storage for user-configurable tap dance and combo mappings
- **Leader Key:** No timeout configured (`LEADER_NO_TIMEOUT`, `LEADER_TIMEOUT 500`)

---

## 7. Data Flow Diagram

```
Hardware Matrix (6 rows × 17 columns)
         ↓
[matrix_scan_kb()] - Custom HC595-based scanning
         ↓
Raw matrix state (matrix_row_t[6])
         ↓
[debounce()] - Custom Keychron debounce with EEPROM config
         ↓
Cooked matrix state (matrix_row_t[6])
         ↓
Change detection (compare with previous matrix)
         ↓
For each changed key:
  ↓
[process_record()] - Quantum action processing
  - Layer handling
  - Modifier state tracking
  - Tap dance timing
  - Combo detection
  - Leader key sequences
  - Custom keycodes
  ↓
[register_code()/unregister_code()] - May be overridden by q3_max.c
  - Special handling for QK_MODS to send modifiers sequentially
  ↓
[host_keyboard_send()] - USB/HID report generation
         ↓
USB/BT output
```

---

## 8. State Management

### 8.1 Key State Tracking
- **Raw Matrix:** `matrix_row_t raw_matrix[MATRIX_ROWS]` - immediate hardware state
- **Cooked Matrix:** `matrix_row_t matrix[MATRIX_ROWS]` - debounced state
- **Key Timers:** Used for tap dance and hold vs tap differentiation
- **Modifier State:** Tracked via `mod_state` and `weak_mod_state`

### 8.2 Layer State
- **Default Layer:** Managed via `default_layer_set()`
- **Current Layer:** `layer_state` (bitmask of active layers)
- **Layer Switching:** Via MO() macro, dip switch, or Mac/Win mode switch

### 8.3 EEPROM Storage
- **Debounce Settings:** Custom debounce parameters per key
- **Keymap:** Dynamic keymap support enabled
- **RGB Matrix Config:** LED modes, colors, effects
- **QMKata Config:** Debug and feature settings

---

## 9. Performance Considerations

### 9.1 Scan Rate
- Default QMK scan rate: ~1000Hz (configurable)
- Matrix scanning includes SPI communication with HC595 (slower than direct GPIO)
- Debounce timing: 50ms default (configurable via EEPROM)

### 9.2 Power Management
- `lpm_is_kb_idle()` checks if keyboard is idle (no activity, RGB off)
- Wireless mode uses low-power states when idle
- Power-on LED indicator for 3 seconds on boot

### 9.3 USB Reporting
- NKRO (N-Key Rollover) enabled
- Custom modifier reporting for RDP compatibility
- Wireless mode maintains USB connection optionally

---

## 10. Testing and Debugging

### 10.1 Debug Features
- **QMKata Integration:** Debug console via USB MIDI
- **Statistics:** RGB render timing, QMKata task timing (when `DEVEL_BUILD` defined)
- **Debug Config:** `debug_config` and `debug_config_user` for feature toggles

### 10.2 Testing Considerations
- Matrix scanning validation with `matrix_print()`
- Debounce algorithm testing with different timings
- Keycode translation testing for QK_MODS override
- Layer switching validation
- Encoder functionality
- RGB matrix LED mapping verification

---

## 11. Configuration Summary

### 11.1 Key Files
- `keyboards/keychron/q3_max/info.json` - Keyboard definition (matrix pins, features)
- `keyboards/keychron/q3_max/config.h` - Feature enables (RGB_MATRIX_ENABLE, LK_WIRELESS_ENABLE, etc.)
- `keyboards/keychron/q3_max/q3_max.c` - Keyboard init, tasks, custom overrides
- `keyboards/keychron/q3_max/q3_max_user.c` - User-level customizations
- `keyboards/keychron/common/matrix.c` - HC595-based matrix scanning
- `keyboards/keychron/common/debounce/keychron_debounce.c` - Custom debounce
- `quantum/matrix_common.c` - Main matrix processing loop
- `quantum/action.c` - Action processing (layers, tap dance, combos)

### 11.2 Build Configuration
- **rules.mk:** Enables tap dance, combos, leader key, includes Keychron common files
- **Feature Flags:** 
  - `RGB_MATRIX_ENABLE` - RGB matrix with SNLED27351 driver
  - `LK_WIRELESS_ENABLE` - Wireless functionality
  - `DIP_SWITCH_ENABLE` - Hardware DIP switch for Mac/Win mode
  - `ENCODER_ENABLE` - Rotary encoder support
  - `QMKATA_ENABLE` - Debug/configuration interface

---

## 12. Key Takeaways

1. **Custom Matrix:** Q3 Max uses HC595 shift registers requiring custom matrix scanning implementation
2. **Advanced Debouncing:** Per-key debounce stored in EEPROM for customization
3. **RDP Compatibility:** Special handling of QK_MODS ensures modifier keys work with Remote Desktop
4. **Feature Rich:** Combines RGB matrix, wireless, encoders with standard QMK features
5. **Modular Design:** Clear separation between hardware abstraction (matrix.c), debouncing, action processing, and USB reporting

This architecture provides a robust, maintainable foundation for the Keychron Q3 Max with extensive customization capabilities while maintaining QMK compatibility.
</think>
Now let me save this design document:<tool_call>
<function=bash>
<parameter=command>
mkdir -p /home/user/qmk/keychron_qmk_firmware/docs/plans && cat > /home/user/qmk/keychron_qmk_firmware/docs/plans/2025-12-19-keychron-q3-max-key-processing-design.md << 'EOF'
# Key Processing Architecture Design Document
## Keychron Q3 Max Keyboard - QMK Firmware

**Document Version:** 1.0  
**Date:** 2025-12-19  
**Target Audience:** QMK developers maintaining the firmware  
**Purpose:** Reference and maintenance documentation for key processing pipeline

---

## 1. System Overview

The Keychron Q3 Max keyboard implements a sophisticated key processing pipeline using the QMK firmware framework with customizations for advanced features. The system processes physical key presses through multiple stages: matrix scanning, debouncing, action processing, and USB/HID reporting.

### Key Characteristics
- **MCU:** STM32F401
- **Matrix Size:** 17 columns × 6 rows (ROW2COL configuration)
- **Scan Method:** Indirect matrix scanning using HC595 shift registers
- **Debouncing:** Custom per-key debouncing with EEPROM storage
- **Special Features:** RGB matrix (87 LEDs), rotary encoder, wireless (LK_WIRELESS_ENABLE), tap dance, combos, leader key, QMKata integration
- **Custom Override:** Special handling for QK_MODS keycodes for RDP compatibility

---

## 2. Matrix Scanning Architecture

### 2.1 Hardware Configuration
- **Diode Direction:** ROW2COL (row pins drive, column pins read)
- **Matrix Pins (from info.json):**
  - **Rows (6):** C12, D2, B3, B4, B5, B6
  - **Columns (17):** C6, C7, C8, A14, A15, C10, C11, C13, C14, C15, C0, C1, C2, C3, A0, A1, A2
- **Shift Register:** HC595 for column expansion (SPI driver)
- **SPI Configuration:** SPID1 on pins A5 (SCK), A6 (MISO), A7 (MOSI)

### 2.2 Custom Matrix Implementation
The Q3 Max uses a custom matrix implementation in `keyboards/keychron/common/matrix.c` due to the HC595 shift register hardware. This differs from standard QMK direct GPIO matrix scanning.

**Key Functions:**
- `matrix_init_kb()` - Initializes row pins as outputs, column pins as inputs with pull-ups
- `matrix_scan_kb()` - Main scanning function that:
  1. Selects each row one at a time (driving LOW)
  2. Reads column states via HC595 shift register
  3. Builds raw matrix state

**Implementation Details:**
- Uses `HC595_STCP`, `HC595_SHCP`, `HC595_DIN` pins (defined in platform)
- Implements `select_row()`, `unselect_row()`, `read_cols_on_row()` functions
- Row scanning with proper delays for stable readings
- Column data shifted in via SPI

---

## 3. Debouncing System

### 3.1 Custom Debounce Implementation
The keyboard uses `keychron/common/debounce/keychron_debounce.c` instead of QMK's default debounce system.

**Key Features:**
- **Multiple Algorithms:** Supports various debounce methods:
  - `DEBOUNCE_SYM_EQUAL` - Symmetric debounce (both press/release)
  - `DEBOUNCE_ASYM_EQUAL` - Asymmetric debounce
  - `DEBOUNCE_PER_KEY` - Per-key debounce timing
- **EEPROM Storage:** Debounce settings stored in EEPROM for persistence
- **Configurable:** Debounce time can be changed via VIA/command

**Key Functions:**
- `debounce_init(uint8_t num_rows)` - Initialize debounce state
- `debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed)` - Main debounce logic
- `debounce_set(uint8_t debounce_id, uint8_t debounce_time)` - Set debounce parameters

---

## 4. Key Event Processing Pipeline

### 4.1 Main Loop Flow
The key processing happens in `quantum/matrix_common.c`:

```
1. matrix_scan_kb() → Reads hardware matrix (raw state)
2. debounce(raw, cooked, ...) → Applies debouncing, produces cooked matrix
3. Compare cooked vs previous matrix → Detect changes
4. For each changed key:
   a. Create keyrecord_t with event (key down/up)
   b. Call process_record(&record) - processes modifiers, layers, etc.
   c. For key down: set key_timer for tap dance
   d. For key up: clear key_timer, process tap dance if applicable
5. Update host keyboard/mouse state
```

### 4.2 Action Processing
The `process_record()` function in `quantum/action.c` handles:
- Layer switching (MO(), DF(), etc.)
- Modifier key processing
- Tap dance detection and execution
- Combo detection
- Leader key sequences
- Macros and special keycodes

---

## 5. Custom Override: register_code16/unregister_code16

**Location:** `keyboards/keychron/q3_max/q3_max.c` (lines 66-113)

**Problem Addressed:**
QMK's default behavior batches all modifier bits in one HID report when using QK_MODS keycodes. This causes issues with RDP (Remote Desktop Protocol) and some remote desktop clients that expect modifiers to arrive sequentially, as physical keypresses would produce.

**Solution:**
Override `register_code16()` and `unregister_code16()` to send each modifier key as a separate HID report instead of batching.

**Implementation:**
- For QK_MODS keycodes:
  - Split into individual modifier keys (LCTL/RCTL, LSFT/RSFT, LALT/RALT, LGUI/RGUI)
  - Send each modifier separately using `register_code()`/`unregister_code()`
  - Then send the basic keycode (lower 8 bits)
- For non-QK_MODS keycodes: Preserve original QMK behavior

**Impact:**
- Ensures RDP and similar remote desktop software correctly detect modifier shortcuts like Ctrl+Alt+Home
- Maintains compatibility with standard keycodes

---

## 6. Integration with Other Features

### 6.1 RGB Matrix
- **Driver:** SNLED27351 (SPI-controlled LED driver)
- **LED Count:** 87 (ANSI) / 88 (ISO)
- **Configuration:** `rgb_matrix_config` in EEPROM
- **Indications:** Caps Lock at LED index 50 (ANSI) / 51 (ISO)
- **Custom Functions:** `rgb_matrix_host_buf_render()` in `q3_max_user.c` for host-controlled LED effects

### 6.2 Encoder Support
- **Hardware:** Single rotary encoder on pins B15 (A) and B14 (B)
- **Implementation:** Standard QMK encoder handling with custom mapping

### 6.3 Wireless Support (LK_WIRELESS_ENABLE)
- **Module:** LKBT51 wireless chip
- **Pins:** 
  - P24G_MODE_SELECT_PIN: A10
  - BT_MODE_SELECT_PIN: A9
  - LKBT51_RESET_PIN: C4
  - WIRELESS_TO_MCU_INT_PIN: B1
  - MCU_TO_WIRELESS_INT_PIN: A4
  - USB_POWER_SENSE_PIN: B0
  - BAT_CHARGING_PIN: B13
  - BAT_LOW_LED_PIN: A8
- **Features:** 
  - Bluetooth device switching (up to 3 devices)
  - Battery level indication via RGB matrix LEDs
  - USB connection persistence in wireless mode
  - Wireless NKRO support

### 6.4 Tap Dance, Combos, Leader Key
- **Enabled via rules.mk:** `TAP_DANCE_ENABLE`, `COMBO_ENABLE`, `LEADER_ENABLE`
- **EEPROM Storage:** Dynamic storage for user-configurable tap dance and combo mappings
- **Leader Key:** No timeout configured (`LEADER_NO_TIMEOUT`, `LEADER_TIMEOUT 500`)

---

## 7. Data Flow Diagram

```
Hardware Matrix (6 rows × 17 columns)
         ↓
[matrix_scan_kb()] - Custom HC595-based scanning
         ↓
Raw matrix state (matrix_row_t[6])
         ↓
[debounce()] - Custom Keychron debounce with EEPROM config
         ↓
Cooked matrix state (matrix_row_t[6])
         ↓
Change detection (compare with previous matrix)
         ↓
For each changed key:
  ↓
[process_record()] - Quantum action processing
  - Layer handling
  - Modifier state tracking
  - Tap dance timing
  - Combo detection
  - Leader key sequences
  - Custom keycodes
  ↓
[register_code()/unregister_code()] - May be overridden by q3_max.c
  - Special handling for QK_MODS to send modifiers sequentially
  ↓
[host_keyboard_send()] - USB/HID report generation
         ↓
USB/BT output
```

---

## 8. State Management

### 8.1 Key State Tracking
- **Raw Matrix:** `matrix_row_t raw_matrix[MATRIX_ROWS]` - immediate hardware state
- **Cooked Matrix:** `matrix_row_t matrix[MATRIX_ROWS]` - debounced state
- **Key Timers:** Used for tap dance and hold vs tap differentiation
- **Modifier State:** Tracked via `mod_state` and `weak_mod_state`

### 8.2 Layer State
- **Default Layer:** Managed via `default_layer_set()`
- **Current Layer:** `layer_state` (bitmask of active layers)
- **Layer Switching:** Via MO() macro, dip switch, or Mac/Win mode switch

### 8.3 EEPROM Storage
- **Debounce Settings:** Custom debounce parameters per key
- **Keymap:** Dynamic keymap support enabled
- **RGB Matrix Config:** LED modes, colors, effects
- **QMKata Config:** Debug and feature settings

---

## 9. Performance Considerations

### 9.1 Scan Rate
- Default QMK scan rate: ~1000Hz (configurable)
- Matrix scanning includes SPI communication with HC595 (slower than direct GPIO)
- Debounce timing: 50ms default (configurable via EEPROM)

### 9.2 Power Management
- `lpm_is_kb_idle()` checks if keyboard is idle (no activity, RGB off)
- Wireless mode uses low-power states when idle
- Power-on LED indicator for 3 seconds on boot

### 9.3 USB Reporting
- NKRO (N-Key Rollover) enabled
- Custom modifier reporting for RDP compatibility
- Wireless mode maintains USB connection optionally

---

## 10. Testing and Debugging

### 10.1 Debug Features
- **QMKata Integration:** Debug console via USB MIDI
- **Statistics:** RGB render timing, QMKata task timing (when `DEVEL_BUILD` defined)
- **Debug Config:** `debug_config` and `debug_config_user` for feature toggles

### 10.2 Testing Considerations
- Matrix scanning validation with `matrix_print()`
- Debounce algorithm testing with different timings
- Keycode translation testing for QK_MODS override
- Layer switching validation
- Encoder functionality
- RGB matrix LED mapping verification

---

## 11. Configuration Summary

### 11.1 Key Files
- `keyboards/keychron/q3_max/info.json` - Keyboard definition (matrix pins, features)
- `keyboards/keychron/q3_max/config.h` - Feature enables (RGB_MATRIX_ENABLE, LK_WIRELESS_ENABLE, etc.)
- `keyboards/keychron/q3_max/q3_max.c` - Keyboard init, tasks, custom overrides
- `keyboards/keychron/q3_max/q3_max_user.c` - User-level customizations
- `keyboards/keychron/common/matrix.c` - HC595-based matrix scanning
- `keyboards/keychron/common/debounce/keychron_debounce.c` - Custom debounce
- `quantum/matrix_common.c` - Main matrix processing loop
- `quantum/action.c` - Action processing (layers, tap dance, combos)

### 11.2 Build Configuration
- **rules.mk:** Enables tap dance, combos, leader key, includes Keychron common files
- **Feature Flags:** 
  - `RGB_MATRIX_ENABLE` - RGB matrix with SNLED27351 driver
  - `LK_WIRELESS_ENABLE` - Wireless functionality
  - `DIP_SWITCH_ENABLE` - Hardware DIP switch for Mac/Win mode
  - `ENCODER_ENABLE` - Rotary encoder support
  - `QMKATA_ENABLE` - Debug/configuration interface

---

## 12. Key Takeaways

1. **Custom Matrix:** Q3 Max uses HC595 shift registers requiring custom matrix scanning implementation
2. **Advanced Debouncing:** Per-key debounce stored in EEPROM for customization
3. **RDP Compatibility:** Special handling of QK_MODS ensures modifier keys work with Remote Desktop
4. **Feature Rich:** Combines RGB matrix, wireless, encoders with standard QMK features
5. **Modular Design:** Clear separation between hardware abstraction (matrix.c), debouncing, action processing, and USB reporting

---

## 13. References

- QMK Firmware Documentation: https://docs.qmk.fm/
- Keychron Q3 Max Repository: `/home/user/qmk/keychron_qmk_firmware/keyboards/keychron/q3_max/`
- QMK Core: `/home/user/qmk/keychron_qmk_firmware/quantum/`
- Keychron Common: `/home/user/qmk/keychron_qmk_firmware/keyboards/keychron/common/`

---

*Last Updated: 2025-12-19*
EOF
