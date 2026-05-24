#pragma once

#include "quantum.h"

typedef enum {
    KBSM_PHASE_PRE_TAP
    /* KBSM_PHASE_POST_TAP and KBSM_PHASE_POST_EXEC are reserved but not yet wired.
       Add them back here when kbsm_process_post_tap() gets a real
       call site. */
} kbsm_phase_t;

typedef enum {
    KBSM_PASS,
    KBSM_CONSUME
} kbsm_result_t;

typedef struct kbsm kbsm_t;

struct kbsm {
    void               *instance;
    kbsm_result_t       (*handle)(void *self, keyevent_t *event, keyrecord_t *record);
    void                (*tick)(void *self);
    void                (*reset)(void *self);
    const char          *name;
    kbsm_phase_t        phase;
    uint8_t             priority;
};

void kbsm_init(void);
void kbsm_reset(void);
void kbsm_register(kbsm_t *machine);

/* Remove a previously registered machine. Safe to call with a machine
   pointer that was never registered (no-op). Required by SRAM-loaded
   behavior modules so unload can detach the module's kbsm_t
   before its memory is cleared. */
void kbsm_unregister(kbsm_t *machine);

// Phase-specific entry points (called from action_exec)
// Returns true if event was consumed (caller should skip further processing)
bool kbsm_process_pre_tap(keyevent_t *event, keyrecord_t *record);

// Called from keyboard_task() for timer handling
void kbsm_tick(void);
