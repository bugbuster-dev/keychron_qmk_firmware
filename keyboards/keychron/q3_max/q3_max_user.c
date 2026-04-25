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
#include "dynamic_keymap.h"
#include "keychron_task.h"

// Default VIA macros — 16 slots, each NUL-terminated.
// Edit strings below. Add key actions with: 0x01, 0xHH, 0xLL (TAP keycode).
static const uint8_t default_via_macros[] = {
    '"', '+', 'y', 0x00,   // Macro 0: gvim copy ("+y)
    '"', '+', 'p', 0x00,   // Macro 1: gvim paste ("+p)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Macros 2-8
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Macros 9-15
};
#ifdef QMKATA_ENABLE
#    include "qmkata/QMKata.h"
#    include "debug_user.h"
#endif

void keyboard_post_init_user(void) {
    // Write default VIA macros on fresh flash (empty EEPROM)
    {
        uint8_t check = 0xFF;
        dynamic_keymap_macro_get_buffer(0, 1, &check);
        if (check == 0) {
            dynamic_keymap_macro_set_buffer(0, sizeof(default_via_macros),
                                            (uint8_t*)default_via_macros);
        }
    }

    // Leader EEPROM init
#if defined(DYNAMIC_LEADER_ENABLE) && defined(LEADER_ENABLE)
    extern void leader_eeprom_init(void);
    leader_eeprom_init();
#endif
    // Tap dance EEPROM init — order independent with combo
#if defined(DYNAMIC_TAP_DANCE_ENABLE) && defined(TAP_DANCE_ENABLE)
    extern void tap_dance_eeprom_init(void);
    tap_dance_eeprom_init();
#endif
#if defined(DYNAMIC_COMBO_ENABLE) && defined(COMBO_ENABLE)
    extern void combo_eeprom_init(void);
    combo_eeprom_init();
#endif

    // Safe mode: if the DELETE key is held at boot, skip module activation
    // to allow recovery from a buggy module that would otherwise crash the
    // firmware or hijack input before the user can unload it.
#if defined(MODULE_LOADER_ENABLE)
    // At keyboard_post_init_user() time matrix_init() has run but matrix_scan()
    // has not, so matrix[] is still zero. Drive a few scans through the
    // default debounce window so a held key is registered before we read it.
    // DEBOUNCE defaults to 5 ms; 10 scans at ~2 ms spacing covers it with
    // margin on both typical (5 ms) and aggressive (1-2 ms) overrides.
    for (uint8_t i = 0; i < 10; i++) {
        matrix_scan();
        wait_ms(2);
    }

    bool safe_mode = false;
    for (uint8_t row = 0; row < MATRIX_ROWS && !safe_mode; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if (!matrix_is_on(row, col)) continue;
            keypos_t pos     = {.row = row, .col = col};
            uint16_t keycode = keymap_key_to_keycode(0, pos);
            if (keycode == KC_DEL) {
                safe_mode = true;
                break;
            }
        }
    }

    if (safe_mode) {
        // Safe mode: skip module activation
        dprintf("SAFE MODE: Skipping module activation\n");
    } else {
        // Normal mode: scan and activate modules
        extern void module_boot_scan(void);
        module_boot_scan();
    }
#endif

#ifdef QMKATA_ENABLE
#    ifdef DEVEL_BUILD
    debug_config.enable = 1;
    debug_config_user.qmkata = 0;
#    endif
    qmkata_init("Keychron QMKata");
#endif
}

#ifdef QMKATA_ENABLE
#    ifdef DEVEL_BUILD
typedef struct stats_time {
    uint32_t counter;
    uint32_t print_interval;
    uint32_t start_time;
    uint32_t max_time;
    uint32_t min_time;
    uint32_t total_time;
} stats_time_t;

static stats_time_t stats_rgb_render;
static stats_time_t stats_qmkata_task;

static inline void _stats_print(stats_time_t *stats, const char *name) {
    if (debug_config_user.stats == 0) return;
    DBG_USR(stats, "%s:%ldx,%ldms,%ld/%ld\n", name, stats->counter, stats->total_time, stats->max_time, stats->min_time);
}

static inline void _stats_start(stats_time_t *stats, uint32_t print_interval) {
    if (debug_config_user.stats == 0) return;
    stats->start_time     = timer_read32();
    stats->print_interval = print_interval;
    if (stats->counter == 0) {
        stats->max_time = 0;
        stats->min_time = 0xFFFFFFFF;
    }
}

static inline void _stats_stop(stats_time_t *stats, const char *name) {
    if (debug_config_user.stats == 0) return;
    uint32_t elapsed = timer_elapsed32(stats->start_time);
    if (elapsed > stats->max_time) stats->max_time = elapsed;
    if (elapsed < stats->min_time) stats->min_time = elapsed;
    stats->total_time += elapsed;
    stats->counter++;
    if (stats->counter % stats->print_interval == 0) {
        _stats_print(stats, name);
        stats->counter    = 0;
        stats->total_time = 0;
    }
}

#        define STATS_START(stats, interval) _stats_start(stats, interval)
#        define STATS_STOP(stats, name) _stats_stop(stats, name)
#    else
#        define STATS_START(stats, interval)
#        define STATS_STOP(stats, name)
#    endif

extern rgb_matrix_host_buffer_t g_rgb_matrix_host_buf;

// render rgb matrix "host buffer" set by user from host
void rgb_matrix_host_buf_render(void) {
    if (!g_rgb_matrix_host_buf.written) return;
    STATS_START(&stats_rgb_render, 1000);
    bool matrix_set = 0;
    for (uint8_t li = 0; li < RGB_MATRIX_LED_COUNT; li++) {
        if (g_rgb_matrix_host_buf.led[li].duration > 0) {
            rgb_matrix_set_color(li, g_rgb_matrix_host_buf.led[li].r, g_rgb_matrix_host_buf.led[li].g, g_rgb_matrix_host_buf.led[li].b);
            g_rgb_matrix_host_buf.led[li].duration--;
            matrix_set = 1;
        }
    }
    if (!matrix_set) g_rgb_matrix_host_buf.written = 0;
    STATS_STOP(&stats_rgb_render, "rgb buf render");
}
#endif

// user override of mac/win mode and keyboard mac/win switch state
static int s_keyb_user_macwin_mode   = -1; // -1=use switch, 'm'=mac, 'w'=windows
static int s_keyb_switch_macwin_mode = -1; // 'm' or 'w'

void keyb_user_set_macwin_mode(int mode) {
    s_keyb_user_macwin_mode = mode;
    if (mode < 0) {
        mode = s_keyb_switch_macwin_mode;
    }
    int layer = 0;
    if (mode == 'm') layer = 0;
    if (mode == 'w') layer = 2;
    default_layer_set(1UL << layer);
}

int keyb_user_get_macwin_mode(void) {
    if (s_keyb_user_macwin_mode < 0) return s_keyb_switch_macwin_mode;
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
#ifdef QMKATA_ENABLE
    STATS_START(&stats_qmkata_task, 10000);
    qmkata_task();
    STATS_STOP(&stats_qmkata_task, "qmkata task");
#endif
}
