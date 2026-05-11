// quantum/features/sticky_combo_adapter.c
#include "sticky_combo_adapter.h"
#include "pipeline.h"
#include "timer.h"
#include "quantum_keycodes.h"
#include "action.h"
#include "StickyCombo.h"

#ifndef STICKY_COMBO_WINDOW_MS
#define STICKY_COMBO_WINDOW_MS 50
#endif

typedef struct {
    StickyCombo sm;
    int8_t  active_combo;         // index into sticky_combos[], -1 if none
    bool    key1_held;            // physical state of active combo's key1
    bool    key2_held;            // physical state of active combo's key2

    // Pending first-press for simultaneous-press detection
    uint16_t pending_keycode;
    int8_t   pending_combo;       // which combo's key was first pressed; -1 if none
    bool     pending_is_key1;     // true if pending is key1 of pending_combo
    uint16_t pending_time;
} sticky_combo_state_t;

static sticky_combo_state_t sc_state = {.active_combo = -1, .pending_combo = -1};
static sm_machine_t sticky_combo_machine;

// Lookup: does this keycode appear as key1 or key2 in any defined combo?
static int8_t find_combo_for_key(uint16_t kc, bool *is_key1, bool *is_key2) {
    for (uint8_t i = 0; i < sticky_combo_count; i++) {
        if (sticky_combos[i].key1 == kc) { *is_key1 = true;  *is_key2 = false; return i; }
        if (sticky_combos[i].key2 == kc) { *is_key1 = false; *is_key2 = true;  return i; }
    }
    return -1;
}

static sm_result_t sticky_combo_handle(void *self, keyevent_t *event, keyrecord_t *record) {
    sticky_combo_state_t *st = self;
    uint16_t kc = get_record_keycode(record, true);

    // ---------------- IDLE ----------------
    if (st->sm.state_id == StickyCombo_StateId_IDLE) {
        if (!event->pressed) return SM_PASS;  // releases pass through

        bool is_key1 = false, is_key2 = false;
        int8_t combo = find_combo_for_key(kc, &is_key1, &is_key2);
        if (combo < 0) return SM_PASS;  // not a combo key

        // Check if this completes a simultaneous press with pending
        if (st->pending_combo == combo &&
            timer_elapsed(st->pending_time) <= STICKY_COMBO_WINDOW_MS &&
            ((st->pending_is_key1 && is_key2) || (!st->pending_is_key1 && is_key1))) {
            // Simultaneous press detected
            st->active_combo = combo;
            st->key1_held = true;
            st->key2_held = true;
            st->pending_combo = -1;

            uint16_t action = sticky_combos[combo].combo_action;
            if (action != KC_NO) {
                tap_code16(action);
            }
            StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_COMBO_PRESS);
            return SM_CONSUME;
        }

        // Not simultaneous; remember this as new pending
        st->pending_combo = combo;
        st->pending_keycode = kc;
        st->pending_is_key1 = is_key1;
        st->pending_time = timer_read();
        return SM_PASS;  // let normal processing happen
    }

    // ---------------- ARMED_BOTH ----------------
    if (st->sm.state_id == StickyCombo_StateId_ARMED_BOTH) {
        if (st->active_combo < 0) return SM_PASS;

        uint16_t key1 = sticky_combos[st->active_combo].key1;
        uint16_t key2 = sticky_combos[st->active_combo].key2;

        if (kc != key1 && kc != key2) return SM_PASS;  // third key

        if (event->pressed) {
            return SM_CONSUME;  // re-press, ignore
        }

        // Release of an active combo key
        if (kc == key1) {
            st->key1_held = false;
            if (st->key2_held) {
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_KEY1);
            } else {
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_BOTH);
                st->active_combo = -1;
            }
        } else {
            st->key2_held = false;
            if (st->key1_held) {
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_KEY2);
            } else {
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_BOTH);
                st->active_combo = -1;
            }
        }
        return SM_CONSUME;
    }

    // ---------------- ARMED_FOR_KEY1 ----------------
    // key2 is held; tapping key1 fires tap_action_1; releasing key2 exits.
    if (st->sm.state_id == StickyCombo_StateId_ARMED_FOR_KEY1) {
        if (st->active_combo < 0) return SM_PASS;
        uint16_t key1 = sticky_combos[st->active_combo].key1;
        uint16_t key2 = sticky_combos[st->active_combo].key2;

        if (kc == key1) {
            if (event->pressed) {
                uint16_t action = sticky_combos[st->active_combo].tap_action_1;
                if (action != KC_NO) tap_code16(action);
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_TAP_KEY1);
            }
            return SM_CONSUME;
        }

        if (kc == key2 && !event->pressed) {
            st->key2_held = false;
            StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_KEY2);
            st->active_combo = -1;
            return SM_CONSUME;
        }

        return SM_PASS;  // third key passes through
    }

    // ---------------- ARMED_FOR_KEY2 ----------------
    if (st->sm.state_id == StickyCombo_StateId_ARMED_FOR_KEY2) {
        if (st->active_combo < 0) return SM_PASS;
        uint16_t key1 = sticky_combos[st->active_combo].key1;
        uint16_t key2 = sticky_combos[st->active_combo].key2;

        if (kc == key2) {
            if (event->pressed) {
                uint16_t action = sticky_combos[st->active_combo].tap_action_2;
                if (action != KC_NO) tap_code16(action);
                StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_TAP_KEY2);
            }
            return SM_CONSUME;
        }

        if (kc == key1 && !event->pressed) {
            st->key1_held = false;
            StickyCombo_dispatch_event(&st->sm, StickyCombo_EventId_ON_RELEASE_KEY1);
            st->active_combo = -1;
            return SM_CONSUME;
        }

        return SM_PASS;
    }

    return SM_PASS;
}

static void sticky_combo_tick(void *self) {
    sticky_combo_state_t *st = self;
    // Clear stale pending press
    if (st->pending_combo >= 0 &&
        timer_elapsed(st->pending_time) > STICKY_COMBO_WINDOW_MS) {
        st->pending_combo = -1;
    }
}

static void sticky_combo_reset(void *self) {
    sticky_combo_state_t *st = self;
    StickyCombo_ctor(&st->sm);
    StickyCombo_start(&st->sm);
    st->active_combo = -1;
    st->pending_combo = -1;
    st->key1_held = false;
    st->key2_held = false;
}

sm_machine_t *sticky_combo_machine_get(void) {
    StickyCombo_ctor(&sc_state.sm);
    StickyCombo_start(&sc_state.sm);
    sticky_combo_machine.instance = &sc_state;
    sticky_combo_machine.handle = sticky_combo_handle;
    sticky_combo_machine.tick = sticky_combo_tick;
    sticky_combo_machine.reset = sticky_combo_reset;
    sticky_combo_machine.name = "sticky_combo";
    sticky_combo_machine.phase = PHASE_PRE_TAP;
    sticky_combo_machine.priority = 40;  // before vim_modal at 50
    return &sticky_combo_machine;
}
