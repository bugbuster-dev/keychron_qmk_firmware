/* Copyright 2024 ~ 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include "keychron_common.h"

#ifdef MODULE_LOADER_ENABLE
#include "module_dispatch.h"
#endif

enum layers {
    MAC_BASE,
    MAC_FN,
    WIN_BASE,
    WIN_FN,
};

#define FN_MAC MO(MAC_FN)
#define FN_WIN MO(WIN_FN)

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_iso_88(
        KC_ESC,   KC_BRID,  KC_BRIU,  KC_MCTRL, KC_LNPAD, UG_VALD,  UG_VALU,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,  KC_MUTE,  KC_SNAP,  KC_SIRI,  UG_NEXT,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_INS,   KC_HOME,  KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,            KC_DEL,   KC_END,   KC_PGDN,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,  KC_NUHS,  KC_ENT,
        KC_LSFT,  KC_NUBS,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,            KC_UP,
        KC_LCTL,  KC_LOPTN, KC_LCMMD,                               KC_SPC,                                 KC_RCMMD, KC_ROPTN, FN_MAC,   KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT),

    [MAC_FN] = LAYOUT_iso_88(
        _______,  KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   UG_TOGG,  _______,  _______,  UG_TOGG,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        UG_TOGG,  UG_NEXT,  UG_VALU,  UG_HUEU,  UG_SATU,  UG_SPDU,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______,  _______,
        _______,  UG_PREV,  UG_VALD,  UG_HUED,  UG_SATD,  UG_SPDD,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  BAT_LVL,  _______,  _______,  _______,  _______,  _______,            _______,            _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______,  _______),

    [WIN_BASE] = LAYOUT_iso_88(
        KC_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_MUTE,  KC_PSCR,  KC_CTANA, UG_NEXT,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_INS,   KC_HOME,  KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,            KC_DEL,   KC_END,   KC_PGDN,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,  KC_NUHS,  KC_ENT,
        KC_LSFT,  KC_NUBS,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,            KC_UP,
        KC_LCTL,  KC_LWIN,  KC_LALT,                               KC_SPC,                                  KC_RALT,  KC_RWIN,  FN_WIN,   KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT),

    [WIN_FN] = LAYOUT_iso_88(
        _______,  KC_BRID,  KC_BRIU,  KC_TASK,  KC_FILE,  UG_VALD,  UG_VALU,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,  UG_TOGG,  _______,  _______,  UG_TOGG,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        UG_TOGG,  UG_NEXT,  UG_VALU,  UG_HUEU,  UG_SATU,  UG_SPDU,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______,  _______,
        _______,  UG_PREV,  UG_VALD,  UG_HUED,  UG_SATD,  UG_SPDD,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  BAT_LVL,  _______,  _______,  _______,  _______,  _______,            _______,            _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______,  _______)
};

// clang-format on
#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [MAC_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [MAC_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
    [WIN_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [WIN_FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
};
#endif // ENCODER_MAP_ENABLE

#if defined(LEADER_ENABLE) && defined(RGB_MATRIX_ENABLE)
// LED index of the key that triggered leader mode (for visual indicator)
static uint8_t leader_trigger_led = NO_LED;
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
#ifdef MODULE_LOADER_ENABLE
    if (!module_dispatch_process_record(keycode, record)) return false;
#endif
#if defined(LEADER_ENABLE) && defined(RGB_MATRIX_ENABLE)
    // Track key position for leader LED indicator.  Updated on every TD or
    // QK_LEADER press; only used when leader is actually active.
    if (record->event.pressed && (IS_QK_TAP_DANCE(keycode) || keycode == QK_LEADER)) {
        leader_trigger_led = g_led_config.matrix_co[record->event.key.row][record->event.key.col];
    }
#endif
#if defined(COMBO_ENABLE) && defined(LEADER_ENABLE)
    // When a combo outputs QK_LEADER, process_record_quantum re-derives the
    // keycode from position (0,0) so process_leader() never sees QK_LEADER.
    // Intercept it here via record->keycode which preserves the combo output.
    if (record->keycode == QK_LEADER && record->event.pressed) {
        leader_start();
        return false;
    }
#endif
    if (!process_record_keychron_common(keycode, record)) {
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// COMBO
////////////////////////////////////////////////////////////////////////////////
#ifdef COMBO_ENABLE
#    ifdef DYNAMIC_COMBO_ENABLE
#        include "combo_eeprom.h"

// Fixed-size array — QMK's keymap_introspection.c uses sizeof(key_combos)
combo_t key_combos[COMBO_DEF_MAX_SLOTS] = {};

// Default combos loaded to EEPROM on first boot / factory reset
// Trigger pairs chosen for near-zero English bigram frequency to avoid misfires.
const combo_def_t combo_default_defs[] = {
    {.keys = {KC_Z, KC_X, COMBO_END}, .keycode = LCTL(KC_A)}, // Select All
    {.keys = {KC_X, KC_S, COMBO_END}, .keycode = LCTL(KC_C)}, // Copy
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)}, // Paste
    {.keys = {KC_V, KC_F, COMBO_END}, .keycode = LCTL(KC_X)}, // Cut
    {.keys = {KC_X, KC_D, COMBO_END}, .keycode = LCTL(KC_Z)}, // Undo
};
const uint8_t combo_default_count = sizeof(combo_default_defs) / sizeof(combo_def_t);

#    endif // DYNAMIC_COMBO_ENABLE
#endif     // COMBO_ENABLE

////////////////////////////////////////////////////////////////////////////////
// TAP DANCE
////////////////////////////////////////////////////////////////////////////////
#ifdef TAP_DANCE_ENABLE
#    ifdef DYNAMIC_TAP_DANCE_ENABLE
#        include "tap_dance_eeprom.h"
// Fixed-size array — QMK's tap dance uses sizeof(tap_dance_actions)
tap_dance_action_t tap_dance_actions[TAP_DANCE_DEF_MAX_SLOTS];
#    endif
#endif

////////////////////////////////////////////////////////////////////////////////
// LEADER KEY
////////////////////////////////////////////////////////////////////////////////
#ifdef LEADER_ENABLE
#    ifdef DYNAMIC_LEADER_ENABLE
#        include "leader_eeprom.h"
#        include "leader.h"

// Default leader sequences loaded to EEPROM on first boot / factory reset
const leader_def_t leader_default_defs[] = {
    {.sequence = {KC_V, KC_C, KC_NO, KC_NO, KC_NO}, .keycode = MC_0},
    {.sequence = {KC_V, KC_P, KC_NO, KC_NO, KC_NO}, .keycode = MC_1},
};
const uint8_t leader_default_count = sizeof(leader_default_defs) / sizeof(leader_def_t);

// Access QMK leader globals for early termination matching
extern uint16_t leader_sequence[5];
extern uint8_t  leader_sequence_size;

// Flag to prevent double-fire: when post_process_record_user already
// matched and fired the action, leader_end_user must not match again.
static bool leader_already_matched = false;

void leader_end_user(void) {
#        ifdef RGB_MATRIX_ENABLE
    leader_trigger_led = NO_LED;
#        endif
    // Timeout fallback: try one final exact match (skip if already fired)
    if (!leader_already_matched) {
        leader_eeprom_try_match(leader_sequence, leader_sequence_size);
    }
    leader_already_matched = false;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Early termination: after process_leader() adds the key, check for matches
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    // Check for exact match first
    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
        leader_already_matched = true;
        leader_end();
        return;
    }

    // No prefix matches remain -- end early
    if (!leader_eeprom_has_prefix(leader_sequence, leader_sequence_size)) {
        leader_end();
    }
}

#    endif // DYNAMIC_LEADER_ENABLE
#endif     // LEADER_ENABLE

////////////////////////////////////////////////////////////////////////////////
// RGB MATRIX INDICATORS
////////////////////////////////////////////////////////////////////////////////
#if defined(LEADER_ENABLE) && defined(RGB_MATRIX_ENABLE)
bool rgb_matrix_indicators_user(void) {
    // Light the trigger key while a leader sequence is active
    if (leader_sequence_active() && leader_trigger_led != NO_LED) {
        rgb_matrix_set_color(leader_trigger_led, 255, 255, 255);
    }
    return true;
}
#endif
