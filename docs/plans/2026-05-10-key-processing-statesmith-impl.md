# Key Processing State Machine Refactor — Implementation Plan

> **⚠️ HISTORICAL — DO NOT EXECUTE**
> This plan was executed in full (10 tasks), then **reverted** because the SMs added no value for combo/tap dance/leader.
> See [2026-05-11-key-processing-pipeline-outcome.md](2026-05-11-key-processing-pipeline-outcome.md) for what actually shipped and why.

---

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace inconsistent key processing hooks (combo, tap dance, leader) with a uniform StateSmith-generated state machine pipeline.

**Architecture:** StateSmith generates C state machines from `.sm` diagrams. A thin pipeline dispatcher (`quantum/pipeline.c`) orchestrates machines in phases (PRE_TAP → POST_TAP → POST_EXEC) with sequential dispatch and short-circuit on consume. Each feature gets a `.sm` diagram + ~20 line adapter.

**Tech Stack:** StateSmith CLI (code gen), C (embedded), QMK firmware build system (ChibiOS/Make)

---

### Task 1: Pipeline Infrastructure (Header + Core)

**Files:**
- Create: `quantum/pipeline.h`
- Create: `quantum/pipeline.c`
- Modify: `quantum/keyboard.c:705-799` (add `pipeline_tick()` to `keyboard_task()`)

**Step 1: Write `pipeline.h`**

```c
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

// Main dispatch: replaces pre_process_record_quantum + process_record_quantum + post_process_record_quantum
void pipeline_process(keyevent_t *event, keyrecord_t *record);

// Called from keyboard_task() for timer handling
void pipeline_tick(void);

// Called on layer change, reset, etc.
void pipeline_reset(void);
```

**Step 2: Write `pipeline.c`**

```c
#include "pipeline.h"
#include "str_util.h"
#include <stdio.h>

#define MAX_MACHINES 16

static sm_machine_t *machines[MAX_MACHINES] = {0};
static int machine_count = 0;

void pipeline_register(sm_machine_t *machine) {
    if (machine_count >= MAX_MACHINES) return;
    machines[machine_count++] = machine;
    // Insertion sort by phase, then priority
    for (int i = machine_count - 1; i > 0; i--) {
        int cmp = (machines[i]->phase << 8) + machines[i]->priority -
                  (machines[i-1]->phase << 8) + machines[i-1]->priority;
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

void pipeline_process(keyevent_t *event, keyrecord_t *record) {
    // Phase 1: PRE_TAP — raw events, can consume
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_PRE_TAP) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) return;
        }
    }

    // Tap/hold resolution barrier (existing QMK code)
    // Called from action_exec() — pipeline_process is called around this

    // Phase 2: POST_TAP — resolved keycodes, can consume
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_TAP) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) return;
        }
    }

    // Execute (existing QMK: process_record_handler)
    // Called from action_exec() — pipeline_process is called around this

    // Phase 3: POST_EXEC — observe only
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_EXEC) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}
```

**Step 3: Wire `pipeline_tick()` into `keyboard_task()`**

In `quantum/keyboard.c`, after `leader_task()` call (around line 760), add:
```c
#ifdef KEY_PROCESSING_SM_ENABLE
    pipeline_tick();
#endif
```

**Step 4: Commit**

```bash
git add quantum/pipeline.h quantum/pipeline.c quantum/keyboard.c
git commit -m "feat: add state machine pipeline dispatcher infrastructure"
```

---

### Task 2: Install StateSmith CLI

**Files:**
- Modify: `Makefile` or build rules (for auto-generation)

**Step 1: Install StateSmith CLI**

```bash
# Check if Go is available (StateSmith is a Go CLI tool)
go version

# Install StateSmith
go install github.com/StateSmith/StateSmith/cli@latest

# Verify
statesmith --version
```

**Step 2: If Go is not available, download prebuilt binary**

```bash
# Download from GitHub releases
curl -sL https://github.com/StateSmith/StateSmith/releases/download/cli-v0.21.0-alpha-1/statesmith-linux -o /usr/local/bin/statesmith
chmod +x /usr/local/bin/statesmith
statesmith --version
```

**Step 3: Create features directory structure**

