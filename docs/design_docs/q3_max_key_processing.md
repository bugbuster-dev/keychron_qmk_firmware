# Key Processing Design Document: Keychron Q3 Max (QMK Firmware)

> **Firmware**: QMK Fork (Keychron-specific)
> **Keyboard**: Keychron Q3 Max
> **Processor**: STM32F401VGT6 (ARM Cortex-M4 @ 16 MHz)
> **Platform**: ChibiOS
> **Document Version**: 1.0
> **Date**: 2026-04-16

---

## Table of Contents

1. [Hardware Overview](#1-hardware-overview)
2. [System Architecture](#2-system-architecture)
3. [Key Processing Pipeline](#3-key-processing-pipeline)
4. [Analog Matrix Scanning](#4-analog-matrix-scanning)
5. [Hall Effect Sensor Processing](#5-hall-effect-sensor-processing)
6. [Calibration System](#6-calibration-system)
7. [Action Processing Pipeline](#7-action-processing-pipeline)
8. [Layer & Keymap System](#8-layer--keymap-system)
9. [HID Report Generation](#9-hid-report-generation)
10. [Advanced Features](#10-advanced-features)
11. [Custom Board Modifications](#11-custom-board-modifications)
12. [Data Flow Summary](#12-data-flow-summary)
13. [Performance & Resource Analysis](#13-performance--resource-analysis)
14. [Error Handling & Edge Cases](#14-error-handling--edge-cases)

---

## 1. Hardware Overview

### 1.1 Processor & Bootloader

| Parameter | Value |
|-----------|-------|
| MCU | STM32F401VGT6 |
| Core | ARM Cortex-M4 @ 16 MHz (HSE 16 MHz, PLL 8/96/4/4) |
| Flash | 1024 KB |
| SRAM | 192 KB |
| Bootloader | stm32-dfu (USB DFU) |
| Platform | ChibiOS RTOS |

### 1.2 Matrix Layout

| Dimension | Value |
|-----------|-------|
| Rows | 6 (`C12`, `D2`, `B3`, `B4`, `B5`, `B6`) |
| Columns | 17 (`C6`..`A2`) |
| Total Keys | 102 (6 x 17) |
| Diode Direction | ROW2COL |

### 1.3 Peripheral Pin Assignments

| Peripheral | Pins |
|------------|------|
| **Row Pins (ADC inputs)** | C12, D2, B3, B4, B5, B6 |
| **Column Select (HC164)** | DS=B3, CP=B5, MR=D2 |
| **SPI1 (RGB Matrix + EEPROM)** | SCK=A5, MISO=A6, MOSI=A7 |
| **SNLED27351 RGB Driver** | SDB=B7, Select={B8, B9} |
| **Audio Codec (CS43L22)** | LRCK=A4, MCLK=C7, SCLK=C10, SDIN=C12 |
| **Gyroscope (L3GD20)** | I2C on A5/A6 (multiplexed with SPI) |
| **Accelerometer (LSM303DLHC)** | I2C on B6/B7 |
| **Rotary Encoder** | Pin A=B15, Pin B=B14 |
| **DIP Switch** | C9 (Mac/Win toggle) |
| **Wireless Module (LKBT51)** | Reset=C4, MCU-to-Wireless Int=A4 |
| **Wireless-to-MCU Interrupt** | B1 |
| **USB Power Sense** | B0 (active low) |
| **Battery Charging** | B13 |
| **Battery Low LED** | A8 |
| **P24G Mode Select** | A10 |
| **BT Mode Select** | A9 |

### 1.4 Hall Effect Sensor Architecture

The Q3 Max replaces the traditional digital GPIO matrix with **Hall Effect (HE) magnetic switches**. Each key has a magnetic switch whose actuation is detected by reading the analog voltage from the HE sensor. The firmware uses the STM32's 12-bit ADC to measure the magnetic field strength, which correlates to key travel distance (0 = fully released, 40 = fully pressed, scaled by TRAVEL_SCALE=6).

**Key difference from standard QMK**: Standard QMK uses digital GPIO matrix scanning (set row LOW, read columns). Q3 Max uses **analog ADC scanning** (select column via HC164 shift register, read ADC values on all 6 row channels simultaneously).

---

## 2. System Architecture

### 2.1 Layered Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Userspace / Keymap (keymap.c)                              │
├─────────────────────────────────────────────────────────────┤
│  QMK Feature Layer (quantum/)                               │
│  ├─ Action System (action.c, action_tapping.c)              │
│  ├─ Layer System (action_layer.c)                           │
│  ├─ Module Pipeline (quantum.c)                             │
│  ├─ Debounce (custom analog debounce)                       │
│  └─ HID Report (tmk_core/protocol/)                         │
├─────────────────────────────────────────────────────────────┤
│  Keychron Common Layer (keyboards/keychron/common/)         │
│  ├─ Analog Matrix (analog_matrix_*)                         │
│  ├─ Wireless Stack (wireless/)                              │
│  ├─ Custom Debounce (debounce/)                             │
│  ├─ Keychron Raw HID (keychron_raw_hid.c)                   │
│  └─ Profile System (profile.c)                              │
├─────────────────────────────────────────────────────────────┤
│  Q3 Max Board Layer (keyboards/keychron/q3_max/)            │
│  ├─ q3_max.c (board init, dip switch, register_code16)      │
│  ├─ config.h (feature macros)                               │
│  └─ board.h (GPIO pin mode overrides)                       │
├─────────────────────────────────────────────────────────────┤
│  ChibiOS Platform (platforms/chibios/)                      │
│  ├─ ADC Driver (ADCD1)                                      │
│  ├─ SPI Driver (SPI1)                                       │
│  ├─ GPIO Driver (PAL)                                       │
│  └─ Timer Abstraction                                       │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Firmware Initialization Sequence

```
main()
  │
  ├─ platform_setup()                    // MCU clocks, GPIO init, ChibiOS HAL
  ├─ protocol_setup()                    // USB initialization
  │
  ├─ keyboard_setup()
  │   └─ matrix_init_custom()            // analog_matrix_scan.c
  │       ├─ Configure row pins as ANALOG inputs
  │       ├─ Configure ADC1 sequence (6 channels)
  │       ├─ Initialize HC164 shift register pins
  │       ├─ SYSCFG->PMC |= SYSCFG_PMC_ADC1DC2  // Analog switch config
  │       ├─ Drop initial 5 scan cycles (unstable ADC startup)
  │       └─ analog_matrix_init()
  │
  ├─ protocol_pre_init()                 // USB pre-init
  ├─ keyboard_init()
  │   ├─ quantum_init()                  // EEConfig, default layer, bootmagic
  │   ├─ encoder_init()
  │   └─ keyboard_post_init_quantum()
  │
  ├─ protocol_post_init()                // host_set_active_driver()
  │
  └─ keyboard_post_init_kb()             // q3_max.c
      └─ keychron_common_init()          // Keychron common init + power-on LED
```

### 2.3 Main Loop

```c
// quantum/main.c
while (true) {
    protocol_pre_task();
    protocol_keyboard_task();     // keyboard_task() -> matrix_task()
    protocol_post_task();
    raw_hid_task();               // Handles analog_matrix_rx() for Raw HID
    deferred_exec_task();         // Deferred execution queue
    housekeeping_task();          // General housekeeping
}
```

---

## 3. Key Processing Pipeline

### 3.1 Complete Pipeline Overview

```
Physical Key Press/Release
         │
         ▼
┌──────────────────────────┐
│  Stage 1: ADC Scanning   │  matrix_scan_custom()
│  (Hardware: ADC1 + HC164)│  17 columns x 6 rows
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 2: Analog-to-     │  update_raw_value()
│  Travel Conversion       │  ADC value → travel distance (0-240 scaled)
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 3: Mode-Specific  │  Mode dispatch:
│  Actuation Processing    │  - Regular, Rapid Trigger, OKMC,
│                          │    Toggle, Gamepad
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 4: Matrix Merge   │  virtual_matrix |=
│  & Debounce              │    game_controller_matrix | okmc_matrix
│                          │  (analog debounce: 3-sample recheck)
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 5: QMK matrix_task│  Detect changed bits
│  (Standard QMK)          │  Create keyevent_t per changed key
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 6: action_exec()  │  Tap-hold state machine,
│  (Standard QMK)          │  Module pipeline, action lookup
└──────────┬───────────────┘
           │
           ▼
┌──────────────────────────┐
│  Stage 7: Register Code  │  add_key() / add_mods() /
│  & HID Report            │  send_keyboard_report()
└──────────┬───────────────┘
           │
           ▼
   USB HID Report to Host
```

### 3.2 Stage 1: ADC Scanning (matrix_scan_custom)

The analog matrix scan replaces the standard QMK `matrix_scan()`. It operates in the ChibiOS main loop via `matrix_scan_custom()` in `analog_matrix_scan.c`.

**Per-scan procedure**:

```
For each column (0 to 16):
  1. HC164 shift register activates column
  2. 40μs settle delay
  3. ADC conversion on all 6 row channels (simultaneous)
  4. For each row (0 to 5):
     a. update_raw_value(row, col, adc_sample)
     b. analog_matrix_get_key_state(row, col) → pressed?
  5. 3-sample analog debounce recheck
  6. Update analog_raw_matrix and changed_matrix bitmasks
  7. Deactivate column (HC164_output(0x00, true))
```

**HC164 Shift Register**:
- Controls which column's HE sensor is connected to the ADC
- Serial data (B3), clock (B5), master reset (D2)
- 15-bit shift (only 15 columns used of 17 total)
- Bits are shifted MSB-first

**ADC Configuration** (ChibiOS ADCConversionGroup):
- 6 channels (one per row pin)
- Sample time: 56 cycles
- Triggered by software (`ADC_CR2_SWSTART`)
- Free-running mode
- 12-bit resolution (0-4095)

### 3.3 Stage 2: Analog-to-Travel Conversion

Each HE sensor outputs a voltage proportional to the distance between the magnet and the Hall sensor. The firmware converts raw ADC values to physical travel distance.

**Polynomial conversion** (empirically determined for HE sensors):

```c
TRAVEL_POLYNOMIAL(x) = CONST_A1 + CONST_B1*x + CONST_C1*x² + CONST_D1*x³
                     = 426.88962 - 0.48358*x + 2.04637e-4*x² - 2.99368e-8*x³
```

**Travel calculation**:
```c
delta = calib_values[row][col].zero_travel - 30 - adc_value
if delta < 0: return 0

offset = calib_values[row][col].zero_travel - REF_ZERO_TRAVEL
x = adc_value - offset

travel = (TRAVEL_POLYNOMIAL(x) - TRAVEL_POLYNOMIAL(REF_ZERO_TRAVEL))
         * scale_factor[row][col] * TRAVEL_SCALE + 0.5

// Scale factor normalizes each key to expected 4.0mm full travel
// TRAVEL_SCALE = 6 (travel units: 0-240 for 0-4.0mm)
// FULL_TRAVEL_UNIT = 40 (in travel units, 40*6=240)
```

**Key constants**:
| Constant | Value | Meaning |
|----------|-------|---------|
| `REF_ZERO_TRAVEL` | 3121 | Reference ADC at rest |
| `REF_FULL_TRAVEL` | 1940 | Reference ADC at bottom |
| `REF_RANGE` | 1181 | ADC range (rest-to-bottom) |
| `DEFAULT_ZERO_TRAVEL_VALUE` | 3000 | Default ADC at rest |
| `DEFAULT_FULL_RANGE` | 900 | Default ADC range |
| `DEFAULT_FULL_TRAVEL_VALUE` | 2100 | Default ADC at bottom |
| `VALID_ANALOG_RAW_VALUE_MIN` | 1200 | Below: no sensor |
| `VALID_ANALOG_RAW_VALUE_MAX` | 3500 | Above: no sensor |
| `STATIC_HYSTERESIS` | 5 | Minimum ADC change to register |
| `TRAVEL_SCALE` | 6 | Scale ADC to travel units |
| `FULL_TRAVEL_UNIT` | 40 | Full travel in scaled units |
| `BOTTOM_JITTER` | 80 | Jitter compensation at bottom |
| `ZERO_TRAVEL_DEAD_ZONE` | 20 | Dead zone around zero |

### 3.4 Stage 3: Mode-Specific Actuation Processing

Each key has a mode that determines how its travel distance is interpreted:

| Mode | AKM Value | Description |
|------|-----------|-------------|
| Global | 0 | Inherits from profile global settings |
| Regular | 1 | Static actuation point with hysteresis |
| Rapid Trigger | 2 | Dynamic actuation/release points |
| OKMC (DKS) | 3 | One-Key-Many-Commands, multi-depth |
| Gamepad | 4 | XInput/Joystick game controller |
| Toggle | 5 | Latching toggle on actuation |

**Regular Trigger** (`action_regular_trigger.c`):
```
If pressed && travel < deactn_pt:   → release event
If released && travel >= actn_pt:   → press event
deactn_pt = actn_pt - 3 (scaled)
```

**Rapid Trigger** (`action_rapid_trigger.c`):
```
If traveling deeper:
  deactn_pt = current_travel - release_sensitivity
  actn_pt   = current_travel
If releasing:
  deactn_pt = current_travel
  actn_pt   = current_travel + press_sensitivity
```

**OKMC** (`action_okmc.c`):
```
States: RELEASED → SHALLOW_ACTUATED → DEEP_ACTUATED →
        DEEP_DEACT_READY → DEEP_DEACTUATED → SHALLOW_ACTUATED → RELEASED
4 keycodes with 2 actuation points each (shallow/deep)
```

**Toggle** (`action_toggle.c`):
```
First press: latch ON (hold=true)
Second press: latch OFF (hold=false)
```

### 3.5 Stage 4: Matrix Merge & Debounce

The analog matrix implements its own debouncing (3-sample recheck) integrated into `matrix_read_rows_on_col()`:

```c
uint8_t debounce_times = ANALOG_DEBOUCE_TIME;  // = 3
do {
    adcConvert(...);
    for each row:
        update_raw_value(row, col, sample);
        bool pressed = analog_matrix_get_key_state(row, col);
        if pressed:
            if first sample: row_value |= bit
            else: row_value_recheck |= bit
} while (--debounce_times && changed);

// If first sample != recheck samples, clear (bounce detected)
if (first_sample != recheck && changed):
    changed = false;
```

**Matrix merging** (in `matrix_scan_custom()`):
```c
// Merge analog results with game controller and OKMC virtual matrices
virtual_matrix[row] |= game_controller_matrix[row] | okmc_matrix[row];

// Apply analog_matrix_mask to filter non-analog keys
raw_matrix[row] &= analog_matrix_mask[row];
```

### 3.6 Stage 5: QMK Standard Key Event Processing

After the analog matrix scan, QMK's standard `matrix_task()` in `keyboard.c` takes over:

```c
static bool matrix_task(void) {
    matrix_scan();  // Calls matrix_scan_custom() for Q3 Max

    // Detect changed rows
    for each row:
        if (previous[row] != matrix_get_row(row)):
            // For each changed bit in the row:
            for each column:
                keyevent_t event = MAKE_KEYEVENT(row, col, pressed);
                action_exec(event);

    // Store current state as previous
    previous[row] = matrix_get_row(row);
}
```

---

## 4. Analog Matrix Scanning

### 4.1 ADC Pin-to-Channel Mapping

| Pin | ADC Channel | Row Pin |
|-----|------------|---------|
| A0 | IN0 | ✓ (B4) |
| A1 | IN1 | ✓ (B5) |
| A2 | IN2 | ✓ (B6) |
| A3 | IN3 | |
| A4 | IN4 | |
| A5 | IN5 | |
| A6 | IN6 | |
| A7 | IN7 | |
| B0 | IN8 | |
| B1 | IN9 | |
| C0 | IN10 | ✓ (C12) |
| C1 | IN11 | |
| C2 | IN12 | |
| C3 | IN13 | |
| C4 | IN14 | |
| C5 | IN15 | |

**Note**: Row pins C12, D2, B3, B4, B5, B6 map to ADC channels C0(IN10), D2(external), B3(HC164_DS), B4(A1), B5(A2), B6(A3). The mapping is handled dynamically via `pinToAdcChn()` at init time.

### 4.2 Column Selection via HC164

```
HC164 Output Encoding (15 bits):
  Bit 0  → Column 0  (C6)
  Bit 1  → Column 1  (C7)
  ...
  Bit 14 → Column 14 (A2)

HC164_MR (Master Reset): Clears all outputs LOW
HC164_DS (Data): Serial data input
HC164_CP (Clock): Shifts data in on rising edge

Select col 0:
  MR=LOW → MR=HIGH (clear all)
  Shift 0x00001 (LSB first) → col 0 active

Select col N:
  MR=LOW → MR=HIGH (clear all)
  Shift (1<<N) → col N active
```

**Timing**:
- `shifter_delay(50)` between each bit shift (~50 NOPs)
- 40μs settle time after column selection before ADC
- `ANALOG_DEBOUCE_TIME` (3) ADC conversions per column

### 4.3 ADC Initialization

```c
void matrix_init_custom(void) {
    // 1. Power pins (if configured)
    // 2. HC164 shift register pins as output
    writePinLow(HC164_MR);

    // 3. Row pins as ANALOG input
    for each row:
        palSetLineMode(row_pins[x], PAL_MODE_INPUT_ANALOG);

    // 4. Configure ADC SQR registers (sequence registers)
    //    Set sample time (56 cycles) for each channel
    //    Build SQR1/2/3 for 6-channel sequence

    // 5. Start ADC (free-running, software triggered)
    adcStart(&ADCD1, NULL);

    // 6. Enable analog switch (AN4073 Option 2)
    SYSCFG->PMC |= SYSCFG_PMC_ADC1DC2;

    // 7. Drop initial 5 scan cycles (unstable startup)
    for i = 0..5:
        for each column:
            matrix_read_rows_on_col(col, 0);

    // 8. Initialize analog matrix state
    analog_matrix_init();
}
```

### 4.4 Low Power Entry

```c
void matrix_enter_low_power(void) {
    adcStop(&ADCD1);

    // Disable HC164 control pins (set to input low)
    setPinInputLow(HC164_DS);
    setPinInputLow(HC164_CP);
    setPinInputLow(HC164_MR);

    // Disable analog matrix power (if configured)
    writePin(ANALOG_MATRIX_POWER_PIN, !ENABLE_LEVEL);

    // Enable wakeup interrupt
    palEnableLineEvent(ANALOG_MATRIX_WAKEUP_PIN, FALLING_EDGE);

    // Set all row pins to input low
    for each row:
        setPinInputLow(pins_row[x]);
}
```

---

## 5. Hall Effect Sensor Processing

### 5.1 Per-Key State Structure

```c
typedef struct __attribute__((__packed__)){
    uint8_t  mode;              // 0=global, 1=regular, 2=rapid trigger, etc.
    uint8_t  state;             // AKS_REGULAR_RELEASED, AKS_REGULAR_PRESSED, etc.
    uint8_t  travel;            // Current travel (scaled by TRAVEL_SCALE=6)
    uint8_t  last_travel;       // Previous travel
    bool     debug;
    uint8_t  r, c;              // Row, column position
    uint16_t value;             // Raw ADC value
    uint16_t last_val;          // Last ADC value
    activity_point_t regular;   // {actn_pt, deactn_pt} for regular mode
    union {
        activity_point_t rapid; // {actn_pt, deactn_pt} for rapid trigger
        activity_point_t full;  // For toggle mode
    };
    union {
        uint8_t rpd_trig_sen;   // Rapid trigger sensitivity (0.1mm units)
        uint8_t okmc_idx;       // OKMC setting index
        uint8_t js_axis;        // Joystick X/Y axis
        uint8_t hold;           // Toggle state
    };
    uint8_t rpd_trig_sen_rls;   // Rapid trigger release sensitivity
} analog_key_t;
```

**State machine for Regular mode**:
```
[AKS_REGULAR_RELEASED] ── travel >= actn_pt ──→ [AKS_REGULAR_PRESSED]
      ↑                                                |
      └────────── travel < deactn_pt ──────────────────┘
```

**State machine for Rapid Trigger mode**:
```
[AKS_REGULAR_RELEASED] ── travel >= actn_pt ──→ [AKS_REGULAR_PRESSED]
                                         ↓
                         ┌── travel < deactn_pt ──→ [AKS_RAPID_RELEASED]
                         |                        ┌───────┐
                         └────────── travel >= actn_pt ──→ [AKS_RAPID_PRESSED]
                                                        └── travel < deactn_pt ──→ [AKS_RAPID_RELEASED]
```

### 5.2 Per-Key Configuration Structure

```c
typedef struct __attribute__((__packed__)){
    uint8_t mode:2;              // 2 bits: basic mode (0-1)
    uint8_t act_pt:6;            // 6 bits: actuation point (0-63)
    uint8_t rpd_trig_sen:6;      // 6 bits: rapid trigger sensitivity
    uint8_t rpd_trig_sen_deact:6;// 6 bits: release sensitivity
    uint8_t adv_mode:4;          // 4 bits: advanced mode override
    union {
        uint8_t adv_mode_data;
        uint8_t okmc_idx;        // OKMC index
        uint8_t js_axis;         // Joystick axis
    };
} analog_key_config_t;
```

### 5.3 Configuration Resolution

```c
void update_key_config(uint8_t row, uint8_t col) {
    // 1. Mode resolution (priority: override > per-key > global)
    if (key_cfg->mode == AKM_GLOBAL)
        key->mode = profile->global.mode;
    else if (key_cfg->adv_mode != 0)
        key->mode = key_cfg->adv_mode;
    else
        key->mode = key_cfg->mode;

    // 2. Actuation point resolution
    if (key_cfg->act_pt == 0)
        key->regular.actn_pt = profile->global.act_pt;
    else
        key->regular.actn_pt = key_cfg->act_pt;

    // 3. Deactuation point (always 3 units below actuation)
    key->regular.deactn_pt = key->regular.actn_pt - 3 > 0 ? ... : 0;

    // 4. Rapid trigger sensitivity resolution
    //    Per-key or inherit from global. Release sensitivity defaults
    //    to press sensitivity if not set.

    // 5. Scale all values by TRAVEL_SCALE (6)
    key->regular.actn_pt *= TRAVEL_SCALE;
    key->regular.deactn_pt *= TRAVEL_SCALE;
    key->rpd_trig_sen *= TRAVEL_SCALE;
    key->rpd_trig_sen_rls *= TRAVEL_SCALE;

    // 6. Advance mode setup (OKMC index, joystick axis, toggle)
}
```

### 5.4 Raw Value Processing Pipeline

```c
bool update_raw_value(uint8_t row, col, value) {
    // 1. Validate ADC range
    if value < 1200 || value > 3500: return false;

    // 2. During calibration: store sample, skip processing
    if cali_state: store sample, return false;

    // 3. Background auto-calibration check
    auto_caliration_check(row, col, value);

    analog_key_t *k = &analog_key_matrix[row][col];

    // 4. Hysteresis: ignore tiny changes (< 5 ADC counts)
    if abs(k->last_val - value) < STATIC_HYSTERESIS: return false;

    // 5. Update raw value
    k->last_val = value;
    k->value = value;

    // 6. Convert ADC → travel distance
    k->travel = convert_to_travel(row, col, value);

    // 7. Ignore if travel hasn't changed
    if k->travel == k->last_travel: return false;
    k->last_travel = k->travel;

    // 8. Dispatch to mode-specific handler
    switch (k->mode):
        case AKM_RAPID:    return rapid_trigger_action(k);
        case AKM_DKS:      return okmc_action(k);
        case AKM_GAMEPAD:  return xinput_update(k) or joystick_update(k);
        case AKM_TOGGLE:   return toggle_action(k);
        default:           return regular_trigger_action(k);
}
```

---

## 6. Calibration System

### 6.1 Calibration Data Structure

```c
typedef struct __attribute__((__packed__)){
    uint16_t zero_travel:12;     // ADC value at rest (0 travel)
    uint16_t full_travel:12;     // ADC value at bottom (full press)
} calibrated_value_t;
```

Each key stores 3 bytes of calibration data: 12-bit zero_travel + 12-bit full_travel.

### 6.2 Calibration States

```
CALIB_OFF (0)
    │
    ├─→ CALIB_ZERO_TRAVEL_POWER_ON (1)  // Auto-calibration at power-on
    │       │
    │       ├─ Success → CALIB_OFF (saves zero_travel)
    │       └─ Invalid → CALIB_OFF (uses defaults)
    │
    ├─→ CALIB_ZERO_TRAVEL_MANUAL (2)    // Manual zero travel cal
    │       │
    │       └─→ CALIB_FULL_TRAVEL_MANUAL (3)
    │               │
    │               └─→ CALIB_SAVE_AND_EXIT (4)
    │                       │
    │                       └─→ CALIB_OFF
    │
    └─→ CALIB_CLEAR (5)                 // Clear all calibration
```

### 6.3 Power-On Auto-Calibration

```
1. Sample 8 ADC values per key (CAL_SAMPL_CNT=8)
2. Compute average per key
3. Validate: avg_val > DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE/5
4. Subtract ZERO_TRAVEL_DEAD_ZONE (20) from average
5. Set zero_travel = adjusted average
6. Set full_travel = zero_travel - DEFAULT_FULL_RANGE (900)
7. If all keys valid: save to EEPROM
8. If any key invalid: use defaults for all keys
```

### 6.4 Continuous Auto-Calibration

Runs in background during normal operation, triggered by full press → release cycles:

```
State Machine:
  AUTO_CALIB_OFF → AUTO_CALIB_NEXT_LOOP → AUTO_CALIB_FULL_TRAVEL →
    AUTO_CALIB_ZERO_TRAVEL → AUTO_CALIB_FINISHED → AUTO_CALIB_OFF

Detection:
  1. Key fully pressed (value < AUTO_CALIB_FULL_TRAVEL_THRESHOLD)
  2. Key releasing (value jumps above AUTO_CALIB_ZERO_TRAVEL_THRESHOLD)
  3. Collect 8 samples during rest period (within 1000ms)
  4. If confidence >= 12: update calibration + save to EEPROM

Confidence scoring:
  - Range > 1100: +12 (calibrated) or +6 (uncalibrated)
  - Range > 1000: +4 or +3
  - Range > 900:  +3 or +2
```

### 6.5 EEPROM Storage

Calibration data is stored in **dual locations** for redundancy:

1. **External I2C EEPROM** (address 0x52, 32-byte pages)
2. **Internal Flash EEPROM** (emulated, wear-leveling: logical 3072 bytes, backing 6144 bytes)

**EEPROM Layout (Analog Matrix section)**:
```
Offset 0:          Calibration flag (1 byte)
Offset 1:          Current profile index (1 byte)
Offset 2:          Calibration data: MATRIX_ROWS * MATRIX_COLS * 3 bytes
                   (each: zero_travel 12-bit + full_travel 12-bit)
Offset XX:         Profile data: PROFILE_SIZE * 3 profiles
Offset YY:         Game controller curve points (4 * 2 bytes)
Offset YY+8:       Game controller mode (1 byte)
```

---

## 7. Action Processing Pipeline

### 7.1 Event-to-Action Flow

```
keyevent_t event = MAKE_KEYEVENT(row, col, pressed)
         │
         ▼
┌──────────────────┐
│ action_exec()    │  quantum/action.c
│                  │  - clear_weak_mods()
│                  │  - process_hand_swap() [if SWAP_HANDLES_ENABLE]
│                  │  - Check oneshot timeouts
│                  │  - action_tapping_process() [tap-hold]
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ pre_process      │  quantum/action_tapping.c
│ Record           │  Tap-hold state machine:
│                  │  [reset]→[pressed]→[released]→[reset]
│                  │  Determines: TAP vs HOLD
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ process_record() │  quantum/action.c
│                  │  - process_record_quantum() [module chain]
└────────┬─────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ process_record_quantum()  (40+ modules in AND chain)    │
│                                                         │
│  Module Pipeline (each must return true to proceed):   │
│  1.  process_secure()              Secure input         │
│  2.  preprocess_tap_dance()        Tap dance (layer)   │
│  3.  process_key_lock()            Key lock             │
│  4.  process_dynamic_macro()       Dynamic macro        │
│  5.  process_last_key()            Last key             │
│  6.  process_repeat_key()          Repeat key           │
│  7.  process_clicky()              Clicky sound         │
│  8.  process_haptic()              Haptic feedback      │
│  9.  process_auto_mouse()          Auto mouse           │
│  10. process_record_modules()      Module hooks         │
│  11. process_record_kb()           Keyboard hooks      │
│  12. process_record_via()          VIA protocol        │
│  13. process_secure()              Secure input        │
│  14. process_sequencer()           Sequencer           │
│  15. process_midi()                MIDI                │
│  16. process_audio()               Audio               │
│  17. process_backlight()           Backlight           │
│  18. process_led_matrix()          LED matrix          │
│  19. process_steno()               Steno               │
│  20. process_music()               Music               │
│  21. process_caps_word()           Caps word           │
│  22. process_key_override()        Key override        │
│  23. process_tap_dance()           Tap dance           │
│  24. process_unicode_common()      Unicode             │
│  25. process_leader()              Leader key          │
│  26. process_auto_shift()          Auto shift          │
│  27. process_dynamic_tapping_term() Dynamic tapping   │
│  28. process_space_cadet()         Space cadet         │
│  29. process_magic()               Magic codes         │
│  30. process_grave_esc()           Grave escape        │
│  31. process_underglow()           Underglow           │
│  32. process_rgb_matrix()          RGB matrix          │
│  33. process_joystick()            Joystick            │
│  34. process_programmable_button() Programmable btn    │
│  35. process_autocorrect()         Autocorrect         │
│  36. process_tri_layer()           Tri-layer           │
│  37. process_default_layer()       Default layer       │
│  38. process_layer_lock()          Layer lock          │
│  39. process_connection()          Connection state    │
│  40. process_oneshot()             One-shot            │
│  41. process_quantum()             Quantum hooks       │
└────────┬────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────┐
│ action_for_key   │  quantum/keymap_common.c
│ (layer lookup)   │  - layer_switch_get_layer()
│                  │    Checks layer_state | default_layer_state
│                  │    Returns topmost non-transparent layer
│                  │  - action_for_keycode()
│                  │    Converts keycode → action_t
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ process_action() │  quantum/action.c
│                  │  switch(action.kind.id):
│                  │  - ACT_LMODS/ACT_RMODS: add_mods + add_key
│                  │  - ACT_USAGE: host_system_send / host_consumer_send
│                  │  - ACT_MOUSEKEY: register_mouse
│                  │  - ACT_LAYER: layer_on/off/invert
│                  │  - ACT_LAYER_TAP: register_code / layer_on
│                  │  - ACT_SWAP_HANDS: swap_hands toggle
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ register_code()  │  quantum/action.c
│                  │  - add_key(code) → keyboard_report->keys[]
│                  │  - add_mods(mods) → keyboard_report->mods
│                  │  - send_keyboard_report()  ← USB SEND
└──────────────────┘
```

### 7.2 Tap-Hold State Machine

```
                    ┌───────────────┐
                    │    RESET      │
                    │ (no active    │
                    │  tap-hold)    │
                    └───────┬───────┘
                            │ tap-hold key pressed
                            ▼
                    ┌───────────────┐
                    │   PRESSED     │
                    │ (waiting for  │
                    │ settle)       │
                    └───┬───────────┘
                        │
              ┌─────────┼─────────┐
              │         │         │
    ┌─────────┴───┐  ┌──┴───┐  ┌──┴────────────┐
    │ Other key   │  │ TAP  │  │ TAPPING_TERM  │
    │ pressed     │  │ key  │  │ expired → HOLD│
    │ (interrupt) │  │ released│               │
    └──┬──────────┘  └──┬───┘   └───────────────┘
       │                │
       ▼                ▼
  HOLD settled    TAP settled
  immediately     → process tap code

  (if HOLD_ON_OTHER_KEY_PRESS)
```

**Waiting Buffer**: Events arriving while tap-hold is unsettled are queued in `waiting_buffer[]` (circular buffer) and processed after settling.

### 7.3 Keycode Range Encoding

```
0x0000-0x00FF  BASIC_KEYCODE_RANGE     Standard HID keycodes
0x0100-0x1FFF  QK_MODS_RANGE           Modifiers + key
0x2000-0x3FFF  QK_MOD_TAP_RANGE        Mod-Tap (KC_LC LT KC_A)
0x4000-0x4FFF  QK_LAYER_TAP_RANGE      Layer-Tap (MO(1) + KC_A)
0x5000-0x500F  QK_TO_RANGE             TO(layer)
0x5010-0x501F  QK_MOMENTARY_RANGE      MO(layer)
0x5020-0x502F  QK_DEF_LAYER_RANGE      DF(layer)
0x5030-0x503F  QK_TOGGLE_LAYER_RANGE   TG(layer)
0x5040-0x504F  QK_ONE_SHOT_LAYER_RANGE OSL(layer)
0x5060-0x506F  QK_ONE_SHOT_MOD_RANGE   OSOM(mod)
0x5070-0x507F  QK_LAYER_TAP_TOGGLE     TT(layer)
0x5080-0x50FF  QK_LAYER_MOD_RANGE      LM(layer, mod)
0x5400-0x54FF  QK_LAYER_MOD_EXT_RANGE  Extended layer-mods
0x5C00-0x5CFF  QK_SWAP_HANDS_RANGE     Swap hands
0x6000-0x63FF  QK_MACRO_RANGE          Macros
0x6400-0x64FF  QK_TAP_DANCE_RANGE      Tap dance
0x7000-0x70FF  QK_USER_RANGE           User-defined
0x7100-0x73FF  QK_FUN_RANGE            Function keys
0x7400-0x77FF  QK_STENO_RANGE          Stenography
0x7800-0x78FF  QK_MIDI_RANGE           MIDI
0x7900-0x79FF  QK_AUDIO_RANGE          Audio
0x7A00-0x7AFF  QK_NKRO_RANGE           NKRO
0x8000-0x8FFF  QK_MOUSEKEY_RANGE       Mouse keys
0x9000-0x9FFF  QK_UNICODE_RANGE        Unicode
0xA000-0xA0FF  QK_UNICODEMAP_RANGE     Unicode map
0xB000-0xB1FF  QK_SEND_STRING_RANGE    Send string
0xC000-0xC2FF  QK_SEQUENCER_RANGE      Sequencer
0xD000-0xDFFF  QK_DYNAMIC_MACRO_RANGE  Dynamic macro
0xE000-0xE03F  QK_KEY_OVERRIDE_RANGE   Key override
0xE040-0xE0BF  QK_KEY_OVERRIDE_EXT     Extended key override
0xE0C0-0xE0FF  QK_DYNAMIC_TAPPING_TERM Dynamic tapping
```

---

## 8. Layer & Keymap System

### 8.1 Layer State

```c
// Q3 Max uses default 16-bit layer state (16 layers)
typedef uint16_t layer_state_t;

extern layer_state_t default_layer_state;  // Persistent (stored in EEPROM)
extern layer_state_t layer_state;          // Temporary (OSL, MO, TG)
```

### 8.2 Layer Priority (Overlay Model)

Layers are checked from highest to lowest. The first non-transparent action wins:

```c
uint8_t layer_switch_get_layer(keypos_t key) {
    layer_state_t layers = layer_state | default_layer_state;
    for (int8_t i = MAX_LAYER - 1; i >= 0; i--) {
        if (layers & (1UL << i)) {
            action = action_for_key(i, key);
            if (action.code != ACTION_TRANSPARENT) return i;
        }
    }
    return 0;  // fall back to layer 0
}
```

### 8.3 Source Layer Cache

When a key is pressed on a non-default layer (e.g., `LT(1, A)` on layer 0), the firmware caches which layer the key was pressed on. On release, the cached layer is used instead of the current layer, preventing stuck keys when switching layers:

```c
uint8_t source_layers_cache[((MATRIX_ROWS * MATRIX_COLS) + 7) / 8][MAX_LAYER_BITS];

action_t store_or_get_action(bool pressed, keypos_t key) {
    if (pressed) {
        layer = layer_switch_get_layer(key);
        update_source_layers_cache(key, layer);
    } else {
        layer = read_source_layers_cache(key);  // Use cached layer
    }
    return action_for_key(layer, key);
}
```

### 8.4 DIP Switch Integration

The Q3 Max has a DIP switch on C9 that toggles between Mac and Win layouts:

```c
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));
        // active=1 → layer 2 (Mac), active=0 → layer 0 (Win)
    }
    return true;
}
```

---

## 9. HID Report Generation

### 9.1 Report Structures

**Standard Keyboard Report (8 bytes)**:
```
┌────────┬────────┬──────┬──────┬──────┬──────┬──────┬──────┐
│  mods  │  resvd │ key0 │ key1 │ key2 │ key3 │ key4 │ key5 │
│  (1B)  │  (1B)  │ (1B) │ (1B) │ (1B) │ (1B) │ (1B) │ (1B) │
└────────┴────────┴──────┴──────┴──────┴──────┴──────┴──────┘
```

**NKRO Report (16 bytes)**:
```
┌────────┬────────┬────────┬────...────┬────────┐
│  mods  │ bits0  │ bits1  │    ...    │ bit14  │
│  (1B)  │ (1B)   │ (1B)   │           │ (1B)   │
└────────┴────────┴────────┴───────────┴────────┘
Total: 15 bytes (240 bits of key state)
```

### 9.2 NKRO on Q3 Max

Q3 Max supports NKRO via `NKRO_ENABLE = yes` in info.json and `WIRELESS_NKRO_ENABLE` for wireless mode. The NKRO report is sent when:
1. `keymap_config.nkro` is true
2. Host supports NKRO (`host_can_send_nkro()`)

### 9.3 Report Sending

```c
void send_keyboard_report(void) {
    host_keyboard_send(keyboard_report);
}

void host_keyboard_send(report_keyboard_t *report) {
    host_driver_t *driver = host_get_active_driver();
    (*driver->send_keyboard)(report);  // LUFA: Endpoint_Write_Stream_LE
}
```

**Report batching**: Every call to `register_code()` / `unregister_code()` sends an immediate HID report. This is critical for Q3 Max because:
- Rapid trigger mode generates frequent press/release events
- Each travel threshold crossing triggers a new report

### 9.4 Q3 Max Custom register_code16

The Q3 Max overrides `register_code16`/`unregister_code16` for RDP compatibility:

```c
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        // Send each modifier as a SEPARATE HID report
        // (not batched in one report)
        if (code & QK_LCTL) register_code(KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(KC_LEFT_GUI);
        uint8_t basic = code & 0xFF;
        if (basic) register_code(basic);
    } else {
        // Non-QK_MODS: standard QMK behavior
        register_code(code);
    }
}
```

**Rationale**: RDP and some remote desktop clients expect modifiers to arrive sequentially (as physical keypresses would produce), not batched in a single report.

---

## 10. Advanced Features

### 10.1 Rapid Trigger

Rapid Trigger provides dynamic actuation points that follow the user's finger position. It is designed for competitive gaming (FPS, rhythm games).

**Behavior**:
- When pressing deeper: `deactn_pt = current_travel`, `actn_pt = current_travel + sensitivity`
- When releasing: `deactn_pt = current_travel - release_sensitivity`, `actn_pt = current_travel`
- Sensitivity defaults to 0.4mm (4 * 0.1mm units)
- Release sensitivity defaults to press sensitivity

**State machine**:
```
RELEASED → press → PRESSED → release → RELEASED
                    ↑           ↓
                    └── PRESSED → release → RAPID_RELEASED → press → RAPID_PRESSED
```

### 10.2 OKMC (One-Key-Many-Commands)

OKMC maps different actuation depths to different keycodes:

```
Travel depth:   0mm ─── 1mm ─── 2mm ─── 3mm ─── 4mm
                │       │       │       │       │
Keycode:       KC_A   KC_B   KC_C   KC_D    KC_E
                │       │       │       │       │
              shallow  shallow  deep    deep   (extra)
              act      deact    act     deact
```

Each OKMC config has:
- 4 keycodes (16 bits each)
- 4 action bitmasks (shallow/deep actuation/deactuation for 2 keycodes)
- Travel thresholds for shallow and deep actuation

### 10.3 SOCD (Simultaneous Opposing Cardinal Directions)

SOCD resolves conflicts when opposing keys are pressed simultaneously (e.g., Left + Right in fighting games):

```
SOCD Modes:
  Neutral:    Both → neither registers
  Last Priority:  Last pressed wins
  First Priority: First pressed wins
  Roll:         Alternating
```

SOCD is processed in `analog_matrix_task()` via `socd_action()`.

### 10.4 Game Controller Mode

Keys can be mapped to XInput/Joystick game controller inputs:
- Analog travel maps to joystick axis values
- Actuation maps to button presses
- Curve points (4 points) map travel to non-linear axis values
- Profiles can enable/disable game controller mode

### 10.5 Raw HID Configuration Protocol

The firmware exposes a Raw HID endpoint for host-side configuration:

**Command format**:
```
Byte 0: 0xA9 (Analog Matrix command group)
Byte 1: Sub-command
Bytes 2+: Parameters/Response
```

**Key commands**:
| Cmd | Name | Direction | Purpose |
|-----|------|-----------|---------|
| 0x01 | GET_VERSION | ← | Firmware version |
| 0x10 | GET_PROFILES_INFO | ← | Profile metadata |
| 0x11 | SELECT_PROFILE | → | Switch profile |
| 0x12 | GET_PROFILE_RAW | ← | Raw profile data |
| 0x13 | SET_PROFILE_NAME | → | Name a profile |
| 0x14 | SET_TRAVAL | → | Set actuation/sensitivity |
| 0x15 | SET_ADVANCE_MODE | → | Set OKMC/SOCD/gamepad |
| 0x16 | SET_SOCD | → | Configure SOCD mode |
| 0x1E | RESET_PROFILE | → | Reset profile to defaults |
| 0x1F | SAVE_PROFILE | → | Save profile to EEPROM |
| 0x20 | GET_CURVE | ← | Get game controller curve |
| 0x21 | SET_CURVE | → | Set game controller curve |
| 0x30 | GET_REALTIME_TRAVEL | ← | Debug: current travel |
| 0x40 | CALIBRATE | → | Start manual calibration |
| 0x41 | GET_CALIBRATE_STATE | ← | Calibration progress |
| 0x42 | GET_CALIBRATED_VALUE | ← | Per-key calibration data |

---

## 11. Custom Board Modifications

### 11.1 GPIO Pin Configuration (board.h)

All GPIO pins are explicitly configured to prevent floating:
- **GPIOA**: Mix of INPUT, ALTERNATE (SPI, OTG, audio), FLOATING (SWDIO, VBUS)
- **GPIOB**: All INPUT + PULLDOWN (wireless, encoder, battery, I2C)
- **GPIOC**: All INPUT + PULLUP/PULLDOWN (audio codec, OTG, DIP switch)
- **GPIOD**: All INPUT + PULLUP
- **GPIOE**: All INPUT + PULLUP (IMU interrupts)

### 11.2 ChibiOS HAL Configuration (halconf.h)

```c
#define HAL_USE_SPI TRUE              // RGB matrix, EEPROM
#define HAL_USE_RTC TRUE              // Wireless timing
#define PAL_USE_CALLBACKS TRUE        // Wireless interrupt, encoder
```

### 11.3 MCU Clock Configuration (mcuconf.h)

```
HSE: 16 MHz
PLL: M=8, N=96, P=4, Q=4
SYSCLK: 16 MHz (16 / 8 * 96 / 4 = 48 MHz... wait)

Actually:
  PLL_VCO = HSE/M * N = 16/8 * 96 = 192 MHz
  SYSCLK  = PLL_VCO/P = 192/4 = 48 MHz
  USBclk  = PLL_VCO/Q = 192/4 = 48 MHz (USB-safe)
```

### 11.4 Leader Key

```c
#define LEADER_NO_TIMEOUT       // No global timeout
#define LEADER_TIMEOUT 500      // 500ms between keystrokes
#define LEADER_PER_KEY_TIMING   // Per-key timeout tracking
```

### 11.5 RGB Matrix

- **Driver**: SNLED27351 via SPI
- **Select pins**: B8, B9 (12-channel scan, phase 12)
- **SPI divisor**: 16
- **Default animation**: `RGB_MATRIX_SOLID_REACTIVE_SIMPLE`
- **19 animations supported** (band_spiral, breathing, cycle_*, digital_rain, etc.)

---

## 12. Data Flow Summary

### 12.1 Complete Key Press Flow

```
User presses key
    │
    ▼
HC164 shift register activates column
    │
    ▼
ADC1 converts Hall Effect voltage (12-bit, ~15μs)
    │
    ▼
update_raw_value()
    ├─ Validate ADC range (1200-3500)
    ├─ Auto-calibration check (background)
    ├─ Hysteresis check (ΔADC < 5 → skip)
    ├─ Convert ADC → travel (polynomial)
    └─ Mode-specific action:
        └─ regular_trigger_action() → sets bit in analog_raw_matrix[row]
    │
    ▼
changed_matrix[] updated (bit-level change detection)
    │
    ▼
virtual_matrix[] |= game_controller_matrix | okmc_matrix
    │
    ▼
QMK matrix_task() detects changed bits
    │
    ▼
For each changed key:
    keyevent_t event = MAKE_KEYEVENT(row, col, pressed)
    action_exec(event)
    │
    ├─ clear_weak_mods()
    ├─ action_tapping_process() → TAP/HOLD decision
    ├─ process_record_quantum() → 40+ module chain
    ├─ action_for_keycode() → layer lookup → action_t
    └─ process_action() → register_code() / unregister_code()
        │
        ├─ add_key(code) → keyboard_report->keys[]
        ├─ add_mods(mods) → keyboard_report->mods
        └─ send_keyboard_report()
            │
            ▼
        host_keyboard_send(report)
            │
            ▼
        Endpoint_Write_Stream_LE() → USB IN packet
            │
            ▼
        Host OS receives HID Keyboard Report
```

### 12.2 Matrix Data Flow (Per Scan Cycle)

```
matrix_scan_custom()
    │
    ├─ Save previous state: memcpy(last_raw_matrix, raw_matrix)
    ├─ Clear changed_matrix
    │
    ├─ For each column (0..16):
    │   ├─ HC164_output(column_select_bits)
    │   ├─ wait_us(40)
    │   ├─ For 3 samples:
    │   │   ├─ adcConvert(&ADCD1, &adcgrpcfg, samples, 1)
    │   │   ├─ For each row (0..5):
    │   │   │   ├─ update_raw_value(row, col, samples[row])
    │   │   │   └─ analog_matrix_get_key_state(row, col)
    │   │   └─ Recheck: first_sample == recheck_samples?
    │   └─ unselect_col()
    │
    ├─ analog_matrix_task()
    │   ├─ calibrate() (if in calibration mode)
    │   ├─ profile_indication_timer_check()
    │   └─ socd_action()
    │
    ├─ raw_matrix &= analog_matrix_mask
    ├─ virtual_matrix |= game_controller_matrix | okmc_matrix
    │
    └─ memcmp(raw_matrix, last_raw_matrix) → changed?
        └─ if changed: matrix_changed = true
```

---

## 13. Performance & Resource Analysis

### 13.1 Timing Analysis

| Operation | Duration | Notes |
|-----------|----------|-------|
| HC164 shift (15 bits) | ~500μs | 50 NOPs per bit |
| ADC settle delay | 40μs | Per column |
| ADC conversion (6 channels) | ~15μs | 56-cycle sample time |
| Per-column scan time | ~555μs | HC164 + settle + ADC |
| Full matrix scan (17 cols) | ~9.4ms | Before debounce |
| With 3x debounce recheck | ~28ms | Worst case |
| Polynomial travel calc | ~20μs | 32-bit float ops |
| update_raw_value() | ~50μs | Per key |
| Full scan + processing | ~30ms | Including all keys |

### 13.2 Memory Usage

| Resource | Estimate |
|----------|----------|
| **analog_key_matrix** | 16 bytes × 6 × 17 = 1,632 bytes |
| **calib_values** | 3 bytes × 6 × 17 = 306 bytes |
| **saved_calib_values** | 3 bytes × 6 × 17 = 306 bytes |
| **calibrate_values** | 2 bytes × 6 × 17 × 8 = 1,632 bytes |
| **auto_calib** | ~20 bytes × 6 × 17 = 2,040 bytes |
| **raw_matrix** | 8 bytes × 6 = 48 bytes |
| **matrix** | 8 bytes × 6 = 48 bytes |
| **virtual_matrix** | 8 bytes × 6 = 48 bytes |
| **EEPROM (calibration)** | 306 bytes |
| **EEPROM (profiles)** | ~2,000 bytes |
| **Total RAM (analog)** | ~6,060 bytes (~3% of 192KB SRAM) |
| **Total EEPROM** | ~2,300 bytes |

### 13.3 Report Rate

Q3 Max uses standard QMK USB report rate (default 1000 Hz = 1ms interval). In wireless mode, the rate may be reduced by the Bluetooth module.

---

## 14. Error Handling & Edge Cases

### 14.1 ADC Range Validation

```c
if value < 1200 || value > 3500: return false
```

- Values below 1200: No sensor detected or short circuit
- Values above 3500: Open circuit or sensor failure
- These keys are masked out via `analog_matrix_mask`

### 14.2 Calibration Failure Recovery

- Power-on auto-calibration validates all keys
- If any key fails validation, defaults are used for all keys
- Manual calibration allows per-key recovery
- Calibration data is stored in dual EEPROM locations

### 14.3 Key State Consistency

- Deactuation point is always 3 travel units below actuation point (hysteresis)
- Travel change must exceed 0 units (same as last_travel) to generate events
- Analog debounce prevents bounce through 3-sample recheck

### 14.4 Power Management

- `matrix_enter_low_power()` stops ADC and disables peripherals
- Wakeup via `ANALOG_MATRIX_WAKEUP_PIN` interrupt
- USB power sense (B0) detects USB connection state
- Battery charging monitored via B13
- Battery low indicated via A8 LED

### 14.5 RDP Compatibility

The custom `register_code16`/`unregister_code16` ensures QK_MODS keycodes send modifiers as separate HID reports, fixing RDP shortcut detection issues.

---

## Appendix A: File Inventory

### Analog Matrix Module
| File | Purpose |
|------|---------|
| `analog_matrix_scan.c` | ADC scanning, HC164 control, matrix_init_custom, matrix_scan_custom |
| `analog_matrix.c` | Core processing: calibration, travel conversion, Raw HID commands |
| `analog_matrix.h` | Public API and constants |
| `analog_matrix_type.h` | Type definitions (analog_key_t, calibrated_value_t, etc.) |
| `analog_matrix_eeconfig.h` | EEPROM offset definitions |
| `action_regular_trigger.c` | Regular mode actuation logic |
| `action_rapid_trigger.c` | Rapid Trigger mode |
| `action_okmc.c` | OKMC multi-depth mode |
| `action_toggle.c` | Toggle/latch mode |
| `action_socd.c` | SOCD resolution |
| `action_joystick.c` | Joystick gamepad mode |
| `action_xinput.c` | XInput gamepad mode |
| `game_controller_common.c` | Game controller shared state |
| `game_controller_common.h` | Game controller types |
| `profile.c` | Profile management (load, save, switch) |
| `profile.h` | Profile API |
| `eeprom_he.c` | HE-specific EEPROM operations |
| `eeprom_he.h` | EEPROM HE API |
| `sqrt.c` | Square root utility |
| `sqrt.h` | Sqrt API |
| `xinput_keycodes.h` | XInput keycode definitions |
| `usb_descriptor_override.c` | Custom USB descriptors |

### Q3 Max Board Files
| File | Purpose |
|------|---------|
| `q3_max.c` | Board init, dip switch, register_code16 override |
| `q3_max_user.c` | User-level callbacks |
| `debug_user.c` | Debug utilities |
| `config.h` | Feature macros, pin defines |
| `rules.mk` | Build rules |
| `info.json` | Data-driven config |
| `board.h` | GPIO pin mode overrides |
| `halconf.h` | ChibiOS HAL config |
| `mcuconf.h` | MCU clock config |

### QMK Core Files
| File | Purpose |
|------|---------|
| `quantum/matrix.c` | Matrix scanning infrastructure |
| `quantum/keyboard.c` | Main loop, matrix_task, keyboard_task |
| `quantum/action.c` | Action execution, register/unregister code |
| `quantum/action_tapping.c` | Tap-hold state machine |
| `quantum/action_layer.c` | Layer state management |
| `quantum/keymap_common.c` | Keycode-to-action conversion |
| `quantum/quantum.c` | Module processing pipeline |
| `quantum/keyboard.h` | Event types, keypos_t, keyevent_t |
| `quantum/action.h` | action_t, keyrecord_t |
| `quantum/action_code.h` | Action type IDs |
| `quantum/keycodes.h` | Keycode ranges |
| `quantum/modifiers.h` | Modifier bit definitions |
| `tmk_core/protocol/report.c` | HID report manipulation |
| `tmk_core/protocol/host.c` | Host driver dispatch |

---

## Appendix B: Constants Reference

### Travel & Actuation
| Constant | Value | Unit | Meaning |
|----------|-------|------|---------|
| `FULL_TRAVEL_UNIT` | 40 | scaled units | 4.0mm full travel |
| `TRAVEL_SCALE` | 6 | multiplier | Scales to travel units |
| `DEFAULT_ACTUATION_POINT` | 20 | scaled units | 2.0mm default actuation |
| `DEFAULT_RAPID_TRIGGER_SENSITIVITY` | 4 | scaled units | 0.4mm RT sensitivity |
| `MIN_ACTUATION` | 5 | scaled units | Minimum travel to register |
| `ZERO_TRAVEL_DEAD_ZONE` | 20 | ADC counts | Dead zone at rest |
| `BOTTOM_DEAD_ZONE` | 38 | scaled units | Dead zone at bottom |
| `BOTTOM_JITTER` | 80 | ADC counts | Jitter compensation |
| `STATIC_HYSTERESIS` | 5 | ADC counts | Min change to register |
| `RAPID_TRIGGER_TICK` | 10 | ms | RT update interval |

### ADC Ranges
| Constant | Value | Unit | Meaning |
|----------|-------|------|---------|
| `DEFAULT_ZERO_TRAVEL_VALUE` | 3000 | ADC counts | Default at rest |
| `DEFAULT_FULL_RANGE` | 900 | ADC counts | Default range |
| `DEFAULT_FULL_TRAVEL_VALUE` | 2100 | ADC counts | Default at bottom |
| `REF_ZERO_TRAVEL` | 3121 | ADC counts | Reference at rest |
| `REF_FULL_TRAVEL` | 1940 | ADC counts | Reference at bottom |
| `VALID_ANALOG_RAW_VALUE_MIN` | 1200 | ADC counts | Below: no sensor |
| `VALID_ANALOG_RAW_VALUE_MAX` | 3500 | ADC counts | Above: no sensor |
| `ABNORMAL_ANALOG_RAW_THRESHOLD_VALUE` | 3250 | ADC counts | Abnormal detection |

### Calibration
| Constant | Value | Unit | Meaning |
|----------|-------|------|---------|
| `CAL_SAMPL_CNT` | 8 | samples | Calibration sample count |
| `AUTO_CALIB_VALID_RELASING_TIME` | 1000 | ms | Valid release window |
| `AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE` | 50 | ADC counts | Zero travel jitter |
| `AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE` | 100 | ADC counts | Full travel jitter |

### Debounce
| Constant | Value | Unit | Meaning |
|----------|-------|------|---------|
| `ANALOG_DEBOUCE_TIME` | 3 | samples | Analog debounce count |
| `DEBOUNCE` | 50 | ms | QMK debounce time (info.json) |
