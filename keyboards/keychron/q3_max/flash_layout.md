# Q3 Max Flash Layout

This document describes the memory organization of the STM32F401xC flash used in the Keychron Q3 Max. The layout is designed to provide a dedicated reserve for loadable modules while maximizing the available space for the main firmware body.

## Memory Map

| Sector | Address Range | Size | Purpose | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Sector 0** | `0x08000000` - `0x08003FFF` | 16 KB | **Firmware (A)** | Interrupt Vector Table, Reset Handler, and startup code. |
| **Sector 1** | `0x08004000` - `0x08007FFF` | 16 KB | **EEPROM Cache** | Emulated EEPROM for settings, keymaps, and config. |
| **Sector 2** | `0x08008000` - `0x0800BFFF` | 16 KB | **Module Reserve** | Slot 0-3: Loadable modules executing in-place (XIP). |
| **Sector 3** | `0x0800C000` - `0x0800FFFF` | 16 KB | **Module Reserve** | Slot 4-7: Loadable modules executing in-place (XIP). |
| **Sector 4** | `0x08010000` - `0x0801FFFF` | 64 KB | **Firmware (B)** | Main firmware body (Code and Read-Only Data). |
| **Sector 5** | `0x08020000` - `0x0803FFFF` | 128 KB | **Firmware (B)** | Main firmware body continuation. |

## Design Rationale

### 1. Boot Process
The CPU always begins execution at `0x08000000` (Sector 0). The linker is configured to place the main application logic starting at `0x08010000` (Sector 4), creating a gap where modules can be safely stored.

### 2. The Middle-Reserve Strategy
By dedicating Sectors 2 and 3 to the Module System, the architecture achieves:
- **Firmware Stability**: The main firmware has $\sim 208 \text{ KB}$ of total available space, ensuring high growth headroom.
- **Low-Risk Updates**: Module updates only require erasing a 16 KB sector instead of the massive 128 KB Sector 5.
- **Physical Isolation**: Modules are physically separated from the critical EEPROM cache and the main application code.

### 3. Linker Configuration
The memory layout is defined in the linker script:
`platforms/chibios/boards/common/ld/stm32f4xx_common.ld`

It is configured as follows:
- `flash0`: `org = 0x08000000, len = 16k`
- `flash1`: `org = 0x08004000, len = 16k`
- `flash2`: `org = 0x08010000, len = f4xx_flash_size - 64k`

## Module Slots
The reserve area (S2 + S3) is divided into 8 fixed slots of 4 KB each:

| Slot | Start Address | End Address | Sector |
| :--- | :--- | :--- | :--- |
| 0 | `0x08008000` | `0x08008FFF` | Sector 2 |
| 1 | `0x08009000` | `0x08009FFF` | Sector 2 |
| 2 | `0x0800A000` | `0x0800AFFF` | Sector 2 |
| 3 | `0x0800B000` | `0x0800BFFF` | Sector 2 |
| 4 | `0x0800C000` | `0x0800CFFF` | Sector 3 |
| 5 | `0x0800D000` | `0x0800DFFF` | Sector 3 |
| 6 | `0x0800E000` | `0x0800EFFF` | Sector 3 |
| 7 | `0x0800F000` | `0x0800FFFF` | Sector 3 |
