// keyboards/keychron/common/tap_dance/tap_dance_eeprom.c
#include <string.h>
#include "tap_dance_eeprom.h"
#include "eeconfig_kb.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "process_tap_dance.h"
#include "quantum.h"

#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)

#    define TD_EEPROM_MAGIC 0xAD
#    define TD_EEPROM_MAGIC_ADDR ((uint8_t *)EECONFIG_BASE_TAP_DANCE)
#    define TD_EEPROM_DATA_ADDR ((uint8_t *)EECONFIG_BASE_TAP_DANCE + 1)

// RAM mirror -- tap_dance_actions[i].user_data points into this array
static tap_dance_def_t td_defs[TAP_DANCE_DEF_MAX_SLOTS];

// Tracks the last keycode sent by _td_finished for each slot (needed by _td_reset)
static uint16_t td_last_kc[TAP_DANCE_DEF_MAX_SLOTS];

// Provided by keymap.c
extern tap_dance_action_t tap_dance_actions[];

// ---------------------------------------------------------------------------
// Shared callbacks (used by all 8 slots via user_data)
// ---------------------------------------------------------------------------

static void _td_finished(tap_dance_state_t *state, void *user_data) {
    tap_dance_def_t *def  = (tap_dance_def_t *)user_data;
    uint8_t          slot = (uint8_t)(def - td_defs);
    uint16_t         kc   = KC_NO;

    if (state->pressed && def->hold) {
        kc = def->hold;
    } else {
        switch (state->count) {
            case 1:
                kc = def->kc1;
                break;
            case 2:
                kc = def->kc2 ? def->kc2 : def->kc1;
                break;
            default:
                if (def->kc3)
                    kc = def->kc3;
                else if (def->kc2)
                    kc = def->kc2;
                else
                    kc = def->kc1;
                break;
        }
    }

    td_last_kc[slot] = kc;
    if (kc) {
#    ifdef LEADER_ENABLE
        // QK_LEADER is a quantum keycode handled by process_leader() in the
        // processing chain -- register_code16() would truncate it to uint8_t
        // and send a garbage HID code.  Activate leader mode directly instead.
        if (kc == QK_LEADER) {
            leader_start();
            return;
        }
#    endif
        register_code16(kc);
    }
}

static void _td_reset(tap_dance_state_t *state, void *user_data) {
    (void)state;
    tap_dance_def_t *def  = (tap_dance_def_t *)user_data;
    uint8_t          slot = (uint8_t)(def - td_defs);
    if (td_last_kc[slot]) {
#    ifdef LEADER_ENABLE
        // QK_LEADER was handled via leader_start(), nothing to unregister
        if (td_last_kc[slot] != QK_LEADER)
#    endif
            unregister_code16(td_last_kc[slot]);
        td_last_kc[slot] = KC_NO;
    }
}

// ---------------------------------------------------------------------------
// Populate tap_dance_actions[] from td_defs[]
// ---------------------------------------------------------------------------

static void tap_dance_eeprom_apply(void) {
    for (uint8_t i = 0; i < TAP_DANCE_DEF_MAX_SLOTS; i++) {
        // Unregister any currently held key before reassigning
        if (td_last_kc[i]) {
            unregister_code16(td_last_kc[i]);
        }
        tap_dance_actions[i].fn.on_each_tap       = NULL;
        tap_dance_actions[i].fn.on_dance_finished = _td_finished;
        tap_dance_actions[i].fn.on_reset          = _td_reset;
        tap_dance_actions[i].fn.on_each_release   = NULL;
        tap_dance_actions[i].user_data            = &td_defs[i];
        td_last_kc[i]                             = KC_NO;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void tap_dance_eeprom_save(void) {
    eeprom_update_byte(TD_EEPROM_MAGIC_ADDR, TD_EEPROM_MAGIC);
    eeprom_update_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
}

void tap_dance_eeprom_init(void) {
    uint8_t magic = eeprom_read_byte(TD_EEPROM_MAGIC_ADDR);
    if (magic == TD_EEPROM_MAGIC) {
        eeprom_read_block(td_defs, TD_EEPROM_DATA_ADDR, sizeof(td_defs));
    } else {
        tap_dance_eeprom_reset_defaults();
        return; // reset_defaults calls apply
    }
    tap_dance_eeprom_apply();
}

void tap_dance_eeprom_set(uint8_t slot, const tap_dance_def_t *def) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS || !def) return;
    td_defs[slot] = *def;
    tap_dance_eeprom_apply();
    tap_dance_eeprom_save();
}

void tap_dance_eeprom_get(uint8_t slot, tap_dance_def_t *out) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS || !out) return;
    *out = td_defs[slot];
}

void tap_dance_eeprom_clear(uint8_t slot) {
    if (slot >= TAP_DANCE_DEF_MAX_SLOTS) return;
    memset(&td_defs[slot], 0, sizeof(tap_dance_def_t));
    tap_dance_eeprom_apply();
    tap_dance_eeprom_save();
}

void tap_dance_eeprom_reset_defaults(void) {
    memset(td_defs, 0, sizeof(td_defs));
    memset(td_last_kc, 0, sizeof(td_last_kc));
    // Seed slots with the default tap dance definitions from keymap.c
    // TD0: tap x1 = ESC, tap x2 = Ctrl+Alt+Home, hold = Leader
    td_defs[0].kc1 = KC_ESC;
    td_defs[0].kc2 = LCTL(LALT(KC_HOME));
    td_defs[0].hold = QK_LEADER;
    tap_dance_eeprom_apply();
    tap_dance_eeprom_save();
}

#endif // DYNAMIC_TAP_DANCE_ENABLE && TAP_DANCE_ENABLE
