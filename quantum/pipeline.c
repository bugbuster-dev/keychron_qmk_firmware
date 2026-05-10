#include "pipeline.h"
#include <string.h>

#define MAX_MACHINES 16

static sm_machine_t *machines[MAX_MACHINES] = {0};
static int machine_count = 0;

void pipeline_register(sm_machine_t *machine) {
    if (machine_count >= MAX_MACHINES) return;
    machines[machine_count++] = machine;
    // Insertion sort by phase, then priority
    for (int i = machine_count - 1; i > 0; i--) {
        int cmp = ((machines[i]->phase << 8) + machines[i]->priority) -
                  ((machines[i-1]->phase << 8) + machines[i-1]->priority);
        if (cmp >= 0) break;
        sm_machine_t *tmp = machines[i];
        machines[i] = machines[i-1];
        machines[i-1] = tmp;
    }
}

void pipeline_init(void) {
    machine_count = 0;
    memset(machines, 0, sizeof(machines));
}

void pipeline_reset(void) {
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->reset) machines[i]->reset(machines[i]->instance);
    }
}

void pipeline_tick(void) {
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->tick) machines[i]->tick(machines[i]->instance);
    }
}

void pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record) {
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_PRE_TAP) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}

void pipeline_process_post_tap(keyevent_t *event, keyrecord_t *record) {
    // POST_TAP machines (can consume)
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_TAP) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) return;
        }
    }
    // EXECUTE
    process_record_handler(record);
    // POST_EXEC (observe only)
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_EXEC) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}
