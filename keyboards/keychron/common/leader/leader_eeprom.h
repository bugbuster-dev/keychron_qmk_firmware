// keyboards/keychron/common/leader/leader_eeprom.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "eeconfig_leader.h"

typedef struct __attribute__((packed)) {
    uint16_t sequence[LEADER_DEF_MAX_SEQ_LEN]; // up to 5 keycodes, KC_NO (0) terminated
    uint16_t keycode;                          // action keycode when matched
} leader_def_t;

_Static_assert(sizeof(leader_def_t) == 12, "leader_def_t unexpected size");

void leader_eeprom_init(void);
void leader_eeprom_save(void);
void leader_eeprom_set(uint8_t slot, const leader_def_t *def);
void leader_eeprom_get(uint8_t slot, leader_def_t *out);
void leader_eeprom_clear(uint8_t slot);
void leader_eeprom_reset_defaults(void);

// Early termination: check current leader sequence against all slots.
// Call after each keypress during an active leader sequence.
// Returns true if a match was found and the action was fired.
bool leader_eeprom_try_match(const uint16_t *sequence, uint8_t seq_len);

// Returns true if at least one slot has `sequence` as a prefix (more keys possible).
bool leader_eeprom_has_prefix(const uint16_t *sequence, uint8_t seq_len);
