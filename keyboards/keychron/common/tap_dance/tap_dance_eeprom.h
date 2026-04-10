// keyboards/keychron/common/tap_dance/tap_dance_eeprom.h
#pragma once

#include <stdint.h>
#include "eeconfig_tap_dance.h"

typedef struct __attribute__((packed)) {
    uint16_t kc1;   // tap x1  (0 = slot disabled)
    uint16_t kc2;   // tap x2  (0 = fall through to kc1)
    uint16_t kc3;   // tap x3  (0 = fall through to kc2)
    uint16_t hold;  // hold    (0 = do nothing)
} tap_dance_def_t;

_Static_assert(sizeof(tap_dance_def_t) == 8, "tap_dance_def_t unexpected size");

void tap_dance_eeprom_init(void);
void tap_dance_eeprom_set(uint8_t slot, const tap_dance_def_t *def);
void tap_dance_eeprom_get(uint8_t slot, tap_dance_def_t *out);
void tap_dance_eeprom_clear(uint8_t slot);
void tap_dance_eeprom_reset_defaults(void);
void tap_dance_eeprom_save(void);
