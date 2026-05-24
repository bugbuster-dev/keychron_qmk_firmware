/*
    KB SM Environment — implementation.
    See kbsm_env.h for design notes.
*/

#include "kbsm_env.h"
#include "kbsm.h"
#include "timer.h"
#include "action.h"
#include "quantum.h"
#include "print.h"
#include <stdarg.h>

/* Emulator: xprintf is routed to USART2 via QMKata sendchar tee (dbg_putc).
 * Forward module trace calls through the same path. */
#include <stdio.h>  /* vsnprintf */
static int env_xprintf(const char *fmt, ...) {
    va_list ap;
    char buf[128];
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0 && ((size_t)n < sizeof(buf))) {
        xprintf("%s", buf);
    } else if (n > 0) {
        /* Truncated: print what fits */
        xprintf("%s", buf);
    }
    return n;
}

static kbsm_env_t g_kbsm_env = {
    .kbsm_register   = kbsm_register,
    .kbsm_unregister = kbsm_unregister,
    .tap_code16          = tap_code16,
    .register_code16     = register_code16,
    .unregister_code16   = unregister_code16,
    .tap_code            = tap_code,
    .register_code       = register_code,
    .unregister_code     = unregister_code,
    .timer_read          = timer_read,
    .timer_elapsed       = timer_elapsed,
    .get_record_keycode  = get_record_keycode,
    .xprintf             = env_xprintf,
    .extension           = NULL,
    .module_base         = 0,
};

kbsm_env_t *kbsm_env_get(void) {
    return &g_kbsm_env;
}
