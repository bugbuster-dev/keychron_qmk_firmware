// quantum/features/sticky_combo_adapter.h
#pragma once

#include <stdint.h>
#include "pipeline.h"

// User-provided definition of a sticky combo.
// key1 + key2 pressed simultaneously fires combo_action.
// After that, with key2 held, tapping key1 fires tap_action_1.
// With key1 held, tapping key2 fires tap_action_2.
// Any action set to KC_NO (0) is silently skipped.
typedef struct {
    uint16_t key1;
    uint16_t key2;
    uint16_t combo_action;
    uint16_t tap_action_1;
    uint16_t tap_action_2;
} sticky_combo_def_t;

// User must provide these in their keymap:
extern const sticky_combo_def_t sticky_combos[];
extern const uint8_t sticky_combo_count;

// Pipeline registration entry point.
sm_machine_t *sticky_combo_machine_get(void);
