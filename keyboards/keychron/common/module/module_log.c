#include "module_log.h"

#include <stdarg.h>

#ifdef CONSOLE_ENABLE
#include "debug_user.h"
#include "printf.h"  /* lib/printf — exposes vprintf (aliased from vprintf_) */

/* QMK globally defines PRINTF_ALIAS_STANDARD_FUNCTION_NAMES=1 (see
   quantum/logging/print.mk), so lib/printf's vprintf_ is exposed under
   the standard name vprintf. Call the aliased name directly; the macro
   that would rewrite vprintf_ -> vprintf is #undef'd at the bottom of
   printf.h, so writing vprintf_ here would be an undeclared identifier. */

void __attribute__((used)) mprintf(const char *fmt, ...) {
    if (!debug_config_user.module) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#else
void __attribute__((used)) mprintf(const char *fmt, ...) {
    (void)fmt;
}
#endif
