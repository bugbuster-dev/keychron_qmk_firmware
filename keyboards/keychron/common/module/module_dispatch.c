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

bool module_dispatch_combo_should_trigger(uint16_t combo_index, combo_t *combo,
                                          uint16_t keycode, keyrecord_t *record) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_HOOK_COMBO_SHOULD_TRIGGER].func != NULL) {
        combo_should_trigger_fn fn = (combo_should_trigger_fn)hooks[MODULE_HOOK_COMBO_SHOULD_TRIGGER].func;
        return fn(combo_index, combo, keycode, record);
    }

    /* Default: all combos trigger */
    return true;
}

void module_dispatch_process_combo_event(uint16_t combo_index, bool pressed) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_HOOK_PROCESS_COMBO_EVENT].func != NULL) {
        process_combo_event_fn fn = (process_combo_event_fn)hooks[MODULE_HOOK_PROCESS_COMBO_EVENT].func;
        fn(combo_index, pressed);
    }
}

uint16_t module_dispatch_get_combo_term(uint16_t index, combo_t *combo) {
    module_hook_entry_t* hooks = module_get_hook_table();

    if (hooks[MODULE_HOOK_GET_COMBO_TERM].func != NULL) {
        get_combo_term_fn fn = (get_combo_term_fn)hooks[MODULE_HOOK_GET_COMBO_TERM].func;
        return fn(index, combo);
    }

    /* Default: use COMBO_TERM */
    return COMBO_TERM;
}
