# Key Processing — State Machine Refactor Design

> Branch: `refactor/key-processing-sm`
> Framework: [StateSmith](https://github.com/StateSmith/StateSmith) (Apache-2.0)
> Scope: Pipeline + internals, breaking changes OK

---

## Problem

Current key processing hooks are inconsistent:

| Feature | Pipeline Position | API Pattern | Extension |
|---------|-------------------|-------------|-----------|
| Combo | `pre_process_record_quantum()` | `process_combo()` | `process_combo_event()` weak fn |
| Tap Dance | `preprocess_` + `process_` (two places) | Two functions | Callbacks |
| Leader | `process_leader()` + `post_process_record_user()` | Split core/user | `leader_add_user()` weak fn |

Three features, three pipeline positions, three different APIs, three different extension patterns. Adding a fourth feature requires reverse-engineering which pattern to follow.

## Solution

Model every key processing feature as a **StateSmith-generated state machine** behind a **uniform C interface**, dispatched by a **thin pipeline orchestrator** with explicit phase barriers.

---

## Architecture

### Phase Structure

```
EVENT IN
    │
    ├─► [PHASE_PRE_TAP]      Raw matrix events, can consume
    │     combo, module hooks, early intercepts
    │
    ├─► ═══ TAP/HOLD RESOLUTION BARRIER ═══
    │
    ├─► [PHASE_POST_TAP]     Resolved keycodes, can consume
    │     tap_dance, leader, oneshot, main processing
    │
    ├─► [EXECUTE]            register/unregister → HID report
    │
    └─► [PHASE_POST_EXEC]    Observe only, cannot consume
          analytics, logging
```

### State Machine Interface

```c
typedef enum { SM_PASS, SM_CONSUME } sm_result_t;

typedef struct {
    void            *instance;
    sm_result_t     (*handle)(void *self, keyevent_t *event, keyrecord_t *record);
    void            (*tick)(void *self);
    void            (*reset)(void *self);
    const char      *name;
    uint8_t         priority;
    pipeline_phase_t phase;
} sm_machine_t;
```

Every feature produces the same struct. StateSmith generates the state logic; a ~20 line adapter bridges QMK events to SM inputs.

### Pipeline Dispatcher

```c
typedef enum { PHASE_PRE_TAP, PHASE_POST_TAP, PHASE_POST_EXEC } pipeline_phase_t;

void pipeline_init(void);
void pipeline_process(keyevent_t *event, keyrecord_t *record);
void pipeline_tick(void);
void pipeline_reset(void);
void pipeline_register(sm_machine_t *machine);
```

Sequential dispatch within each phase. Short-circuits on first `SM_CONSUME`. Post-exec phase ignores result (observe-only).

---

## Feature State Machines

### Combo
```
idle → tracking : key press
tracking → tracking : additional key press (expand candidates)
tracking → consumed : match found (emit combo keycode)
tracking → idle : timer expire / key release breaks candidate
```

### Tap Dance
```
idle → counting : key press
counting → counting : key press within tapping_term (increment)
counting → idle : timer expire (commit action for count)
counting → idle : hold detected (execute hold action)
```

### Leader
```
idle → active : leader key pressed
active → active : key pressed (append to sequence)
active → idle : sequence matched (execute)
active → idle : timer expire / no prefix match (terminate)
```

---

## Adding a New Feature

```
1. Draw diagram:        features/my_feature.sm
2. Generate:            statesmith gen --lang c features/my_feature.sm
3. Write adapter:       features/my_feature_adapter.c  (~20 lines)
4. Register:            pipeline_register(&my_feature_sm)  (1 line)
```

---

## Build Integration

```makefile
STATESMITH_SOURCES = $(wildcard features/*.sm)
GENERATED_C = $(patsubst features/%.sm,generated/sm/%.c,$(STATESMITH_SOURCES))
GENERATED_H = $(patsubst features/%.sm,generated/sm/%.h,$(STATESMITH_SOURCES))

generated/sm/%.c generated/sm/%.h: features/%.sm | generated/sm/
    statesmith gen --lang c $< --output-dir generated/sm/

SRC_C += $(GENERATED_C)
```

---

## Files

| File | Purpose |
|------|---------|
| `quantum/pipeline.h/c` | Pipeline dispatcher (handwritten) |
| `quantum/sm_machine.h` | Uniform SM interface |
| `features/combo.sm` | Combo state diagram |
| `features/combo_adapter.c` | Combo → SM bridge |
| `features/tap_dance.sm` | Tap dance state diagram |
| `features/tap_dance_adapter.c` | Tap dance → SM bridge |
| `features/leader.sm` | Leader state diagram |
| `features/leader_adapter.c` | Leader → SM bridge |
| `quantum/rules.mk` | StateSmith generation rules |

---

## Trade-offs

| Pro | Con |
|-----|-----|
| One uniform API for all features | StateSmith CLI dependency in build |
| Visual diagrams = living docs | Adapter boilerplate (~20 lines/feature) |
| Deterministic phase ordering | Breaking change to existing code |
| New feature = draw + 1 line register | Generated C files (commit or regenerate) |
| State machines are verifiable | |
