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

/* xprintf is a macro in QMK's print.h that expands to either uprintf or
   nothing depending on the log target. Modules need an actual function
   to call through the env table, so wrap it. */
static int env_xprintf(const char *fmt, ...) {
    /* TODO: forward to uprintf via va_list once that's wired up.
       For now, swallow — modules can return MODULE_INIT_MAGIC etc. to
       confirm execution without relying on log output. */
    (void)fmt;
    return 0;
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
