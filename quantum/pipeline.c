#include "pipeline.h"
#include <string.h>

#define MAX_MACHINES 16

static sm_machine_t *machines[MAX_MACHINES] = {0};
static int machine_count = 0;

void pipeline_register(sm_machine_t *machine) {
    if (!machine || machine_count >= MAX_MACHINES) return;
    machines[machine_count++] = machine;
    // Insertion sort by phase, then priority
    for (int i = machine_count - 1; i > 0; i--) {
        int cur = (int)machines[i]->phase * 256 + machines[i]->priority;
        int prev = (int)machines[i-1]->phase * 256 + machines[i-1]->priority;
        if (cur >= prev) break;
        sm_machine_t *tmp = machines[i];
        machines[i] = machines[i-1];
        machines[i-1] = tmp;
    }
}

void pipeline_unregister(sm_machine_t *machine) {
    if (!machine) return;
    // Find the entry and compact the array.
    for (int i = 0; i < machine_count; i++) {
        if (machines[i] == machine) {
            for (int j = i; j < machine_count - 1; j++) {
                machines[j] = machines[j + 1];
            }
            machine_count--;
            machines[machine_count] = NULL;
            return;
        }
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
        if (machines[i]->tick) {
            machines[i]->tick(machines[i]->instance);
        }
    }
}

// Returns true if pipeline consumed the event (caller should skip further processing).
bool pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record) {
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_PRE_TAP && machines[i]->handle) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) {
                return true;
            }
        }
    }
    return false;
}

void pipeline_process_post_tap(keyevent_t *event, keyrecord_t *record) {
    // POST_TAP machines (can consume)
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_TAP && machines[i]->handle) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) return;
        }
    }
    // EXECUTE
    process_record_handler(record);
    // POST_EXEC (observe only)
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_EXEC && machines[i]->handle) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}
