// Vim Modal Layer for QMK
//
// Demonstrates the pipeline orchestrator + StateSmith integration for a
// feature that GENUINELY benefits from state machine modeling:
// - 5 distinct modes (Normal, Insert, Visual, Command, Replace)
// - Each mode translates keys differently
// - Transitions are non-trivial and enforced by the SM
//
// Activation: register the machine via pipeline_register(vim_modal_machine_get())
//             and bind a key to KC_VIM_TOGGLE (or similar) to enter/exit.
// For demo: starts active on boot in Normal mode.

#include "vim_modal_adapter.h"
#include "pipeline.h"
#include "quantum_keycodes.h"
#include "action_util.h"
#include "VimModal.h"

typedef struct {
    VimModal sm;
    bool enabled;  // master on/off (e.g., toggled by a dedicated key)
} vim_modal_state_t;

static vim_modal_state_t vim_state = {.enabled = true};
static sm_machine_t vim_modal_machine;

// ---------------------------------------------------------------------------
// Normal mode: translate keys to navigation commands
// Returns SM_CONSUME if key was handled internally (state transition or
// translated to a different keycode that was sent directly).
// ---------------------------------------------------------------------------
static sm_result_t handle_normal(vim_modal_state_t *st, uint16_t kc, keyrecord_t *r) {
    if (!r->event.pressed) return SM_CONSUME;  // suppress all releases in normal

    switch (kc) {
        // Mode transitions
        case KC_I:
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_I);
            return SM_CONSUME;
        case KC_A:
            // 'a' = insert after cursor: send Right then enter insert
            tap_code(KC_RIGHT);
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_A);
            return SM_CONSUME;
        case KC_O:
            // 'o' = open line below: End, Enter, then insert mode
            tap_code(KC_END);
            tap_code(KC_ENTER);
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_O);
            return SM_CONSUME;
        case KC_V:
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_V);
            return SM_CONSUME;

        // Navigation: hjkl → arrows
        case KC_H: tap_code(KC_LEFT);  return SM_CONSUME;
        case KC_J: tap_code(KC_DOWN);  return SM_CONSUME;
        case KC_K: tap_code(KC_UP);    return SM_CONSUME;
        case KC_L: tap_code(KC_RIGHT); return SM_CONSUME;

        // Word navigation
        case KC_W: tap_code16(LCTL(KC_RIGHT)); return SM_CONSUME;
        case KC_B: tap_code16(LCTL(KC_LEFT));  return SM_CONSUME;

        // Line navigation
        case KC_0:    tap_code(KC_HOME); return SM_CONSUME;
        case KC_DOT:  tap_code(KC_END);  return SM_CONSUME;  // '$' lives on shifted 4; '.' for demo

        // File navigation
        case KC_G: tap_code16(LCTL(KC_HOME)); return SM_CONSUME;  // top of file

        // Edit commands
        case KC_X:    tap_code(KC_DELETE); return SM_CONSUME;
        case KC_U:    tap_code16(LCTL(KC_Z)); return SM_CONSUME;  // undo
        case KC_R:    tap_code16(LCTL(KC_Y)); return SM_CONSUME;  // redo (vim uses Ctrl+R, we map 'r')
        case KC_P:    tap_code16(LCTL(KC_V)); return SM_CONSUME;  // paste
        case KC_Y:    tap_code16(LCTL(KC_C)); return SM_CONSUME;  // yank (copy selection)

        // Suppress all other keys in normal mode (no garbage to host)
        default:
            return SM_CONSUME;
    }
}

// ---------------------------------------------------------------------------
// Insert mode: pass through everything except Escape
// ---------------------------------------------------------------------------
static sm_result_t handle_insert(vim_modal_state_t *st, uint16_t kc, keyrecord_t *r) {
    if (kc == KC_ESCAPE && r->event.pressed) {
        VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
        return SM_CONSUME;
    }
    return SM_PASS;  // normal typing
}