```bash
mkdir -p quantum/features
mkdir -p generated/sm
```

**Step 4: Create `.gitignore` entry for generated files**

Append to `.gitignore`:
```
generated/sm/
```

**Step 5: Commit**

```bash
git add .gitignore
git commit -m "chore: add StateSmith code generation setup"
```

---

### Task 3: Combo State Machine

**Files:**
- Create: `quantum/features/combo.sm`
- Create: `quantum/features/combo_adapter.h`
- Create: `quantum/features/combo_adapter.c`

**Step 1: Write the StateSmith diagram (`combo.sm`)**

Use PlantUML format (StateSmith's primary input):

```plantuml
@startuml
stateMachine name combo_sm

state idle <<initial>>
state tracking
state consumed

' Inputs
input key_press(key: keypos_t)
input key_release(key: keypos_t)
input timer_expire()
input reset()

' Outputs
output matched(combo_keycode: uint16_t)
output none

route idle
  on key_press --> tracking
  on reset --> idle

route tracking
  on key_press --> tracking
  on key_release --> tracking
  on key_release if no_candidates_left --> idle
  on timer_expire if no_match --> idle
  on timer_expire if match_found --> consumed
  on reset --> idle

route consumed
  on key_release --> idle
  on reset --> idle
  entry / emit matched(combo_keycode)
@enduml
```

**Step 2: Write adapter header (`combo_adapter.h`)**

```c
#pragma once
#include "pipeline.h"
#include "process_combo.h"

sm_machine_t *combo_machine_get(void);
```

**Step 3: Write adapter (`combo_adapter.c`)**

```c
#include "combo_adapter.h"
#include "pipeline.h"
#include "timer.h"
#include "keymap_introspection.h"

// Generated SM includes (after statesmith gen)
// #include "generated/sm/combo_sm.h"

typedef struct {
    // combo_sm_t sm;  // generated state machine instance
    keypos_t pressed_keys[COMBO_MAX_KEYS];
    uint8_t pressed_count;
    uint16_t timer;
    bool has_match;
    uint16_t matched_keycode;
} combo_adapter_state_t;

static combo_adapter_state_t combo_state;
static sm_machine_t combo_machine;

static sm_result_t combo_handle(void *self, keyevent_t *event, keyrecord_t *record) {
    combo_adapter_state_t *st = self;

    if (event->pressed) {
        if (st->pressed_count < COMBO_MAX_KEYS) {
            st->pressed_keys[st->pressed_count++] = event->key;
        }
        if (st->timer == 0) st->timer = timer_read();

        // Check all registered combos against pressed keys
        // (existing combo matching logic, extracted from process_combo.c)
        // For now, pass through — matching logic migrates in Task 5
    } else {
        // Remove key from pressed set
        for (uint8_t i = 0; i < st->pressed_count; i++) {
            if (st->pressed_keys[i].row == event->key.row && st->pressed_keys[i].col == event->key.col) {
                st->pressed_keys[i] = st->pressed_keys[--st->pressed_count];
                break;
            }
        }
        if (st->pressed_count == 0) {
            st->timer = 0;
        }
    }
    return SM_PASS;  // placeholder, wired to actual matching in Task 5
}

static void combo_tick(void *self) {
    combo_adapter_state_t *st = self;
    if (st->timer && st->pressed_count > 0) {
        uint16_t longest_term = 200;  // default combo term
        if (timer_elapsed(st->timer) > longest_term) {
            // Timer expired — either match or reset
            st->timer = 0;
            st->pressed_count = 0;
        }
    }
}

static void combo_reset(void *self) {
    combo_adapter_state_t *st = self;
    st->pressed_count = 0;
    st->timer = 0;
    st->has_match = false;
}

sm_machine_t *combo_machine_get(void) {
    combo_machine.instance = &combo_state;
    combo_machine.handle = combo_handle;
    combo_machine.tick = combo_tick;
    combo_machine.reset = combo_reset;
    combo_machine.name = "combo";
    combo_machine.phase = PHASE_PRE_TAP;
    combo_machine.priority = 100;
    return &combo_machine;
}
```

**Step 4: Commit**

```bash
git add quantum/features/combo.sm quantum/features/combo_adapter.h quantum/features/combo_adapter.c
git commit -m "feat: add combo state machine with adapter"
```

---

### Task 4: Tap Dance State Machine

**Files:**
- Create: `quantum/features/tap_dance.sm`
- Create: `quantum/features/tap_dance_adapter.h`
- Create: `quantum/features/tap_dance_adapter.c`

**Step 1: Write diagram (`tap_dance.sm`)**

```plantuml
@startuml
stateMachine name tap_dance_sm

state idle <<initial>>
state counting
state committed

input key_press(td_index: uint8_t)
input key_release(td_index: uint8_t)
input other_key_press()
input timer_expire()
input reset()

output tap_action(count: uint8_t, td_index: uint8_t)
output hold_action(td_index: uint8_t)
output none

route idle
  on key_press --> counting

route counting
  on key_press if within_tapping_term --> counting
  on key_release if within_tapping_term --> counting
  on timer_expire --> idle / emit tap_action(count, td_index)
  on other_key_press --> idle / emit hold_action(td_index)
  on reset --> idle

route committed
  on key_release --> idle
  on reset --> idle
@enduml
```

**Step 2: Write adapter header (`tap_dance_adapter.h`)**

```c
#pragma once
#include "pipeline.h"

sm_machine_t *tap_dance_machine_get(void);
```

**Step 3: Write adapter (`tap_dance_adapter.c`)**

```c
#include "tap_dance_adapter.h"
#include "pipeline.h"
#include "timer.h"
#include "action_util.h"
#include "action_layer.h"

#define MAX_TAP_DANCE 16

typedef struct {
    uint8_t count;
    uint8_t td_index;
    uint16_t last_tap_time;
    bool active;
    bool committed;
} tap_dance_adapter_state_t;

static tap_dance_adapter_state_t td_state;
static sm_machine_t tap_dance_machine;

static sm_result_t tap_dance_handle(void *self, keyevent_t *event, keyrecord_t *record) {
    tap_dance_adapter_state_t *st = self;
    uint16_t keycode = get_record_keycode(record, true);

    if (keycode >= QK_TAP_DANCE && keycode <= QK_TAP_DANCE_MAX) {
        uint8_t idx = QK_TAP_DANCE_GET_INDEX(keycode);

        if (event->pressed) {
            if (!st->active || st->td_index != idx) {
                st->active = true;
                st->td_index = idx;
                st->count = 1;
                st->committed = false;
                st->last_tap_time = timer_read();
            } else if (timer_elapsed(st->last_tap_time) < GET_TAPPING_TERM(keycode, record)) {
                st->count++;
                st->last_tap_time = timer_read();
                st->committed = false;
            } else {
                // Expired, commit previous, start new
                // tap_action(st->count, st->td_index);  // execute
                st->count = 1;
                st->last_tap_time = timer_read();
            }
            return SM_CONSUME;  // hold for tapping term
        } else {
            // Release — if committed, let it through
            if (st->committed) {
                st->active = false;
                return SM_PASS;
            }
            return SM_CONSUME;
        }
    }

    if (st->active && !event->key.row == 0) {
        // Other key pressed during tap dance — commit as hold
        // hold_action(st->td_index);
        st->active = false;
    }
    return SM_PASS;
}

static void tap_dance_tick(void *self) {
    tap_dance_adapter_state_t *st = self;
    if (st->active && !st->committed) {
        uint16_t term = 200;  // TAPPING_TERM
        if (timer_elapsed(st->last_tap_time) > term) {
            // Tapping term expired — commit action
            st->committed = true;
            // tap_action(st->count, st->td_index);  // execute registered action
        }
    }
}

static void tap_dance_reset(void *self) {
    tap_dance_adapter_state_t *st = self;
    st->active = false;
    st->count = 0;
    st->committed = false;
}

sm_machine_t *tap_dance_machine_get(void) {
    tap_dance_machine.instance = &td_state;
    tap_dance_machine.handle = tap_dance_handle;
    tap_dance_machine.tick = tap_dance_tick;
    tap_dance_machine.reset = tap_dance_reset;
    tap_dance_machine.name = "tap_dance";
    tap_dance_machine.phase = PHASE_POST_TAP;
    tap_dance_machine.priority = 200;
    return &tap_dance_machine;
}
```

**Step 4: Commit**

```bash
git add quantum/features/tap_dance.sm quantum/features/tap_dance_adapter.h quantum/features/tap_dance_adapter.c
git commit -m "feat: add tap dance state machine with adapter"
```

---

### Task 5: Leader State Machine

**Files:**
- Create: `quantum/features/leader.sm`
- Create: `quantum/features/leader_adapter.h`
- Create: `quantum/features/leader_adapter.c`

**Step 1: Write diagram (`leader.sm`)**

```plantuml
@startuml
stateMachine name leader_sm

state idle <<initial>>
state active
state matched

input leader_key_press()
input key_press(keycode: uint16_t)
input timer_expire()
input reset()

output sequence_matched(sequence: uint16_t[], len: uint8_t)
output none

route idle
  on leader_key_press --> active
  on reset --> idle

route active
  on key_press if sequence_matched --> matched / emit sequence_matched
  on key_press if no_prefix_match --> idle
  on key_press --> active
  on timer_expire --> idle
  on reset --> idle

route matched
  on key_press --> idle
  on reset --> idle
@enduml
```

**Step 2: Write adapter header (`leader_adapter.h`)**

```c
#pragma once
#include "pipeline.h"

sm_machine_t *leader_machine_get(void);
```

**Step 3: Write adapter (`leader_adapter.c`)**

```c
#include "leader_adapter.h"
#include "pipeline.h"
#include "timer.h"
#include "quantum_keycodes.h"
#include <string.h>

#define LEADER_SEQ_MAX 5
#ifndef LEADER_TIMEOUT
#define LEADER_TIMEOUT 300
#endif

typedef struct {
    bool active;
    uint16_t sequence[LEADER_SEQ_MAX];
    uint8_t seq_len;
    uint16_t timer;
} leader_adapter_state_t;

static leader_adapter_state_t leader_state;
static sm_machine_t leader_machine;

// Hook: called after each key added, return true to execute and end
typedef bool (*leader_match_fn)(uint16_t *seq, uint8_t len);
static leader_match_fn custom_match = NULL;

void leader_set_match_fn(leader_match_fn fn) { custom_match = fn; }

static sm_result_t leader_handle(void *self, keyevent_t *event, keyrecord_t *record) {
    leader_adapter_state_t *st = self;
    uint16_t keycode = get_record_keycode(record, true);

    if (!event->pressed) return SM_PASS;

    if (!st->active && keycode == QK_LEADER) {
        st->active = true;
        st->seq_len = 0;
        st->timer = timer_read();
        memset(st->sequence, 0, sizeof(st->sequence));
        return SM_CONSUME;  // consume the leader key itself
    }

    if (st->active) {
        if (st->seq_len < LEADER_SEQ_MAX) {
            st->sequence[st->seq_len++] = keycode;
            st->timer = timer_read();

            if (custom_match && custom_match(st->sequence, st->seq_len)) {
                st->active = false;
                st->seq_len = 0;
                return SM_CONSUME;
            }
        } else {
            st->active = false;
        }
        return SM_CONSUME;
    }

    return SM_PASS;
}

static void leader_tick(void *self) {
    leader_adapter_state_t *st = self;
    if (st->active && st->seq_len > 0) {
        if (timer_elapsed(st->timer) > LEADER_TIMEOUT) {
            st->active = false;
            st->seq_len = 0;
        }
    }
}

static void leader_reset(void *self) {
    leader_adapter_state_t *st = self;
    st->active = false;
    st->seq_len = 0;
    memset(st->sequence, 0, sizeof(st->sequence));
}

sm_machine_t *leader_machine_get(void) {
    leader_machine.instance = &leader_state;
    leader_machine.handle = leader_handle;
    leader_machine.tick = leader_tick;
    leader_machine.reset = leader_reset;
    leader_machine.name = "leader";
    leader_machine.phase = PHASE_POST_TAP;
    leader_machine.priority = 300;
    return &leader_machine;
}
```

**Step 4: Commit**

```bash
git add quantum/features/leader.sm quantum/features/leader_adapter.h quantum/features/leader_adapter.c
git commit -m "feat: add leader state machine with adapter"
```

---

### Task 6: Wire Pipeline Into QMK Action Execution

**Files:**
- Modify: `quantum/action.c:81-149` (`action_exec()`)
- Modify: `quantum/quantum.c:285-298` (`pre_process_record_quantum`, `post_process_record_quantum`)
- Modify: `quantum/rules.mk`

**Step 1: Add build flag to `quantum/rules.mk`**

```makefile
# State machine key processing pipeline
ifdef KEY_PROCESSING_SM_ENABLE
    SRC_C += quantum/pipeline.c
    SRC_C += quantum/features/combo_adapter.c
    SRC_C += quantum/features/tap_dance_adapter.c
    SRC_C += quantum/features/leader_adapter.c
    NO_COMBO_ENABLE = yes
    NO_TAP_DANCE_ENABLE = yes
    NO_LEADER_ENABLE = yes
endif
```

**Step 2: Replace `action_exec()` processing in `quantum/action.c`**

At the top of `action_exec()` (after line 110 where `record` is created), replace the existing `pre_process_record_quantum` + `action_tapping_process` flow:

```c
#ifdef KEY_PROCESSING_SM_ENABLE
    // New SM pipeline handles pre-tap phase
    pipeline_process_pre_tap(&event, &record);
#endif

#ifndef NO_ACTION_TAPPING
    action_tapping_process(record);
#endif

#ifdef KEY_PROCESSING_SM_ENABLE
    // New SM pipeline handles post-tap + execute + post-exec
    pipeline_process_post_tap(&event, &record);
#else
    // Existing code path
    if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
        process_record(&record);
    }
#endif
```

**Step 3: Split `pipeline_process()` into phase-specific calls**

Update `pipeline.c` to expose phase-specific entry points:
```c
void pipeline_process_pre_tap(keyevent_t *event, keyrecord_t *record) {
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_PRE_TAP) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}

void pipeline_process_post_tap(keyevent_t *event, keyrecord_t *record) {
    // POST_TAP machines
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_TAP) {
            if (machines[i]->handle(machines[i]->instance, event, record) == SM_CONSUME) return;
        }
    }
    // EXECUTE
    process_record_handler(record);
    // POST_EXEC
    for (int i = 0; i < machine_count; i++) {
        if (machines[i]->phase == PHASE_POST_EXEC) {
            machines[i]->handle(machines[i]->instance, event, record);
        }
    }
}
```

**Step 4: Register machines at init**

In `quantum/quantum.c` `keyboard_post_init()`:
```c
#ifdef KEY_PROCESSING_SM_ENABLE
    pipeline_init();
    pipeline_register(combo_machine_get());
    pipeline_register(tap_dance_machine_get());
    pipeline_register(leader_machine_get());
#endif
```

**Step 5: Commit**

```bash
git add quantum/action.c quantum/quantum.c quantum/rules.mk quantum/pipeline.c quantum/pipeline.h
git commit -m "feat: wire SM pipeline into QMK action execution"
```

---

### Task 7: Build Rules for StateSmith Generation

**Files:**
- Modify: `Makefile` (top-level, or `rules.mk`)
- Create: `quantum/features/Makefile` (or integrate into existing build)

**Step 1: Add generation rule**

In the top-level `Makefile` or a dedicated `quantum/rules.mk`:

```makefile
# StateSmith code generation
STATESMITH ?= statesmith
SM_SOURCES := $(wildcard quantum/features/*.sm)
SM_GENERATED := $(patsubst quantum/features/%.sm,generated/sm/%.c,$(SM_SOURCES))
SM_GENERATED_H := $(patsubst quantum/features/%.sm,generated/sm/%.h,$(SM_SOURCES))

generated/sm:
    mkdir -p generated/sm

generated/sm/%.c generated/sm/%.h: quantum/features/%.sm | generated/sm
    $(STATESMITH) gen --lang c $< --output-dir generated/sm

ifneq ($(SM_SOURCES),)
$(OBJECTive): $(SM_GENERATED) $(SM_GENERATED_H)
endif
```

**Step 2: Commit**

```bash
git add Makefile
git commit -m "build: add StateSmith code generation rules"
```

---

### Task 8: Integration Test — Build Q3 Max With Pipeline

**Files:**
- Modify: `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/rules.mk`

**Step 1: Enable the pipeline in keychron keymap**

Append to `keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/rules.mk`:
```makefile
KEY_PROCESSING_SM_ENABLE = yes
```

**Step 2: Attempt build**

```bash
make keychron/q3_max/ansi_encoder:keychron
```

**Step 3: Fix any compilation errors**

Expected issues:
- Missing includes in adapters
- Type mismatches between `keyevent_t` and SM inputs
- Unresolved symbols from old combo/tap dance/leader code

**Step 4: Commit**

```bash
git add -A
git commit -m "test: verify Q3 Max builds with SM pipeline enabled"
```

---

### Task 9: Migrate Combo Matching Logic Fully

**Files:**
- Modify: `quantum/features/combo_adapter.c`
- Reference: `quantum/process_keycode/process_combo.c` (extract matching logic)

**Step 1: Extract combo matching from `process_combo.c`**

The core matching logic (`process_single_combo`, `apply_combos`, `release_combo`) needs to be called from the combo adapter's `handle` and `tick` functions. Either:
- Keep `process_combo.c` as a library and call it from the adapter, OR
- Inline the matching logic into the adapter

**Step 2: Wire combo output to key injection**

When a combo matches, the adapter should inject the combo keycode into the record:
```c
if (st->has_match) {
    record->keycode = st->matched_keycode;
    record->event.pressed = true;
    st->has_match = false;
    st->pressed_count = 0;
    st->timer = 0;
    return SM_CONSUME;
}
```

**Step 3: Commit**

```bash
git add quantum/features/combo_adapter.c
git commit -m "feat: migrate combo matching logic to SM adapter"
```

---

### Task 10: Clean Up Old Code

**Files:**
- Delete: `quantum/process_keycode/process_combo.c` (or mark as deprecated)
- Delete: `quantum/process_keycode/process_tap_dance.c` (or mark as deprecated)
- Delete: `quantum/process_keycode/process_leader.c`
- Delete: `quantum/leader.c`
- Modify: All `#include "process_combo.h"` etc.
- Modify: `quantum/quantum.c` — remove old `#ifdef COMBO_ENABLE`, `#ifdef TAP_DANCE_ENABLE`, `#ifdef LEADER_ENABLE` blocks

**Step 1: Remove old includes from `quantum.c`**

Remove from `process_record_quantum()`:
- `process_combo()` call (line ~288)
- `preprocess_tap_dance()` / `process_tap_dance()` calls (lines ~322, ~398)
- `process_leader()` call (line ~404)

**Step 2: Remove old task functions from `keyboard.c`**

Remove:
- `combo_task()` call
- `tap_dance_task()` call
- `leader_task()` call

Replace with single `pipeline_tick()`.

**Step 3: Commit**

```bash
git rm quantum/process_keycode/process_combo.c quantum/process_keycode/process_tap_dance.c quantum/process_keycode/process_leader.c quantum/leader.c 2>/dev/null || true
git add quantum/quantum.c quantum/keyboard.c
git commit -m "refactor: remove old key processing code, fully migrate to SM pipeline"
```

---

## Summary

| Task | Files | Estimated Time |
|------|-------|----------------|
| 1. Pipeline infra | `pipeline.h/c`, `keyboard.c` | 30 min |
| 2. Install StateSmith | build setup | 15 min |
| 3. Combo SM | `combo.sm`, adapter | 45 min |
| 4. Tap Dance SM | `tap_dance.sm`, adapter | 45 min |
| 5. Leader SM | `leader.sm`, adapter | 30 min |
| 6. Wire into QMK | `action.c`, `quantum.c`, `rules.mk` | 45 min |
| 7. Build rules | `Makefile` | 15 min |
| 8. Integration test | build + fix | 60 min |
| 9. Full combo migration | `combo_adapter.c` | 60 min |
| 10. Clean up old code | delete + remove includes | 30 min |
| **Total** | | **~5.5 hours** |
