#pragma once

#include <stdint.h>
#include "eeconfig_combo.h"

typedef struct __attribute__((packed)) {
    uint16_t keys[COMBO_DEF_MAX_KEYS]; // COMBO_END (0) terminated
    uint16_t keycode;                  // result keycode
} combo_def_t;

_Static_assert(sizeof(combo_def_t) == 18, "combo_def_t unexpected padding");

void combo_eeprom_init(void);
void combo_eeprom_save(void);
void combo_eeprom_set(uint8_t slot, const uint16_t *keys, uint16_t keycode);
void combo_eeprom_get(uint8_t slot, combo_def_t *out);
void combo_eeprom_clear(uint8_t slot);
void combo_eeprom_reset_defaults(void);
