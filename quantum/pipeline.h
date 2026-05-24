#pragma once

#include "quantum.h"

typedef enum {
    PHASE_PRE_TAP
    /* PHASE_POST_TAP and PHASE_POST_EXEC are reserved but not yet wired.
       Add them back here when pipeline_process_post_tap() gets a real
       call site. */
} pipeline_phase_t;

typedef enum {
    SM_PASS,
    SM_CONSUME
} sm_result_t;

typedef struct sm_machine sm_machine_t;

struct sm_machine {
    void               *instance;
    sm_result_t         (*handle)(void *self, keyevent_t *event, keyrecord_t *record);
    void                (*tick)(void *self);
    void                (*reset)(void *self);
    const char          *name;
    pipeline_phase_t    phase;
    uint8_t             priority;
};

void pipeline_init(void);
void pipeline_register(sm_machine_t *machine);

/* Remove a previously registered machine. Safe to call with a machine
   pointer that was never registered (no-op). Required by SRAM-loaded
   pipeline modules so unload can detach the module's sm_machine_t
   before its memory is cleared. */
void pipeline_unregister(sm_machine_t *machine);

// Phase-specific entry points (called from action_exec)
// Returns true if event was consumed (caller should skip further processing)
bool pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record);

// Called from keyboard_task() for timer handling
void pipeline_tick(void);
