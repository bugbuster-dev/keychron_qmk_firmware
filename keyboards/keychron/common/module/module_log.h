/*
   Module logging — mprintf and friends.

   mprintf(fmt, ...) is a printf-compatible function intended for use
   from loadable modules. It is gated by debug_config_user.module:
   when the bit is 0 (default) the call is a no-op; when 1 it forwards
   to vprintf_ (the lib/printf variadic entry point), which routes to
   the same console as xprintf.

   Modules call mprintf for routine diagnostics. Output is automatically
   prefixed with "[mod] " so module console output is distinguishable
   from firmware core output. Modules may still call printf for
   unconditional output (e.g. unrecoverable error paths where the user
   must see something regardless of the bit setting).

   The function exists as a real symbol with a stable address even
   when CONSOLE_ENABLE=no; in that build it is a body-less no-op.
   This guarantees the host-side .map-based PROVIDE symbol resolver
   in qmk-tools/qmk/QMKata/ModuleBuild.py can always link modules
   that reference mprintf, regardless of firmware build options.
*/

#pragma once

void mprintf(const char *fmt, ...);
