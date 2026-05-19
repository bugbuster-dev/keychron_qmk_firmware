/*
    Pipeline Environment — implementation.
    See pipeline_env.h for design notes.
*/

#include "pipeline_env.h"
#include "pipeline.h"
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

static pipeline_env_t g_pipeline_env = {
    .pipeline_register   = pipeline_register,
    .pipeline_unregister = pipeline_unregister,
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
};

pipeline_env_t *pipeline_env_get(void) {
    return &g_pipeline_env;
}
