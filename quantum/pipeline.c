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
    extern void debug_led_on(int led, uint8_t r, uint8_t g, uint8_t b);
    /* Light LEDs encoding machine_count + first machine validity */
    debug_led_on(40, machine_count > 0 ? 255 : 0, 0, 0);  // red if any machine
    if (machine_count > 0 && machines[0]) {
        debug_led_on(41, machines[0]->tick ? 255 : 0, 0, 0);  // red if tick set
        debug_led_on(42, machines[0]->handle ? 255 : 0, 0, 0);
        debug_led_on(43, machines[0]->instance ? 255 : 0, 0, 0);
    }
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->tick) {
            debug_led_on(30, 255, 255, 255);  // LED 30 = about to call tick
            machines[i]->tick(machines[i]->instance);
            debug_led_on(31, 255, 255, 255);  // LED 31 = tick returned
        }
    }
}

// Returns true if pipeline consumed the event (caller should skip further processing).
bool pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record) {
    extern void debug_led_on(int led, uint8_t r, uint8_t g, uint8_t b);
    debug_led_on(20, 255, 255, 255);  // LED 20 = pipeline_process_pre_tap entered
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_PRE_TAP && machines[i]->handle) {
            debug_led_on(21, 255, 255, 255);  // LED 21 = found PRE_TAP machine
            debug_led_on(22, 255, 255, 255);  // LED 22 = about to call handle()
            sm_result_t r = machines[i]->handle(machines[i]->instance, event, record);
            debug_led_on(23, 255, 255, 255);  // LED 23 = handle() returned
            if (r == SM_CONSUME) {
                debug_led_on(24, 255, 255, 255);  // LED 24 = consumed
                return true;
            }
        }
    }
    debug_led_on(25, 255, 255, 255);  // LED 25 = pipeline returned (no consume)
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
