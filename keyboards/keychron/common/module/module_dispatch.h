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
#include "action_layer.h"  /* layer_state_t */

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

/**
 * @brief Dispatcher for get_combo_must_hold.
 * Returns false when no module hooks it (QMK default).
 */
bool module_dispatch_get_combo_must_hold(uint16_t index, combo_t *combo);

/**
 * @brief Dispatcher for get_combo_must_tap.
 * Returns false when no module hooks it (QMK default).
 */
bool module_dispatch_get_combo_must_tap(uint16_t index, combo_t *combo);

/**
 * @brief Dispatcher for get_combo_must_press_in_order.
 * Returns true when no module hooks it (QMK default).
 */
bool module_dispatch_get_combo_must_press_in_order(uint16_t index, combo_t *combo);

/**
 * @brief Dispatcher for process_combo_key_release.
 * Returns false when no module hooks it (QMK default: do not release combo).
 */
bool module_dispatch_process_combo_key_release(uint16_t index, combo_t *combo, uint8_t key_index, uint16_t keycode);

/**
 * @brief Dispatcher for process_combo_key_repress.
 * Returns false when no module hooks it (QMK default: no special repress behavior).
 */
bool module_dispatch_process_combo_key_repress(uint16_t index, combo_t *combo, uint8_t key_index, uint16_t keycode);

/**
 * @brief Dispatcher for combo_ref_from_layer.
 * Returns the input layer when no module hooks it (QMK default).
 */
uint8_t module_dispatch_combo_ref_from_layer(uint8_t layer);

/* ------------------------------------------------------------------ */
/* Key processing dispatcher prototypes                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Cooperative dispatch helper for process_record_user.
 *
 * Call this from inside your keymap's process_record_user to route
 * keypress handling to the module owning MODULE_KEY_HOOK_PROCESS_RECORD.
 * Returns true when no module claims the hook (continue processing).
 * Returns false when a module suppresses the keypress.
 *
 * This is NOT a strong override — it is a public helper. Keymaps that
 * don't call it make this hook unreachable (opt-in per keymap).
 */
bool module_dispatch_process_record(uint16_t keycode, keyrecord_t *record);

/**
 * @brief Strong override of QMK's pre_process_record_user.
 *
 * Automatically invoked by QMK before the keymap's process_record_user.
 * No keymap action required. Returns true when no module claims the hook.
 */
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record);

/**
 * @brief Strong override of QMK's layer_state_set_user.
 *
 * Automatically invoked by QMK on layer changes.
 * Returns the state unchanged when no module claims the hook.
 */
layer_state_t layer_state_set_user(layer_state_t state);

/* ------------------------------------------------------------------ */
/* Lifecycle dispatcher prototypes                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Strong override of QMK's housekeeping_task_user.
 *
 * Automatically invoked by QMK every main-loop tick.
 * No-op when no module claims the hook.
 */
void housekeeping_task_user(void);

/**
 * @brief Strong override of QMK's shutdown_user.
 *
 * Automatically invoked by QMK before reboot/bootloader jump.
 * Returns true (allow shutdown) when no module claims the hook.
 */
bool shutdown_user(bool jump_to_bootloader);
