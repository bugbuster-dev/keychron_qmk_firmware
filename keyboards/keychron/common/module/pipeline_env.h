/*
    Pipeline Environment — callback table for SRAM-loaded pipeline modules.

    A module loaded into SRAM (or flash) cannot link directly against
    firmware symbols — the host has no way to resolve BL relocations into
    arbitrary firmware addresses. The pattern (modelled on
    keyboards/keychron/q3_max/dynld_func.h for RGB animations) is:

      1. Firmware exports a single struct of function pointers
         (g_pipeline_env, defined in pipeline_env.c).
      2. Module init() receives the env pointer as its argument.
      3. Module stores the env in its module-local state and routes all
         firmware calls (tap_code16, pipeline_register, etc.) through it.

    Adding a new callable to this struct does NOT require a header-version
    bump — old modules that never reference the new field continue to
    work. Removing or reordering callables DOES require a bump.
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"
#include "pipeline.h"

typedef struct pipeline_env {
    /* Pipeline registration. unregister() is needed for SRAM modules so
       that unloading cleans up the machine pointer; the registered
       sm_machine_t lives in module memory and becomes invalid after
       module_sram_clear(). */
    void     (*pipeline_register)(sm_machine_t *machine);
    void     (*pipeline_unregister)(sm_machine_t *machine);

    /* Key actions — wrappers for QMK's register/unregister/tap families.
       Modules must NOT call register_code16 directly; the symbol may not
       be resolvable from the module's load address. */
    void     (*tap_code16)(uint16_t kc);
    void     (*register_code16)(uint16_t kc);
    void     (*unregister_code16)(uint16_t kc);
    void     (*tap_code)(uint8_t kc);
    void     (*register_code)(uint8_t kc);
    void     (*unregister_code)(uint8_t kc);

    /* Timing — QMK's timer_read returns ms since boot wrapped to 16 bits;
       timer_elapsed returns ms since `since`. */
    uint16_t (*timer_read)(void);
    uint16_t (*timer_elapsed)(uint16_t since);

    /* Keycode resolution. Modules typically call this on the record they
       were handed to convert event.key into the keycode for the current
       layer. */
    uint16_t (*get_record_keycode)(keyrecord_t *r, bool update_layer_cache);

    /* Diagnostic. */
    int      (*xprintf)(const char *fmt, ...);

  /* Reserved for future expansion. Cast to whatever callback table
        (e.g. dynld_math_funcs_t) the module needs but firmware hasn't
        baked into this struct yet. NULL when unused. */
    void     *extension;

    /* Module load base address. Set by the loader before calling init()
        so the module can rebase its own internal pointers (compiled at
        ORIGIN=0) to the actual runtime address. */
    uintptr_t module_base;
} pipeline_env_t;

/* Firmware accessor — the single env instance populated with the live
   function pointers. Returned by reference so it can be passed to
   module init functions without copying. */
pipeline_env_t *pipeline_env_get(void);
