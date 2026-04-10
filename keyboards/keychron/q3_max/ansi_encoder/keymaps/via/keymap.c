/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include "keychron_common.h"

#ifdef TAP_DANCE_ENABLE
// Tap Dance declarations
enum {
    TD_ESC,
};

#    define KC_TD_ESC TD(TD_ESC)
#else
#    define KC_TD_ESC KC_ESC
#endif

enum layers {
    MAC_BASE,
    MAC_FN,
    WIN_BASE,
    WIN_FN,
};

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_tkl_ansi(
        KC_ESC,   KC_BRID,  KC_BRIU,  KC_MCTRL, KC_LNPAD, RGB_VAD,  RGB_VAI,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,    KC_MUTE,    KC_SNAP,  KC_SIRI,  RGB_MOD,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,     KC_BSPC,    KC_INS,   KC_HOME,  KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,    KC_BSLS,    KC_DEL,   KC_END,   KC_PGDN,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,              KC_ENT,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,              KC_RSFT,              KC_UP,
        KC_LCTL,  KC_LOPTN, KC_LCMMD,                               KC_SPC,                                 KC_RCMMD, KC_ROPTN, MO(MAC_FN), KC_RCTL,    KC_LEFT,  KC_DOWN,  KC_RGHT),

    [MAC_FN] = LAYOUT_tkl_ansi(
        _______,  KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,     RGB_TOG,    _______,  _______,  RGB_TOG,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,
        RGB_TOG,  RGB_MOD,  RGB_VAI,  RGB_HUI,  RGB_SAI,  RGB_SPI,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,
        _______,  RGB_RMOD, RGB_VAD,  RGB_HUD,  RGB_SAD,  RGB_SPD,  _______,  _______,  _______,  _______,  _______,  _______,              _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,              _______,              _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,    _______,    _______,  _______,  _______),

    [WIN_BASE] = LAYOUT_tkl_ansi(
        KC_TD_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,     KC_MUTE,    KC_PSCR,  KC_CTANA, RGB_MOD,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,     KC_BSPC,    KC_INS,   KC_HOME,  KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,    KC_BSLS,    KC_DEL,   KC_END,   KC_PGDN,
        KC_LCTL,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,              KC_ENT,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,              KC_RSFT,              KC_UP,
        KC_LCTL,  KC_LCMD,  KC_LALT,                                KC_SPC,                                 KC_RALT,  KC_RWIN,  MO(WIN_FN), KC_RCTL,    KC_LEFT,  KC_DOWN,  KC_RGHT),

    [WIN_FN] = LAYOUT_tkl_ansi(
        _______,  KC_BRID,  KC_BRIU,  KC_TASK,  KC_FILE,  RGB_VAD,  RGB_VAI,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,    RGB_TOG,    KC_7,        KC_8,     KC_9,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    KC_4,        KC_5,     KC_6,
        RGB_TOG,  RGB_MOD,  RGB_VAI,  RGB_HUI,  RGB_SAI,  RGB_SPI,  _______,  _______,  _______,  _______,  _______,  _______,  _______,       KC_0,    KC_1,        KC_2,     KC_3,
        _______,  RGB_RMOD, RGB_VAD,  RGB_HUD,  RGB_SAD,  RGB_SPD,  _______,  _______,  _______,  _______,  _______,  _______,              _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,              _______,              _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,    _______,    _______,  _______,  _______),
};

// clang-format on
#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [MAC_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [MAC_FN]   = {ENCODER_CCW_CW(RGB_VAD, RGB_VAI)},
    [WIN_BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [WIN_FN]   = {ENCODER_CCW_CW(RGB_VAD, RGB_VAI)},
};
#endif // ENCODER_MAP_ENABLE

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
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
const combo_def_t combo_default_defs[] = {
    {.keys = {KC_C, KC_A, COMBO_END}, .keycode = LCTL(KC_A)},
    {.keys = {KC_C, KC_D, COMBO_END}, .keycode = LCTL(KC_C)},
    {.keys = {KC_C, KC_V, COMBO_END}, .keycode = LCTL(KC_V)},
};
const uint8_t combo_default_count = sizeof(combo_default_defs) / sizeof(combo_def_t);

#    else
// Static combos (no EEPROM persistence)
const uint16_t PROGMEM combo1_keys[] = {KC_A, KC_B, COMBO_END};
const uint16_t PROGMEM combo2_keys[] = {KC_C, KC_D, COMBO_END};
combo_t                key_combos[]  = {
    COMBO(combo1_keys, KC_ESC),
    COMBO(combo2_keys, LCTL(KC_Z)),
};
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
#    else
// Static tap dance definitions (non-dynamic build)
tap_dance_action_t tap_dance_actions[] = {
    [TD_ESC] = ACTION_TAP_DANCE_DOUBLE(KC_ESC, LCTL(LALT(KC_HOME))),
};
#    endif

#    ifdef TAPPING_TERM_PER_KEY
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    return TAPPING_TERM;
}
#    endif

#endif

////////////////////////////////////////////////////////////////////////////////
// LEADER KEY
////////////////////////////////////////////////////////////////////////////////
#ifdef LEADER_ENABLE
#    ifdef DYNAMIC_LEADER_ENABLE
#        include "leader_eeprom.h"
#        include "leader.h"

// Access QMK leader globals for early termination matching
extern uint16_t leader_sequence[5];
extern uint8_t  leader_sequence_size;

void leader_end_user(void) {
    // Timeout fallback: try one final exact match
    leader_eeprom_try_match(leader_sequence, leader_sequence_size);
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Early termination: after process_leader() adds the key, check for matches
    if (!leader_sequence_active()) return;
    if (!record->event.pressed) return;

    // Check for exact match first
    if (leader_eeprom_try_match(leader_sequence, leader_sequence_size)) {
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
