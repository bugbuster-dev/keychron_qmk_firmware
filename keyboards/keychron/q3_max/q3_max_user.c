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
#ifdef FIRMATA_ENABLE
#include "firmata/Firmata_QMK.h"
#endif

void keyboard_post_init_user(void) {
#ifdef FIRMATA_ENABLE
    debug_config.enable = 1;
    firmata_initialize("Keychron Firmata");
#endif
}

#ifdef FIRMATA_ENABLE
extern rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

// render rgb matrix "host buffer" set by user from host
void rgb_matrix_host_buf_render(void)
{
    if (!g_rgb_matrix_host_buf.written) return;
    bool matrix_set = 0;
    for (uint8_t li = 0; li < RGB_MATRIX_LED_COUNT; li++) {
        if (g_rgb_matrix_host_buf.led[li].duration > 0) {
            rgb_matrix_set_color(li, g_rgb_matrix_host_buf.led[li].r, g_rgb_matrix_host_buf.led[li].g, g_rgb_matrix_host_buf.led[li].b);
            g_rgb_matrix_host_buf.led[li].duration--;
            matrix_set = 1;
        }
    }
    if (!matrix_set) g_rgb_matrix_host_buf.written = 0;
}
#endif

// user override of mac/win mode and keyboard mac/win switch state
static int s_keyb_user_macwin_mode = -1;    // -1=use switch, 'm'=mac, 'w'=windows
static int s_keyb_switch_macwin_mode = -1;  // 'm' or 'w'

void keyb_user_set_macwin_mode(int mode) {
    s_keyb_user_macwin_mode = mode;
    if (mode < 0) {
        mode = s_keyb_switch_macwin_mode;
    }
    int layer = 0;
    if (mode == 'm')
        layer = 0;
    if (mode == 'w')
        layer = 2;
    default_layer_set(1UL << layer);
}

int keyb_user_get_macwin_mode(void) {
    if (s_keyb_user_macwin_mode < 0)
        return s_keyb_switch_macwin_mode;
    return s_keyb_user_macwin_mode;
}

bool dip_switch_update_user(uint8_t index, bool active) {
    if (index == 0) {
        if (active)
            s_keyb_switch_macwin_mode = 'w';
        else
            s_keyb_switch_macwin_mode = 'm';

        // ignore win/mac switch when overrided by user
        if (s_keyb_user_macwin_mode != -1) return true;
    }
    return false;
}

void keychron_task_user(void) {
#ifdef FIRMATA_ENABLE
    firmata_task();
#endif
}
