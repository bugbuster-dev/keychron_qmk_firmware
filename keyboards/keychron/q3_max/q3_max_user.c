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

#ifdef MODULE_SRAM_ENABLE
#    include "module_sram.h"
#    include "module_loader.h"
#endif

/* Emulator dprintf via USART2 (0x40004404).
 * USART2 CR1 pre-enabled by .resc. Renode accepts DR writes unconditionally.
 * QMKata's sendchar() tees here, so xprintf/dprintf reach Renode analyzer.
 * Under EMULATOR_BUILD: real functions writing to USART2.
 * Otherwise: static inline no-ops eliminated by the compiler. */
#ifdef EMULATOR_BUILD
void dbg_putc(char c) {
    *(volatile uint32_t *)0x40004404 = c;
}
void dbg_print(const char *s) { while (*s) dbg_putc(*s++); }
void dbg_hex8(uint8_t v) {
    static const char h[] = "0123456789abcdef";
    dbg_putc(h[(v >> 4) & 0xf]);
    dbg_putc(h[v & 0xf]);
}
#else
static inline void dbg_putc(char c) { (void)c; }
static inline void dbg_print(const char *s) { (void)s; }
static inline void dbg_hex8(uint8_t v) { (void)v; }
#endif

#ifdef MODULE_SRAM_ENABLE
/* Emulator runtime command byte for module load/unload from the Renode
 * monitor. Host writes via `sysbus WriteByte <addr> <cmd>`. Edge-triggered
 * in emu_module_poll() below.
 *   1 = load slot 8 (bytes must already be staged at g_emu_module_stage)
 *   2 = unload slot 8
 * Other values ignored. Declared volatile + used so the optimizer can't
 * elide it. */
volatile uint8_t g_emu_module_cmd __attribute__((used)) = 0;

/* Staging buffer for module bytes uploaded by the host via Renode's
 * `sysbus LoadBinary @<bin> <g_emu_module_stage>`. Lives in .bss so it
 * gets zero-initialised after boot but the host stages *after* boot,
 * so .bss zeroing doesn't clobber the staged bytes. We deliberately do
 * NOT stage into g_module_sram directly because the loader's
 * module_sram_clear() memsets the slot to 0xFF before memcpy, which
 * would wipe a stage-in-place source. */
uint8_t g_emu_module_stage[0x1000] __attribute__((used, aligned(4)));
volatile uint16_t g_emu_module_stage_len __attribute__((used)) = 0;
#endif

#ifdef MODULE_SRAM_ENABLE
/* Emulator: poll the runtime command byte each scan. Edge-detected so it
 * fires once per write. To re-trigger the same command, write 0 then the
 * command again:
 *   sysbus WriteByte <addr> 0
 *   sysbus WriteByte <addr> 1
 *
 * housekeeping_task_kb and housekeeping_task_user are both already claimed
 * (by keychron_task.c and module_dispatch.c respectively), so we piggyback
 * on matrix_scan_kb below — it's already overridden in this file. */
static void emu_module_poll(void) {
    static uint8_t last_cmd = 0;
    uint8_t cmd = g_emu_module_cmd;
    if (cmd == last_cmd) return;
    last_cmd = cmd;
    if (cmd == 1) {
        uint16_t len = g_emu_module_stage_len;
        dbg_print("\r\nemu: load slot 8 len=");
        dbg_hex8((len >> 8) & 0xff);
        dbg_hex8(len & 0xff);
        if (len == 0) {
            dbg_print(" (no staged bytes; write g_emu_module_stage_len first)");
            return;
        }
        bool ok = module_load(MODULE_SRAM_SLOT_BASE_ID,
                              g_emu_module_stage,
                              len);
        dbg_print(ok ? " OK" : " FAIL");
    } else if (cmd == 2) {
        dbg_print("\r\nemu: unload slot 8... ");
        bool ok = module_unload(MODULE_SRAM_SLOT_BASE_ID);
        dbg_print(ok ? "OK" : "FAIL");
    }
}
#endif

/* Log cooked matrix[3] to UART only when it changes (emulator only) */
void matrix_scan_kb(void) {
#ifdef EMULATOR_BUILD
    extern matrix_row_t matrix[MATRIX_ROWS];
    static uint32_t prev = 0;
    uint32_t v = matrix[3];
    if (v != prev) {
        prev = v;
        dbg_print("\r\nMAT ");
        dbg_hex8(v & 0xff); dbg_putc(' ');
        dbg_hex8((v >> 8) & 0xff); dbg_putc(' ');
        dbg_hex8((v >> 16) & 0xff); dbg_putc(' ');
        dbg_hex8((v >> 24) & 0xff);
    }
#endif
#ifdef MODULE_SRAM_ENABLE
    emu_module_poll();
#endif
}

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

#ifdef MODULE_SRAM_ENABLE
    /* Emulator note: SRAM module loading is driven by the runtime command
     * byte g_emu_module_cmd (polled from matrix_scan_kb). The host stages
     * the module bytes into g_emu_module_stage (a separate buffer that
     * the loader's module_sram_clear() does NOT touch), sets
     * g_emu_module_stage_len, then writes 1 to g_emu_module_cmd. */
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
