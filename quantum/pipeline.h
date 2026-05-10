#pragma once

#include "quantum.h"

typedef enum {
    PHASE_PRE_TAP,
    PHASE_POST_TAP,
    PHASE_POST_EXEC
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

// Phase-specific entry points (called from action_exec)
void pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record);
void pipeline_process_post_tap(keyevent_t *event, keyrecord_t *record);

// Called from keyboard_task() for timer handling
void pipeline_tick(void);

// Called on layer change, reset, etc.
void pipeline_reset(void);
