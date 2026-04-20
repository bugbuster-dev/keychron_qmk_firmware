/*
    Module Dispatch - Header
    Provides dispatcher functions for QMK callbacks that can be overridden by modules.
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "process_keycode/process_combo.h"
#include "quantum/keycodes.h"
#include "action.h"

/* Dispatcher function prototypes */

/**
 * @brief Dispatcher for combo_should_trigger.
 * @param combo_index The index of the combo.
 * @param combo The combo definition.
 * @param keycode The keycode being processed.
 * @param record The keyrecord.
 * @return true if the combo should trigger, false otherwise.
 */
bool module_dispatch_combo_should_trigger(uint16_t combo_index, combo_t *combo,
                                          uint16_t keycode, keyrecord_t *record);

/**
 * @brief Dispatcher for process_combo_event.
 * @param combo_index The index of the combo.
 * @param pressed true if the combo was pressed, false if released.
 */
void module_dispatch_process_combo_event(uint16_t combo_index, bool pressed);

/**
 * @brief Dispatcher for get_combo_term.
 * @param index The combo index.
 * @param combo The combo definition.
 * @return The combo term in milliseconds.
 */
uint16_t module_dispatch_get_combo_term(uint16_t index, combo_t *combo);
