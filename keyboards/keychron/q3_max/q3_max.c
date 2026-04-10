/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software : you can redistribute it and /or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see < http://www.gnu.org/licenses/>.
 */

#include "quantum.h"
#include "keychron_task.h"
#include "keychron_common.h"

#ifdef FACTORY_TEST_ENABLE
#    include "factory_test.h"
#endif
#ifdef LK_WIRELESS_ENABLE
#    include "lkbt51.h"
#    include "wireless.h"
#    include "keychron_wireless_common.h"
#    include "battery.h"
#endif

#define POWER_ON_LED_DURATION 3000
static uint32_t power_on_indicator_timer;

#ifdef DIP_SWITCH_ENABLE
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (dip_switch_update_user(index, active)) return true;

    if (index == 0) {
        default_layer_set(1UL << (active ? 2 : 0));
    }
    return true;
}
#endif

void keyboard_post_init_kb(void) {
    keychron_common_init();

    power_on_indicator_timer = timer_read32();
    keyboard_post_init_user();
}

extern void keychron_task_user(void);

bool keychron_task_kb(void) {
    if (power_on_indicator_timer) {
        if (timer_elapsed32(power_on_indicator_timer) > POWER_ON_LED_DURATION) {
            power_on_indicator_timer = 0;
#ifdef LK_WIRELESS_ENABLE
            writePin(BAT_LOW_LED_PIN, !BAT_LOW_LED_PIN_ON_STATE);
#endif

        } else {
#ifdef LK_WIRELESS_ENABLE
            writePin(BAT_LOW_LED_PIN, BAT_LOW_LED_PIN_ON_STATE);
#endif
        }
    }

    keychron_task_user();
    return true;
}

#ifdef LK_WIRELESS_ENABLE
bool lpm_is_kb_idle(void) {
    return power_on_indicator_timer == 0
#    ifdef FACTORY_TEST_ENABLE
           && !factory_reset_indicating()
#    endif
        ;
}
#endif

/* Override register_code16/unregister_code16 to send each modifier key as a
 * separate HID report instead of batching all modifier bits in one call.
 *
 * QMK default: do_code16() calls register_weak_mods(all_mods_at_once), which
 * sets all modifier bits in a single HID report. This causes RDP and some
 * remote desktop clients to miss shortcut detection (e.g. Ctrl+Alt+Home for
 * the RDP connection bar) because they expect modifiers to arrive sequentially,
 * as physical keypresses would produce.
 *
 * For non-QK_MODS keycodes the original QMK behaviour is preserved exactly.
 */
void register_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        bool right = !!(code & QK_RMODS_MIN);
        if (code & QK_LCTL) register_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
        if (code & QK_LSFT) register_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LALT) register_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LGUI) register_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        uint8_t basic = code & 0xFF;
        if (basic) register_code(basic);
    } else {
        /* Original QMK path: do_code16 returns extract_mod_bits() which is 0
         * for non-QK_MODS codes, so the register_mods/register_weak_mods call
         * is a no-op and register_code() does the real work. */
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) {
            register_mods(0);
        } else {
            register_weak_mods(0);
        }
        register_code(code);
    }
}

void unregister_code16(uint16_t code) {
    if (IS_QK_MODS(code)) {
        uint8_t basic = code & 0xFF;
        bool    right = !!(code & QK_RMODS_MIN);
        if (basic) unregister_code(basic);
        if (code & QK_LGUI) unregister_code(right ? KC_RIGHT_GUI : KC_LEFT_GUI);
        if (code & QK_LALT) unregister_code(right ? KC_RIGHT_ALT : KC_LEFT_ALT);
        if (code & QK_LSFT) unregister_code(right ? KC_RIGHT_SHIFT : KC_LEFT_SHIFT);
        if (code & QK_LCTL) unregister_code(right ? KC_RIGHT_CTRL : KC_LEFT_CTRL);
    } else {
        unregister_code(code);
        if (IS_MODIFIER_KEYCODE(code) || code == KC_NO) {
            unregister_mods(0);
        } else {
            unregister_weak_mods(0);
        }
    }
}