// ---------------------------------------------------------------------------
// Visual mode: hjkl extend selection (Shift+arrows), y/d operate on selection
// ---------------------------------------------------------------------------
static sm_result_t handle_visual(vim_modal_state_t *st, uint16_t kc, keyrecord_t *r) {
    if (!r->event.pressed) return SM_CONSUME;

    switch (kc) {
        case KC_ESCAPE:
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
            return SM_CONSUME;

        // Extend selection with Shift+arrows
        case KC_H: tap_code16(LSFT(KC_LEFT));  return SM_CONSUME;
        case KC_J: tap_code16(LSFT(KC_DOWN));  return SM_CONSUME;
        case KC_K: tap_code16(LSFT(KC_UP));    return SM_CONSUME;
        case KC_L: tap_code16(LSFT(KC_RIGHT)); return SM_CONSUME;

        case KC_W: tap_code16(LSFT(LCTL(KC_RIGHT))); return SM_CONSUME;
        case KC_B: tap_code16(LSFT(LCTL(KC_LEFT)));  return SM_CONSUME;

        case KC_Y:
            tap_code16(LCTL(KC_C));  // copy selection
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
            return SM_CONSUME;
        case KC_D:
            tap_code16(LCTL(KC_X));  // cut selection
            VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
            return SM_CONSUME;

        default:
            return SM_CONSUME;
    }
}

// ---------------------------------------------------------------------------
// Command mode: stub — would accumulate keystrokes and execute on Enter
// For demo, just Escape returns to normal
// ---------------------------------------------------------------------------
static sm_result_t handle_command(vim_modal_state_t *st, uint16_t kc, keyrecord_t *r) {
    if (!r->event.pressed) return SM_CONSUME;
    if (kc == KC_ESCAPE) {
        VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
    } else if (kc == KC_ENTER) {
        VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ENTER);
    }
    return SM_CONSUME;
}

// ---------------------------------------------------------------------------
// Replace mode: each keystroke replaces a character (Delete + key)
// ---------------------------------------------------------------------------
static sm_result_t handle_replace(vim_modal_state_t *st, uint16_t kc, keyrecord_t *r) {
    if (!r->event.pressed) return SM_CONSUME;
    if (kc == KC_ESCAPE) {
        VimModal_dispatch_event(&st->sm, VimModal_EventId_ON_ESCAPE);
        return SM_CONSUME;
    }
    // Delete current char, then let the key pass through normally
    tap_code(KC_DELETE);
    return SM_PASS;
}

// ---------------------------------------------------------------------------
// Pipeline interface
// ---------------------------------------------------------------------------
static sm_result_t vim_modal_handle(void *self, keyevent_t *event, keyrecord_t *record) {
    vim_modal_state_t *st = self;
    if (!st->enabled) return SM_PASS;

    uint16_t kc = get_record_keycode(record, true);

    // Dispatch based on current SM state — this is where the SM ACTUALLY matters
    switch (st->sm.state_id) {
        case VimModal_StateId_NORMAL:  return handle_normal(st, kc, record);
        case VimModal_StateId_INSERT:  return handle_insert(st, kc, record);
        case VimModal_StateId_VISUAL:  return handle_visual(st, kc, record);
        case VimModal_StateId_COMMAND: return handle_command(st, kc, record);
        case VimModal_StateId_REPLACE: return handle_replace(st, kc, record);
        default: return SM_PASS;
    }
}

static void vim_modal_reset(void *self) {
    vim_modal_state_t *st = self;
    VimModal_ctor(&st->sm);
    VimModal_start(&st->sm);
}

sm_machine_t *vim_modal_machine_get(void) {
    VimModal_ctor(&vim_state.sm);
    VimModal_start(&vim_state.sm);
    vim_modal_machine.instance = &vim_state;
    vim_modal_machine.handle = vim_modal_handle;
    vim_modal_machine.tick = NULL;  // no timer-based logic
    vim_modal_machine.reset = vim_modal_reset;
    vim_modal_machine.name = "vim_modal";
    vim_modal_machine.phase = PHASE_PRE_TAP;
    vim_modal_machine.priority = 50;  // runs before everything else
    return &vim_modal_machine;
}
