/*
    Module Dispatch - Implementation
    Provides dispatcher functions for QMK callbacks that can be overridden by modules.
*/

#include "module_dispatch.h"
#include "module_loader.h"

/* Helper typedefs for hook functions */
typedef bool (*combo_should_trigger_fn)(uint16_t, combo_t*, uint16_t, keyrecord_t*);
typedef void (*process_combo_event_fn)(uint16_t, bool);
typedef uint16_t (*get_combo_term_fn)(uint16_t, combo_t*);
typedef bool (*get_combo_bool_fn)(uint16_t, combo_t*);
typedef bool (*process_combo_key_fn)(uint16_t, combo_t*, uint8_t, uint16_t);
typedef uint8_t (*combo_ref_from_layer_fn)(uint8_t);

bool module_dispatch_combo_should_trigger(uint16_t combo_index, combo_t *combo,
                                          uint16_t keycode, keyrecord_t *record) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_COMBO_HOOK_SHOULD_TRIGGER].func != NULL) {
        combo_should_trigger_fn fn = (combo_should_trigger_fn)hooks[MODULE_COMBO_HOOK_SHOULD_TRIGGER].func;
        return fn(combo_index, combo, keycode, record);
    }

    /* Default: all combos trigger */
    return true;
}

void module_dispatch_process_combo_event(uint16_t combo_index, bool pressed) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_COMBO_HOOK_PROCESS_EVENT].func != NULL) {
        process_combo_event_fn fn = (process_combo_event_fn)hooks[MODULE_COMBO_HOOK_PROCESS_EVENT].func;
        fn(combo_index, pressed);
    }
}

uint16_t module_dispatch_get_combo_term(uint16_t index, combo_t *combo) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_COMBO_HOOK_GET_TERM].func != NULL) {
        get_combo_term_fn fn = (get_combo_term_fn)hooks[MODULE_COMBO_HOOK_GET_TERM].func;
        return fn(index, combo);
    }

    /* Default: use COMBO_TERM */
    return COMBO_TERM;
}

bool module_dispatch_get_combo_must_hold(uint16_t index, combo_t *combo) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_GET_MUST_HOLD].func != NULL) {
        get_combo_bool_fn fn = (get_combo_bool_fn)hooks[MODULE_COMBO_HOOK_GET_MUST_HOLD].func;
        return fn(index, combo);
    }
    return false;
}

bool module_dispatch_get_combo_must_tap(uint16_t index, combo_t *combo) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_GET_MUST_TAP].func != NULL) {
        get_combo_bool_fn fn = (get_combo_bool_fn)hooks[MODULE_COMBO_HOOK_GET_MUST_TAP].func;
        return fn(index, combo);
    }
    return false;
}

bool module_dispatch_get_combo_must_press_in_order(uint16_t index, combo_t *combo) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_GET_MUST_PRESS_IN_ORDER].func != NULL) {
        get_combo_bool_fn fn = (get_combo_bool_fn)hooks[MODULE_COMBO_HOOK_GET_MUST_PRESS_IN_ORDER].func;
        return fn(index, combo);
    }
    return true;
}

bool module_dispatch_process_combo_key_release(uint16_t index, combo_t *combo, uint8_t key_index, uint16_t keycode) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_PROCESS_KEY_RELEASE].func != NULL) {
        process_combo_key_fn fn = (process_combo_key_fn)hooks[MODULE_COMBO_HOOK_PROCESS_KEY_RELEASE].func;
        return fn(index, combo, key_index, keycode);
    }
    return false;
}

bool module_dispatch_process_combo_key_repress(uint16_t index, combo_t *combo, uint8_t key_index, uint16_t keycode) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_PROCESS_KEY_REPRESS].func != NULL) {
        process_combo_key_fn fn = (process_combo_key_fn)hooks[MODULE_COMBO_HOOK_PROCESS_KEY_REPRESS].func;
        return fn(index, combo, key_index, keycode);
    }
    return false;
}

uint8_t module_dispatch_combo_ref_from_layer(uint8_t layer) {
    module_hook_entry_t* hooks = module_get_hook_table();
    if (hooks[MODULE_COMBO_HOOK_REF_FROM_LAYER].func != NULL) {
        combo_ref_from_layer_fn fn = (combo_ref_from_layer_fn)hooks[MODULE_COMBO_HOOK_REF_FROM_LAYER].func;
        return fn(layer);
    }
    return layer;
}

#ifdef COMBO_ENABLE
/*
 * Strong overrides of QMK's weak combo callbacks. These are the actual
 * integration points that route core combo processing through the module
 * hook table. Without these, loaded modules would never be invoked.
 *
 * The corresponding feature flags (COMBO_SHOULD_TRIGGER,
 * COMBO_TERM_PER_COMBO, COMBO_MUST_HOLD_PER_COMBO,
 * COMBO_MUST_TAP_PER_COMBO, COMBO_MUST_PRESS_IN_ORDER_PER_COMBO,
 * COMBO_PROCESS_KEY_RELEASE, COMBO_PROCESS_KEY_REPRESS) are enabled in
 * module_loader.mk so that quantum/process_keycode/process_combo.c
 * actually calls these callbacks. combo_ref_from_layer is always called
 * by core and needs no flag.
 */

bool combo_should_trigger(uint16_t combo_index, combo_t *combo,
                          uint16_t keycode, keyrecord_t *record) {
    return module_dispatch_combo_should_trigger(combo_index, combo, keycode, record);
}

uint16_t get_combo_term(uint16_t combo_index, combo_t *combo) {
    return module_dispatch_get_combo_term(combo_index, combo);
}

void process_combo_event(uint16_t combo_index, bool pressed) {
    module_dispatch_process_combo_event(combo_index, pressed);
}

bool get_combo_must_hold(uint16_t combo_index, combo_t *combo) {
    return module_dispatch_get_combo_must_hold(combo_index, combo);
}

bool get_combo_must_tap(uint16_t combo_index, combo_t *combo) {
    return module_dispatch_get_combo_must_tap(combo_index, combo);
}

bool get_combo_must_press_in_order(uint16_t combo_index, combo_t *combo) {
    return module_dispatch_get_combo_must_press_in_order(combo_index, combo);
}

bool process_combo_key_release(uint16_t combo_index, combo_t *combo, uint8_t key_index, uint16_t keycode) {
    return module_dispatch_process_combo_key_release(combo_index, combo, key_index, keycode);
}

bool process_combo_key_repress(uint16_t combo_index, combo_t *combo, uint8_t key_index, uint16_t keycode) {
    return module_dispatch_process_combo_key_repress(combo_index, combo, key_index, keycode);
}

uint8_t combo_ref_from_layer(uint8_t layer) {
    return module_dispatch_combo_ref_from_layer(layer);
}
#endif // COMBO_ENABLE
