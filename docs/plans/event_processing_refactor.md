# QMK Key Event Processing Refactoring

## Design Document

**Version:** 2.0  
**Date:** 2026-04-11  
**Status:** Proposal (revised after review)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Problem Statement](#2-problem-statement)
3. [Architecture Design](#3-architecture-design)
4. [Detailed Design](#4-detailed-design)
   - 4.1–4.6: Event types, State machine framework, Sequence matching, Action output, Feature SMs, Integration
   - 4.7: Process Record Quantum Chain — Migration Strategy
5. [Implementation Plan](#5-implementation-plan) (~25 weeks across 6 phases)
6. [API Reference](#6-api-reference)
7. [Examples](#7-examples)
8. [Migration Guide](#8-migration-guide)
9. [Testing Strategy](#9-testing-strategy)
10. [Appendices](#10-appendices) (A: Memory, B: Timing, C: Compatibility, D: Troubleshooting, E: Split Keyboards)

---

## 1. Overview

### 1.1 Purpose

This document proposes a refactoring of QMK's key event processing system to address architectural complexity, improve extensibility, and enable cleaner implementation of sequence-based features.

### 1.2 Scope

- Core event processing pipeline
- State machine framework for feature implementation
- Sequence pattern matching system
- Compatibility layer for existing code
- Migration path for legacy features

### 1.3 Target Audience

- QMK core maintainers
- Keyboard firmware developers
- Feature contributors

### 1.4 Key Benefits

| Benefit | Description |
|---------|-------------|
| **Modularity** | Features are independent, composable state machines |
| **Extensibility** | New features don't require core modifications |
| **Testability** | Each state machine can be tested in isolation |
| **Maintainability** | Clear state transitions instead of implicit side effects |
| **Discoverability** | Unified API for sequence-based features |

---

## 2. Problem Statement

### 2.1 Current Architecture Issues

#### 2.1.1 Scattered State Management

State is fragmented across multiple modules:

```
┌─────────────────────────────────────────────────────────────┐
│                    STATE LOCATIONS                          │
├─────────────────────────────────────────────────────────────┤
│  action.c:          retro_tapping_counter, tp_buttons       │
│  action_util.c:     real_mods, weak_mods, oneshot_*         │
│  action_layer.c:    layer_state, default_layer_state        │
│  action_tapping.c:  tap state tracking                      │
│  process_combo.c:   combo state                             │
│  process_tap_dance: tap dance state                         │
│  ...               (many more)                              │
└─────────────────────────────────────────────────────────────┘
```

**Impact:** Difficult to reason about global state, race conditions possible, hard to debug.

#### 2.1.2 Complex Conditional Execution

The `action_exec()` function contains numerous `#ifdef` branches:

```c
void action_exec(keyevent_t event) {
    #ifdef SWAP_HANDS_ENABLE
        process_hand_swap(&event);
    #endif
    
    #ifndef NO_ACTION_ONESHOT
        if (keymap_config.oneshot_enable) {
            #if (defined(ONESHOT_TIMEOUT) && (ONESHOT_TIMEOUT > 0))
                if (has_oneshot_layer_timed_out()) {
                    clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
                }
                // ... more nested conditionals
            #endif
        }
    #endif
    
    #ifndef NO_ACTION_TAPPING
        #if defined(AUTO_SHIFT_ENABLE) && defined(RETRO_SHIFT)
            // ... more nested logic
        #endif
        if (IS_NOEVENT(record.event) || pre_process_record_quantum(&record)) {
            action_tapping_process(record);
        }
    #else
        // ... alternative path
    #endif
}
```

**Impact:** Difficult to understand full execution path, hard to add new features.

#### 2.1.3 No Unified Sequence Handling

Each sequence-based feature implements its own detection logic:

| Feature | Implementation | Pattern |
|---------|---------------|---------|
| Combos | `process_combo.c` | Custom timing windows |
| Tap Dance | `process_tap_dance.c` | Custom timing windows |
| Leader | `leader.c` | Custom timing windows |
| Retro Tapping | `action.c` | Embedded in action processing |

**Impact:** Code duplication, inconsistent behavior, hard to add new sequence features.

#### 2.1.4 Implicit State Transitions

State changes occur through side effects rather than explicit transitions:

```c
// Current: State change as side effect
void action_tapping_process(keyrecord_t *record) {
    // ... complex logic with multiple state changes ...
    record->tap.interrupted = true;  // State change
    record->tap.count++;             // State change
    if (condition) {
        record->tap.count = 0;       // State change
    }
}
```

**Impact:** Hard to debug, hard to verify correctness, state invariants difficult to maintain.

### 2.2 Requirements

#### 2.2.1 Functional Requirements

1. **FR-1:** System shall process key events from physical input to HID report
2. **FR-2:** System shall support tap-hold key resolution
3. **FR-3:** System shall support layer switching
4. **FR-4:** System shall support modifier key handling
5. **FR-5:** System shall support sequence detection (combos, tap-dance, etc.)
6. **FR-6:** System shall support oneshot modifiers and layers
7. **FR-7:** System shall support retro tapping
8. **FR-8:** System shall provide hooks for user customization

#### 2.2.2 Non-Functional Requirements

1. **NFR-1:** Memory footprint shall not increase by more than 2KB for typical keyboards
2. **NFR-2:** Latency shall not increase by more than 1ms per feature stage
3. **NFR-3:** System shall maintain backward compatibility with existing keymaps
4. **NFR-4:** Each state machine shall be independently testable
5. **NFR-5:** System shall support dynamic feature enable/disable at runtime (optional)

---

## 3. Architecture Design

### 3.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           QMK EVENT ARCHITECTURE                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌────────────────┐                                                         │
│  │   Physical     │                                                         │
│  │    Input       │                                                         │
│  └──────┬─────────┘                                                         │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐                                                         │
│  │  Matrix Scanner│  Reads GPIO, builds matrix state                        │
│  └──────┬─────────┘                                                         │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐                                                         │
│  │   Debouncer    │  Filters mechanical bounce                              │
│  └──────┬─────────┘                                                         │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐                                                         │
│  │  Change        │  Detects state changes                                  │
│  │  Detector      │                                                         │
│  └──────┬─────────┘                                                         │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐                                                         │
│  │  Event         │  Creates unified_event_t                                │
│  │  Factory       │                                                         │
│  └──────┬─────────┘                                                         │
│         │                                                                   │
│         ▼                                                                   │
│  ╔═══════════════════════════════════════════════════════════════════╗     │
│  ║                    STATE MACHINE PIPELINE                          ║     │
│  ╠═══════════════════════════════════════════════════════════════════╣     │
│  ║                                                                   ║     │
│  ║  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         ║     │
│  ║  │  Combo   │──│  Override│──│  Tap     │──│  Leader  │── ...   ║     │
│  ║  │  SM      │  │  SM      │  │  Hold    │  │  SM      │         ║     │
│  ║  └──────────┘  └──────────┘  └──────────┘  └──────────┘         ║     │
│  ║                                                                   ║     │
│  ╠═══════════════════════════════════════════════════════════════════╣     │
│  ║                                                                   ║     │
│  ║  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         ║     │
│  ║  │  Retro   │──│  Oneshot │──│  Layer   │──│  Action  │         ║     │
│  ║  │  Tap SM  │  │  SM      │  │  SM      │  │  SM      │         ║     │
│  ║  └──────────┘  └──────────┘  └──────────┘  └──────────┘         ║     │
│  ╚═══════════════════════════════════════════════════════════════════╝     │
│         │                                                                   │
│         ▼                                                                   │
│  ┌────────────────┐                                                         │
│  │  HID Report    │  Builds and sends USB reports                           │
│  │  Builder       │                                                         │
│  └────────────────┘                                                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Component Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         COMPONENT ARCHITECTURE                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                        EVENT LAYER                                 │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                    │    │
│  │  unified_event_t        - Base event structure                     │    │
│  │  event_type_t           - Event type enumeration                   │    │
│  │  Event factory macros   - Event creation helpers                   │    │
│  │                                                                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                   │                                         │
│                                   ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     STATE MACHINE LAYER                            │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                    │    │
│  │  state_machine_t        - State machine structure                  │    │
│  │  state_result_t         - Transition result enumeration            │    │
│  │  sm_register()          - Register state machine                   │    │
│  │  sm_process()           - Process event through pipeline           │    │
│  │  sm_tick_all()          - Send tick to all state machines          │    │
│  │                                                                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                   │                                         │
│                                   ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     SEQUENCE LAYER                                 │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                    │    │
│  │  sequence_pattern_t     - Sequence matcher structure               │    │
│  │  sequence_result_t      - Match result enumeration                 │    │
│  │  seq_init()             - Initialize pattern                       │    │
│  │  seq_match()            - Match event against pattern              │    │
│  │  seq_timeout()          - Check timeout                            │    │
│  │  seq_reset()            - Reset matcher                            │    │
│  │                                                                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                   │                                         │
│                                   ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                    FEATURE LAYER                                   │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                    │    │
│  │  process_combo_state.c      - Combo state machine                  │    │
│  │  process_tap_dance_state.c  - Tap dance state machine              │    │
│  │  process_leader_state.c     - Leader state machine                 │    │
│  │  process_retro_state.c      - Retro tapping state machine          │    │
│  │  process_oneshot_state.c    - Oneshot state machine                │    │
│  │  process_layer_state.c      - Layer state machine                  │    │
│  │                                                                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 State Machine Composition

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     STATE MACHINE COMPOSITION MODEL                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Event Flow:                                                                │
│                                                                             │
│      ┌─────────┐                                                            │
│      │ Event   │                                                            │
│      └────┬────┘                                                            │
│           │                                                                 │
│           │                                                                 │
│           ▼                                                                 │
│  ┌─────────────────┐                                                        │
│  │  State Machine  │    STATE_ACCEPT  ┌─────────────┐                       │
│  │      A          │─────────────────▶│  State      │                       │
│  └───────┬─────────┘    STATE_REJECT  │  Machine    │                       │
│          │                             │      B      │                       │
│          │ STATE_HOLD                  └──────┬──────┘                       │
│          │                                    │                              │
│          ▼                                    │ STATE_ACCEPT                  │
│  ┌─────────────┐            STATE_TRANSFORM   │                              │
│  │ Event Held  │◀─────────────────────────────┘                              │
│  │ (Delayed)   │                              │                              │
│  └─────────────┘                              ▼                              │
│                                              │                              │
│                                      ┌───────┴────────┐                     │
│                                      │  New Event     │                     │
│                                      │  (Transformed) │                     │
│                                      └────────────────┘                     │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.4 State Transition Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     STATE TRANSITION MODEL                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Per-Feature State Machine:                                                 │
│                                                                             │
│      ┌──────────────┐                                                       │
│      │    IDLE      │                                                       │
│      └──────┬───────┘                                                       │
│             │ 1. Trigger Event                                              │
│             ▼                                                               │
│      ┌──────────────┐                                                       │
│      │   PENDING    │◀────────────────────────────────┐                     │
│      │              │                                 │                     │
│      │ 2. Wait for  │ 6. Cancel Event                 │                     │
│      │    Next      │─────────────────────────────────┘                     │
│      │    Event     │                                                       │
│      └──────┬───────┘                                                       │
│             │ 3. Complete Event                                             │
│             │ 4. Timeout                                                    │
│             ▼                                                               │
│      ┌──────────────┐                                                       │
│      │   ACTIVE     │                                                       │
│      │              │                                                       │
│      │ 7. Produce   │                                                       │
│      │    Output    │                                                       │
│      └──────┬───────┘                                                       │
│             │ 8. Cleanup                                                    │
│             ▼                                                               │
│      ┌──────────────┐                                                       │
│      │    IDLE      │                                                       │
│      └──────────────┘                                                       │
│                                                                             │
│  Event Processing Results:                                                  │
│                                                                             │
│  STATE_ACCEPT    - Event consumed, produce output, STOP propagation         │
│  STATE_REJECT    - Event not handled, pass to NEXT state machine            │
│  STATE_HOLD      - Event held for later resolution, STOP propagation        │
│  STATE_TRANSFORM - Event transformed, restart pipeline with new event       │
│                    (iterative, max SM_MAX_TRANSFORM_DEPTH passes)            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Detailed Design

### 4.1 Event Type System

#### 4.1.1 Event Type Enumeration

```c
// Location: quantum/event_state.h

typedef enum {
    // Physical Input Events
    EVT_KEY_PRESS,           ///< Key pressed
    EVT_KEY_RELEASE,         ///< Key released
    EVT_ENCODER_CW,          ///< Encoder clockwise
    EVT_ENCODER_CCW,         ///< Encoder counter-clockwise
    
    // Derived Events
    EVT_COMBO_TRIGGER,       ///< Combo detected (pressed)
    EVT_COMBO_RELEASE,       ///< Combo released
    EVT_LEADER_COMPLETE,     ///< Leader sequence complete
    
    // System Events
    EVT_TICK,                ///< Internal timing event
    EVT_LAYER_SWITCH,        ///< Layer state changed
    EVT_MOD_CHANGE,          ///< Modifier state changed
    
    // Sentinel
    EVT_COUNT                ///< Number of event types
} event_type_t;
```

#### 4.1.2 Unified Event Structure

```c
// Location: quantum/event_state.h

typedef struct {
    event_type_t type;       ///< Event type (uint8_t via compiler flag)
    keypos_t     pos;        ///< Key position or encoder ID
    uint16_t     timestamp;  ///< Timestamp in milliseconds
    bool         pressed;    ///< Pressed (true) or released (false)
    uint8_t      metadata;   ///< Feature-specific data
} unified_event_t;

// Memory layout (no packed attribute — avoids unaligned access faults on ARM):
//   ARM Cortex-M (32-bit): 8 bytes (with -fshort-enums for event_type_t)
//   AVR ATmega32U4 (8-bit): 7 bytes
//
// Note: No void* context field. QMK does not use heap allocation (malloc/free).
// Feature-specific context belongs in the state machine's state_data, not in
// the event struct. This avoids dangling pointer risk and keeps events copyable
// by value without ownership concerns.
//
// Platform compatibility: QMK still supports AVR (ATmega32U4) in addition to
// ARM Cortex-M. Use -fshort-enums compiler flag (already set in QMK's build
// system) to ensure event_type_t is uint8_t on both platforms. Avoid
// __attribute__((packed)) which causes unaligned access faults on some ARM
// variants; natural alignment is sufficient given the field sizes.
```

#### 4.1.3 Event Factory Macros

```c
// Location: quantum/event_state.h

// Key event creation
#define EVT_KEY(pos, pressed) \
    ((unified_event_t){ \
        .type = (pressed) ? EVT_KEY_PRESS : EVT_KEY_RELEASE, \
        .pos = (pos), \
        .timestamp = timer_read(), \
        .pressed = (pressed), \
        .metadata = 0 \
    })

// Tick event
#define EVT_TICK() \
    ((unified_event_t){ \
        .type = EVT_TICK, \
        .pressed = false, \
        .timestamp = timer_read() \
    })

// Encoder event
#define EVT_ENCODER(enc_id, direction, pressed) \
    ((unified_event_t){ \
        .type = (direction == ENCODER_CW) ? EVT_ENCODER_CW : EVT_ENCODER_CCW, \
        .pos = MAKE_KEYPOS(KEYLOC_ENCODER, (enc_id)), \
        .timestamp = timer_read(), \
        .pressed = (pressed), \
        .metadata = 0 \
    })
```

### 4.2 State Machine Framework

#### 4.2.1 State Machine Structure

```c
// Location: quantum/event_state.h

typedef enum {
    STATE_ACCEPT,      ///< Event consumed, produce output
    STATE_REJECT,      ///< Event not handled, pass through
    STATE_HOLD,        ///< Event held for later resolution
    STATE_TRANSFORM    ///< Event transformed, new event produced
} state_result_t;

typedef struct state_machine state_machine_t;

// Handler function signature
typedef state_result_t (*state_handler_fn)(
    state_machine_t* sm,   ///< State machine instance
    unified_event_t* event,///< Event to process
    void* output          ///< Output buffer (action output, new event, etc.)
);

// State machine structure
typedef struct state_machine {
    const char*           name;         ///< Debug name
    void*                 state_data;   ///< Statically allocated state (pointer to static global)
    size_t                state_size;   ///< Size of state block
    state_handler_fn      handler;      ///< Event handler
    state_handler_fn      tick_handler; ///< Tick handler (optional)
    void (*cleanup_fn)(state_machine_t*); ///< Cleanup function
    state_machine_t*      next;         ///< Next in pipeline
} state_machine_t;
```

#### 4.2.2 Pipeline Operations

```c
// Location: quantum/event_state.h

/**
 * @brief Register a state machine in the processing pipeline
 * 
 * State machines are added to the end of the pipeline.
 * Events flow through the pipeline in registration order.
 * 
 * @param sm Pointer to state machine structure
 */
void sm_register(state_machine_t* sm);

/**
 * @brief Get the pipeline head
 * 
 * @return Pointer to first state machine, or NULL if empty
 */
state_machine_t* sm_get_pipeline(void);

/**
 * @brief Process an event through the entire state machine pipeline
 * 
 * @param head Pointer to pipeline head
 * @param event Event to process
 * @param output Output buffer for processed result
 * @return Final state result
 */
state_result_t sm_process(state_machine_t* head, unified_event_t* event, void* output);

/**
 * @brief Send tick event to all state machines
 * 
 * Called periodically to allow state machines to handle timeouts
 * and periodic tasks.
 * 
 * @param head Pointer to pipeline head
 */
void sm_tick_all(state_machine_t* head);

/**
 * @brief Remove a state machine from the pipeline
 * 
 * @param sm Pointer to state machine to remove
 * @return true if removed, false if not found
 */
bool sm_unregister(state_machine_t* sm);
```

#### 4.2.3 Implementation Details

```c
// Location: quantum/event_state.c

// Pipeline head
static state_machine_t* g_pipeline = NULL;

void sm_register(state_machine_t* sm) {
    // Validate parameters
    if (!sm || !sm->handler) {
        return;
    }
    
    // Clear next pointer
    sm->next = NULL;
    
    // Insert at end of chain
    if (!g_pipeline) {
        g_pipeline = sm;
    } else {
        state_machine_t* cur = g_pipeline;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = sm;
    }
}

state_machine_t* sm_get_pipeline(void) {
    return g_pipeline;
}

state_result_t sm_process(state_machine_t* head, unified_event_t* event, void* output) {
    if (!head || !event) {
        return STATE_REJECT;
    }
    
    // Iterative transform loop with hard depth limit to prevent unbounded
    // recursion on embedded targets (ARM Cortex-M with 4-8KB stack).
    // Each transform re-feeds the transformed event through the full pipeline.
    #define SM_MAX_TRANSFORM_DEPTH 4
    
    unified_event_t  current_event = *event;
    unified_event_t  transform_buf;         // Separate buffer for transformed events
    action_output_t  transform_output;      // Separate output buffer for transform pass
    
    for (uint8_t depth = 0; depth < SM_MAX_TRANSFORM_DEPTH; depth++) {
        state_machine_t* cur = head;
        state_result_t   result = STATE_REJECT;
        
        while (cur) {
            // Use the caller's output buffer on the final (non-transform) path
            result = cur->handler(cur, &current_event, output);
            
            switch (result) {
                case STATE_ACCEPT:
                    // Event consumed by this state machine — stop pipeline propagation.
                    // The handler has written its action to the output buffer.
                    return STATE_ACCEPT;
                    
                case STATE_REJECT:
                    // Not handled by this SM, pass to next in pipeline
                    break;
                    
                case STATE_HOLD:
                    // Event held for later resolution — stop propagation
                    return STATE_HOLD;
                    
                case STATE_TRANSFORM:
                    // Event transformed into a new event. The handler wrote the new
                    // event description into the output buffer. Copy it into our
                    // transform_buf (separate from output to avoid type-punning),
                    // then restart the pipeline with the new event.
                    memcpy(&transform_buf, output, sizeof(unified_event_t));
                    current_event = transform_buf;
                    memset(output, 0, sizeof(action_output_t));
                    goto next_transform_pass;  // restart pipeline from head
            }
            
            cur = cur->next;
        }
        
        // Reached end of pipeline without STATE_ACCEPT or STATE_TRANSFORM
        return result;
        
        next_transform_pass:
            continue;  // re-enter for loop, restart pipeline from head
    }
    
    // Transform depth limit exceeded — treat as reject to avoid infinite loops.
    // This should not happen in normal operation; log for debugging.
    #ifdef EVENT_DEBUG
    dprintf("SM: transform depth limit (%d) exceeded\n", SM_MAX_TRANSFORM_DEPTH);
    #endif
    return STATE_REJECT;
}

void sm_tick_all(state_machine_t* head) {
    // Tick event and output buffer — allocated once on stack per scan cycle.
    // Tick handlers may produce actions (e.g., tap-hold timeout → resolve as hold),
    // so we must provide valid buffers, not NULL.
    unified_event_t tick_event = {
        .type = EVT_TICK,
        .timestamp = timer_read(),
        .pressed = false
    };
    action_output_t tick_output = {0};
    
    state_machine_t* cur = head;
    while (cur) {
        if (cur->tick_handler) {
            memset(&tick_output, 0, sizeof(tick_output));
            state_result_t result = cur->tick_handler(cur, &tick_event, &tick_output);
            
            // If tick produced an action, execute it immediately
            if (result == STATE_ACCEPT && tick_output.type != ACTION_NONE) {
                execute_action_output(&tick_output);
            }
        }
        cur = cur->next;
    }
}

bool sm_unregister(state_machine_t* target) {
    if (!target) return false;
    
    // Special case: removing head
    if (g_pipeline == target) {
        g_pipeline = g_pipeline->next;
        target->next = NULL;
        return true;
    }
    
    // Find predecessor
    state_machine_t* cur = g_pipeline;
    while (cur && cur->next) {
        if (cur->next == target) {
            cur->next = target->next;
            target->next = NULL;
            return true;
        }
        cur = cur->next;
    }
    
    return false;
}
```

### 4.3 Sequence Pattern Matching

#### 4.3.1 Sequence Pattern Structure

```c
// Location: quantum/event_state.h

typedef enum {
    SEQ_MATCH_NONE,      ///< No match
    SEQ_MATCH_PARTIAL,   ///< Partial match in progress
    SEQ_MATCH_COMPLETE,  ///< Complete match found
    SEQ_MATCH_FAILED     ///< Match failed (timeout or mismatch)
} sequence_result_t;

// Sequence step: matches both event type AND key identity.
// Without key matching, a sequence of "press A, press B" would match
// "press C, press D" (both are EVT_KEY_PRESS, EVT_KEY_PRESS). This renders
// the sequence layer useless for combos, leader sequences, and tap-dance,
// all of which must identify specific keys.
typedef struct {
    event_type_t type;       ///< Expected event type (EVT_KEY_PRESS, etc.)
    uint16_t     keycode;    ///< Expected keycode (KC_NO = wildcard, matches any key)
    keypos_t     pos;        ///< Expected position (KEYPOS_ANY = wildcard)
} sequence_step_t;

// Special values for wildcard matching
#define KC_WILDCARD   KC_NO                       // Match any keycode
#define KEYPOS_ANY    ((keypos_t){.row = 255, .col = 255})  // Match any position

#ifndef SEQ_MAX_LENGTH
#define SEQ_MAX_LENGTH 8   ///< Maximum steps in a sequence
#endif

typedef struct {
    sequence_step_t steps[SEQ_MAX_LENGTH]; ///< Sequence of expected steps
    uint8_t      length;      ///< Number of steps in sequence
    uint16_t     timeout_ms;  ///< Timeout in milliseconds (0 = no timeout)
    uint16_t     timestamp;   ///< Timestamp of first event
    uint8_t      current_idx; ///< Current position in sequence
    bool         strict;      ///< Strict mode (no partial restart)
} sequence_pattern_t;
```

#### 4.3.2 Sequence Matching Operations

```c
// Location: quantum/event_state.h

/**
 * @brief Initialize a sequence pattern matcher
 * 
 * @param seq Pointer to sequence pattern structure
 * @param steps Array of sequence steps (type + key identity)
 * @param len Number of steps
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 */
void seq_init(sequence_pattern_t* seq, sequence_step_t* steps, uint8_t len, uint16_t timeout_ms);

/**
 * @brief Initialize with strict mode
 * 
 * In strict mode, a mismatch resets the sequence completely
 * without attempting to restart from the first event.
 * 
 * @param seq Pointer to sequence pattern structure
 * @param steps Array of sequence steps
 * @param len Number of steps
 * @param timeout_ms Timeout in milliseconds
 */
void seq_init_strict(sequence_pattern_t* seq, sequence_step_t* steps, uint8_t len, uint16_t timeout_ms);

/**
 * @brief Feed an event to the sequence matcher
 * 
 * Matches both event type AND key identity (keycode or position).
 * A step with keycode=KC_WILDCARD or pos=KEYPOS_ANY matches any key.
 * 
 * @param seq Pointer to sequence pattern structure
 * @param event Event to match
 * @return Matching result
 */
sequence_result_t seq_match(sequence_pattern_t* seq, unified_event_t* event);

/**
 * @brief Check if sequence has timed out
 * 
 * @param seq Pointer to sequence pattern structure
 * @return true if timed out
 */
bool seq_timeout(sequence_pattern_t* seq);

/**
 * @brief Reset the sequence matcher
 * 
 * @param seq Pointer to sequence pattern structure
 */
void seq_reset(sequence_pattern_t* seq);

/**
 * @brief Get current progress
 * 
 * @param seq Pointer to sequence pattern structure
 * @return Number of steps matched so far
 */
uint8_t seq_progress(sequence_pattern_t* seq);
```

#### 4.3.3 Sequence Matching Implementation

```c
// Location: quantum/event_sequence.c

void seq_init(sequence_pattern_t* seq, sequence_step_t* steps, uint8_t len, uint16_t timeout_ms) {
    if (!seq || !steps || len == 0 || len > SEQ_MAX_LENGTH) return;
    
    memcpy(seq->steps, steps, len * sizeof(sequence_step_t));
    seq->length = len;
    seq->timeout_ms = timeout_ms;
    seq->timestamp = timer_read();
    seq->current_idx = 0;
    seq->strict = false;
}

void seq_init_strict(sequence_pattern_t* seq, sequence_step_t* steps, uint8_t len, uint16_t timeout_ms) {
    seq_init(seq, steps, len, timeout_ms);
    seq->strict = true;
}

// Check if a single event matches a sequence step.
// A step can match on event type, keycode, position, or any combination.
// Wildcard values (KC_WILDCARD, KEYPOS_ANY) match anything.
static bool step_matches(sequence_step_t* step, unified_event_t* event) {
    // Event type must always match
    if (event->type != step->type) {
        return false;
    }
    
    // Check keycode match (if not wildcard)
    if (step->keycode != KC_WILDCARD) {
        uint16_t event_keycode = get_keycode_for_pos(event->pos);
        if (event_keycode != step->keycode) {
            return false;
        }
    }
    
    // Check position match (if not wildcard)
    if (step->pos.row != KEYPOS_ANY.row || step->pos.col != KEYPOS_ANY.col) {
        if (!KEYEQ(event->pos, step->pos)) {
            return false;
        }
    }
    
    return true;
}

sequence_result_t seq_match(sequence_pattern_t* seq, unified_event_t* event) {
    if (!seq || !event) return SEQ_MATCH_NONE;
    
    // Check timeout
    if (seq->timeout_ms > 0 && 
        timer_elapsed(seq->timestamp) > seq->timeout_ms) {
        seq->current_idx = 0;
        return SEQ_MATCH_FAILED;
    }
    
    // Check if event matches expected step at current position
    if (step_matches(&seq->steps[seq->current_idx], event)) {
        seq->current_idx++;
        
        if (seq->current_idx >= seq->length) {
            // Complete match!
            return SEQ_MATCH_COMPLETE;
        }
        return SEQ_MATCH_PARTIAL;
    }
    
    // Mismatch detected
    if (seq->strict) {
        // Strict mode: complete reset
        seq->current_idx = 0;
    } else if (step_matches(&seq->steps[0], event)) {
        // Non-strict: try to restart from first step
        seq->current_idx = 1;
        seq->timestamp = event->timestamp;
        return SEQ_MATCH_PARTIAL;
    } else {
        seq->current_idx = 0;
    }
    
    return SEQ_MATCH_NONE;
}

bool seq_timeout(sequence_pattern_t* seq) {
    if (!seq || seq->timeout_ms == 0) return false;
    return timer_elapsed(seq->timestamp) > seq->timeout_ms;
}

void seq_reset(sequence_pattern_t* seq) {
    if (!seq) return;
    seq->current_idx = 0;
    seq->timestamp = timer_read();
}

uint8_t seq_progress(sequence_pattern_t* seq) {
    return seq ? seq->current_idx : 0;
}
```

> **Design note — Why match key identity, not just event type:**
> The sequence layer is the foundation for combo detection, leader sequences,
> and tap-dance features. All of these need to distinguish *which* key was
> pressed, not just *that* a key was pressed. Without key matching, the pattern
> `[EVT_KEY_PRESS, EVT_KEY_PRESS]` would match any two key presses, making it
> impossible to define "press A then press B" as distinct from "press C then D".
>
> The `sequence_step_t` struct supports three matching modes:
> 1. **Exact position:** Match a specific physical key (for combos, key overrides)
> 2. **Exact keycode:** Match a specific logical key (for leader, tap-dance)
> 3. **Wildcard:** Match any key of a given event type (for structural patterns)
>
> The `get_keycode_for_pos()` call in `step_matches()` performs a layer-aware
> keymap lookup, consistent with how QMK currently resolves keycodes.

### 4.4 Action Output Structure

```c
// Location: quantum/action_output.h

typedef enum {
    ACTION_NONE,           ///< No action
    ACTION_KEY_PRESS,      ///< Key press
    ACTION_KEY_RELEASE,    ///< Key release
    ACTION_MOD_PRESS,      ///< Modifier press
    ACTION_MOD_RELEASE,    ///< Modifier release
    ACTION_LAYER_ON,       ///< Layer turn on
    ACTION_LAYER_OFF,      ///< Layer turn off
    ACTION_LAYER_TOGGLE,   ///< Layer toggle
    ACTION_MACRO_START,    ///< Macro start
    ACTION_MOUSE_MOVE,     ///< Mouse move
    ACTION_MOUSE_CLICK,    ///< Mouse click
    ACTION_CONSUMER,       ///< Consumer control
    ACTION_SYSTEM          ///< System control
} action_type_t;

typedef struct {
    action_type_t type;
    union {
        uint8_t keycode;      ///< For key actions
        uint8_t mods;         ///< For modifier actions
        uint8_t layer;        ///< For layer actions
        uint16_t macro_id;    ///< For macro actions
        struct {
            int16_t x;
            int16_t y;
        } mouse_delta;        ///< For mouse move
        uint8_t mouse_btn;    ///< For mouse click
        uint16_t usage_code;  ///< For consumer/system
    } data;
    bool execute_immediate;   ///< Execute without delay
} action_output_t;
```

### 4.5 Feature State Machines

#### 4.5.1 Tap-Hold State Machine

```c
// Location: quantum/process_taphold_state.c

// State enumeration
typedef enum {
    TH_STATE_IDLE,           ///< No pending tap-hold
    TH_STATE_PENDING,        ///< Waiting for tap/hold resolution
    TH_STATE_RESOLVED_TAP,   ///< Resolved as tap
    TH_STATE_RESOLVED_HOLD   ///< Resolved as hold
} taphold_state_e;

// Per-key tap-hold slot (one per concurrent tap-hold key)
typedef struct {
    taphold_state_e state;
    keypos_t        key_pos;
    uint16_t        keycode;
    uint16_t        press_time;
    uint8_t         interrupt_count;
    bool            interrupted;
} taphold_slot_t;

// Pooled state: supports up to TAPHOLD_MAX_CONCURRENT simultaneous tap-hold keys.
// Home row mods commonly require 4 concurrent slots (one per home row finger).
// The current QMK implementation handles this via waiting_buffer[WAITING_BUFFER_SIZE]
// in action_tapping.c; we use an explicit slot pool instead.
#ifndef TAPHOLD_MAX_CONCURRENT
#define TAPHOLD_MAX_CONCURRENT 8
#endif

// Waiting buffer for events received while tap-hold keys are pending.
// These events must be replayed if a tap-hold resolves as tap (the interrupting
// keys need to be processed after the tap output).
#ifndef TAPHOLD_WAITING_BUFFER_SIZE
#define TAPHOLD_WAITING_BUFFER_SIZE 8
#endif

typedef struct {
    taphold_slot_t   slots[TAPHOLD_MAX_CONCURRENT];
    uint8_t          active_count;         ///< Number of active slots
    unified_event_t  waiting_buffer[TAPHOLD_WAITING_BUFFER_SIZE];
    uint8_t          waiting_count;        ///< Events in waiting buffer
} taphold_state_t;

// --- Slot management helpers ---

static taphold_slot_t* th_find_slot(taphold_state_t* state, keypos_t pos) {
    for (uint8_t i = 0; i < state->active_count; i++) {
        if (KEYEQ(state->slots[i].key_pos, pos)) {
            return &state->slots[i];
        }
    }
    return NULL;
}

static taphold_slot_t* th_alloc_slot(taphold_state_t* state) {
    if (state->active_count >= TAPHOLD_MAX_CONCURRENT) {
        return NULL;  // Pool exhausted — fall through to normal processing
    }
    return &state->slots[state->active_count++];
}

static void th_free_slot(taphold_state_t* state, taphold_slot_t* slot) {
    uint8_t idx = slot - state->slots;
    if (idx < state->active_count - 1) {
        // Compact: move last slot into freed position
        state->slots[idx] = state->slots[state->active_count - 1];
    }
    state->active_count--;
}

static void th_buffer_event(taphold_state_t* state, unified_event_t* event) {
    if (state->waiting_count < TAPHOLD_WAITING_BUFFER_SIZE) {
        state->waiting_buffer[state->waiting_count++] = *event;
    }
    // If buffer full, event is dropped (same behavior as current QMK)
}

// --- State machine handler ---

static state_result_t taphold_handler(state_machine_t* sm, unified_event_t* event, void* output) {
    taphold_state_t* state = (taphold_state_t*)sm->state_data;
    
    // Only handle key events
    if (event->type != EVT_KEY_PRESS && event->type != EVT_KEY_RELEASE) {
        return STATE_REJECT;
    }
    
    // Check if this event is for an already-tracked tap-hold key
    taphold_slot_t* slot = th_find_slot(state, event->pos);
    
    if (slot) {
        // Event for a pending/resolved tap-hold key
        switch (slot->state) {
            case TH_STATE_PENDING:
                return th_pending_handler(state, slot, event, output);
                
            case TH_STATE_RESOLVED_TAP:
            case TH_STATE_RESOLVED_HOLD:
                // Key released after resolution — clean up slot
                if (!event->pressed) {
                    if (slot->state == TH_STATE_RESOLVED_HOLD) {
                        action_output_t* out = (action_output_t*)output;
                        out->type = ACTION_LAYER_OFF;
                        out->data.layer = get_taphold_layer(slot->keycode);
                        th_free_slot(state, slot);
                        return STATE_ACCEPT;
                    }
                    th_free_slot(state, slot);
                }
                return STATE_REJECT;
                
            default:
                return STATE_REJECT;
        }
    }
    
    // New key event — check if it starts a new tap-hold
    if (event->pressed) {
        uint16_t keycode = get_keycode_for_pos(event->pos);
        
        if (IS_TAPHOLD_KEYCODE(keycode)) {
            slot = th_alloc_slot(state);
            if (!slot) {
                return STATE_REJECT;  // Pool full, process as normal key
            }
            
            slot->state = TH_STATE_PENDING;
            slot->key_pos = event->pos;
            slot->keycode = keycode;
            slot->press_time = event->timestamp;
            slot->interrupt_count = 0;
            slot->interrupted = false;
            
            return STATE_HOLD;
        }
    }
    
    // Not a tap-hold key. If any tap-hold keys are pending, this event
    // interrupts them. Buffer it and notify pending slots.
    if (state->active_count > 0) {
        for (uint8_t i = 0; i < state->active_count; i++) {
            if (state->slots[i].state == TH_STATE_PENDING) {
                state->slots[i].interrupt_count++;
                state->slots[i].interrupted = true;
                
                // Check PERMISSIVE_HOLD: resolve immediately on interrupt
                #ifdef PERMISSIVE_HOLD
                if (event->pressed) {
                    state->slots[i].state = TH_STATE_RESOLVED_HOLD;
                    action_output_t* out = (action_output_t*)output;
                    out->type = ACTION_MOD_PRESS;
                    out->data.mods = get_taphold_mods(state->slots[i].keycode);
                    // Don't buffer — let the interrupting key flow through
                    return STATE_ACCEPT;
                }
                #endif
                
                // Check HOLD_ON_OTHER_KEY_PRESS: resolve on any other key press
                #ifdef HOLD_ON_OTHER_KEY_PRESS
                if (event->pressed) {
                    state->slots[i].state = TH_STATE_RESOLVED_HOLD;
                    action_output_t* out = (action_output_t*)output;
                    out->type = ACTION_MOD_PRESS;
                    out->data.mods = get_taphold_mods(state->slots[i].keycode);
                    return STATE_ACCEPT;
                }
                #endif
            }
        }
        
        // Buffer the interrupting event for replay after resolution
        th_buffer_event(state, event);
        return STATE_HOLD;
    }
    
    return STATE_REJECT;
}

// PENDING state: Wait for release (tap) or timeout/interrupt (hold)
static state_result_t th_pending_handler(taphold_state_t* state,
                                          taphold_slot_t* slot,
                                          unified_event_t* event, void* output) {
    if (event->pressed) {
        // Already pending, ignore repeat
        return STATE_HOLD;
    }
    
    // Key released — was it a tap?
    uint16_t hold_duration = timer_elapsed(slot->press_time);
    
    if (hold_duration < get_tapping_term(slot->keycode)) {
        // Tap!
        slot->state = TH_STATE_RESOLVED_TAP;
        action_output_t* out = (action_output_t*)output;
        out->type = ACTION_KEY_PRESS;
        out->data.keycode = get_taphold_tap_keycode(slot->keycode);
        out->execute_immediate = true;
        
        // Replay buffered events after tap output
        // (handled by the pipeline dispatcher after STATE_ACCEPT)
        return STATE_ACCEPT;
    } else {
        // Held too long — it's a hold
        slot->state = TH_STATE_RESOLVED_HOLD;
        action_output_t* out = (action_output_t*)output;
        out->type = ACTION_MOD_PRESS;
        out->data.mods = get_taphold_mods(slot->keycode);
        return STATE_ACCEPT;
    }
}

// Tick handler: Check for timeout on all active slots
static state_result_t taphold_tick(state_machine_t* sm, unified_event_t* event, void* output) {
    taphold_state_t* state = (taphold_state_t*)sm->state_data;
    
    for (uint8_t i = 0; i < state->active_count; i++) {
        taphold_slot_t* slot = &state->slots[i];
        if (slot->state == TH_STATE_PENDING) {
            if (timer_elapsed(slot->press_time) >= get_tapping_term(slot->keycode)) {
                // Timeout — resolve as hold
                slot->state = TH_STATE_RESOLVED_HOLD;
                action_output_t* out = (action_output_t*)output;
                out->type = ACTION_MOD_PRESS;
                out->data.mods = get_taphold_mods(slot->keycode);
                
                // Replay buffered events now that we've resolved
                // TODO: replay waiting_buffer through pipeline
                return STATE_ACCEPT;
            }
        }
    }
    
    return STATE_REJECT;  // No action produced
}

// State machine instance — pooled state supports concurrent tap-hold keys
static taphold_state_t g_taphold_state = {0};

static state_machine_t sm_taphold = {
    .name = "taphold",
    .state_data = &g_taphold_state,
    .state_size = sizeof(taphold_state_t),
    .handler = taphold_handler,
    .tick_handler = taphold_tick,
    .cleanup_fn = NULL,
};

// Initialization
void taphold_state_init(void) {
    sm_register(&sm_taphold);
}
```

> **Design note — Multiple concurrent tap-hold keys:**
> The current QMK implementation in `action_tapping.c` supports concurrent
> tap-hold via a `waiting_buffer[WAITING_BUFFER_SIZE]` (typically 8 entries).
> The pooled slot design here serves the same purpose but with explicit state
> per pending key. This is critical for home row mods, where a user may hold
> `LSFT_T(KC_A)` and `LCTL_T(KC_S)` simultaneously. The `TAPHOLD_MAX_CONCURRENT`
> define (default 8) matches the existing `WAITING_BUFFER_SIZE`.
>
> Per-key tapping term is supported via `get_tapping_term(keycode)`, matching
> the existing `get_tapping_term()` API. Configuration variants like
> `PERMISSIVE_HOLD`, `HOLD_ON_OTHER_KEY_PRESS`, `RETRO_TAPPING`, and
> `QUICK_TAP_TERM` are handled via `#ifdef` guards within the handler, with
> per-key variants using the same `get_*()` function pattern.

#### 4.5.2 Combo State Machine

```c
// Location: quantum/process_combo_state.c

// State enumeration
typedef enum {
    COMBO_STATE_WAITING,     ///< Waiting for combo start
    COMBO_STATE_MATCHING,    ///< Matching combo sequence
    COMBO_STATE_CONFIRMED,   ///< Combo confirmed
    COMBO_STATE_CANCELLED    ///< Combo cancelled
} combo_state_e;

// Per-combo candidate tracking
// Multiple combos can be partially matching simultaneously.
// e.g., combo "A+B" and combo "A+C" are both candidates after pressing A.
#ifndef COMBO_MAX_CANDIDATES
#define COMBO_MAX_CANDIDATES 8  // Max combos matching concurrently
#endif

#ifndef COMBO_HELD_BUFFER_SIZE
#define COMBO_HELD_BUFFER_SIZE 8  // Events held during combo matching
#endif

typedef struct {
    uint8_t   combo_index;       ///< Index into combo definition table
    uint8_t   matched_count;     ///< Keys matched so far
} combo_candidate_t;

// State structure — manages multiple concurrent combo candidates
typedef struct {
    combo_state_e      state;
    combo_candidate_t  candidates[COMBO_MAX_CANDIDATES];
    uint8_t            candidate_count;          ///< Active candidates
    keypos_t           matched_keys[8];          ///< Keys involved in current match
    uint8_t            matched_key_count;         ///< Total unique keys pressed during matching
    uint16_t           start_time;
    uint16_t           last_key_time;
    uint16_t           result_keycode;            ///< Keycode of confirmed combo
    
    // Event replay buffer: holds events received during matching.
    // If the combo is cancelled (timeout, wrong key), these events
    // must be replayed through the pipeline as individual keystrokes.
    unified_event_t    held_events[COMBO_HELD_BUFFER_SIZE];
    uint8_t            held_count;
} combo_state_t;

static combo_state_t g_combo_state = {0};

// --- Helpers ---

static void combo_buffer_event(combo_state_t* state, unified_event_t* event) {
    if (state->held_count < COMBO_HELD_BUFFER_SIZE) {
        state->held_events[state->held_count++] = *event;
    }
}

// Replay held events through the pipeline after combo cancellation.
// This ensures partially-typed combo keys produce their expected individual
// keystroke output rather than being silently swallowed.
static void combo_replay_held_events(combo_state_t* state) {
    for (uint8_t i = 0; i < state->held_count; i++) {
        action_output_t replay_output = {0};
        // Re-inject into pipeline (skip combo SM to avoid re-matching)
        state_machine_t* next_sm = sm_combo.next;
        if (next_sm) {
            state_result_t result = sm_process(next_sm, &state->held_events[i], &replay_output);
            if (result == STATE_ACCEPT) {
                execute_action_output(&replay_output);
            }
        }
    }
    state->held_count = 0;
}

static void combo_cancel_and_replay(combo_state_t* state) {
    state->state = COMBO_STATE_CANCELLED;
    combo_replay_held_events(state);
    
    // Reset state for next combo detection
    state->state = COMBO_STATE_WAITING;
    state->candidate_count = 0;
    state->matched_key_count = 0;
    state->held_count = 0;
}

// --- State machine handler ---

static state_result_t combo_handler(state_machine_t* sm, 
                                     unified_event_t* event, void* output) {
    combo_state_t* state = (combo_state_t*)sm->state_data;
    
    if (event->type != EVT_KEY_PRESS && event->type != EVT_KEY_RELEASE) {
        return STATE_REJECT;
    }
    
    switch (state->state) {
        case COMBO_STATE_WAITING:
            return combo_waiting_handler(state, event, output);
            
        case COMBO_STATE_MATCHING:
            return combo_matching_handler(state, event, output);
            
        case COMBO_STATE_CONFIRMED:
            return combo_confirmed_handler(state, event, output);
            
        default:
            return STATE_REJECT;
    }
}

// WAITING: Check if event starts any combo(s)
static state_result_t combo_waiting_handler(combo_state_t* state,
                                            unified_event_t* event, void* output) {
    if (!event->pressed) return STATE_REJECT;
    
    // Find ALL combos that start with this key (not just the first match).
    // This enables overlapping combo definitions like "A+B→X" and "A+C→Y".
    state->candidate_count = 0;
    
    for (uint8_t i = 0; i < COMBO_COUNT; i++) {
        combo_definition_t* combo = get_combo_definition(i);
        if (is_key_in_combo(combo, event->pos)) {
            if (state->candidate_count < COMBO_MAX_CANDIDATES) {
                state->candidates[state->candidate_count].combo_index = i;
                state->candidates[state->candidate_count].matched_count = 1;
                state->candidate_count++;
            }
        }
    }
    
    if (state->candidate_count == 0) {
        return STATE_REJECT;  // No combo starts with this key
    }
    
    // Start matching — hold the event
    state->state = COMBO_STATE_MATCHING;
    state->matched_keys[0] = event->pos;
    state->matched_key_count = 1;
    state->start_time = event->timestamp;
    state->last_key_time = event->timestamp;
    
    // Buffer this event for potential replay
    combo_buffer_event(state, event);
    
    return STATE_HOLD;
}

// MATCHING: Continue matching combo against all candidates
static state_result_t combo_matching_handler(combo_state_t* state,
                                             unified_event_t* event, void* output) {
    // Check timeout
    combo_definition_t* first_combo = get_combo_definition(
        state->candidates[0].combo_index);
    uint16_t timeout = first_combo->timeout_ms;  // Use first candidate's timeout
    
    if (timer_elapsed(state->start_time) > timeout) {
        // Timeout — cancel and replay all held events
        combo_cancel_and_replay(state);
        return STATE_REJECT;
    }
    
    if (!event->pressed) {
        // Key released during matching — cancel and replay
        combo_cancel_and_replay(state);
        return STATE_REJECT;
    }
    
    // Buffer this event
    combo_buffer_event(state, event);
    
    // Test this key against all remaining candidates.
    // Eliminate candidates where this key doesn't match.
    uint8_t surviving = 0;
    int8_t  completed_idx = -1;
    
    for (uint8_t i = 0; i < state->candidate_count; i++) {
        combo_definition_t* combo = get_combo_definition(
            state->candidates[i].combo_index);
        
        if (is_key_in_combo(combo, event->pos)) {
            state->candidates[i].matched_count++;
            
            // Check if this candidate is fully matched
            if (state->candidates[i].matched_count >= combo->key_count) {
                completed_idx = i;
            }
            
            // Keep this candidate alive
            if (surviving != i) {
                state->candidates[surviving] = state->candidates[i];
            }
            surviving++;
        }
        // else: candidate eliminated (key not in this combo)
    }
    
    state->candidate_count = surviving;
    state->matched_keys[state->matched_key_count++] = event->pos;
    state->last_key_time = event->timestamp;
    
    if (state->candidate_count == 0) {
        // All candidates eliminated — cancel and replay
        combo_cancel_and_replay(state);
        return STATE_REJECT;
    }
    
    if (completed_idx >= 0) {
        // A combo matched completely!
        combo_definition_t* combo = get_combo_definition(
            state->candidates[completed_idx].combo_index);
        
        state->state = COMBO_STATE_CONFIRMED;
        state->result_keycode = combo->keycode;
        state->held_count = 0;  // Discard held events (consumed by combo)
        
        action_output_t* out = (action_output_t*)output;
        out->type = ACTION_KEY_PRESS;
        out->data.keycode = combo->keycode;
        
        return STATE_ACCEPT;
    }
    
    // Still matching — hold event
    return STATE_HOLD;
}

// CONFIRMED: Wait for all keys to release
static state_result_t combo_confirmed_handler(combo_state_t* state,
                                              unified_event_t* event, void* output) {
    if (!event->pressed && is_key_in_matched_set(state, event->pos)) {
        if (all_combo_keys_released(state)) {
            // All released - trigger release event
            action_output_t* out = (action_output_t*)output;
            out->type = ACTION_KEY_RELEASE;
            out->data.keycode = state->result_keycode;
            
            // Reset to waiting
            memset(state, 0, sizeof(combo_state_t));
            state->state = COMBO_STATE_WAITING;
            
            return STATE_ACCEPT;
        }
    }
    
    return STATE_HOLD;
}

static state_result_t combo_tick(state_machine_t* sm, unified_event_t* event, void* output) {
    combo_state_t* state = (combo_state_t*)sm->state_data;
    
    if (state->state == COMBO_STATE_MATCHING) {
        combo_definition_t* combo = get_combo_definition(
            state->candidates[0].combo_index);
        if (timer_elapsed(state->start_time) > combo->timeout_ms) {
            // Timeout — cancel and replay held events
            combo_cancel_and_replay(state);
        }
    }
    
    return STATE_REJECT;  // No action produced by tick
}

static state_machine_t sm_combo = {
    .name = "combo",
    .state_data = &g_combo_state,
    .state_size = sizeof(combo_state_t),
    .handler = combo_handler,
    .tick_handler = combo_tick,
    .cleanup_fn = NULL,
};

void combo_state_init(void) {
    sm_register(&sm_combo);
}
```

> **Design note — Multiple concurrent combo candidates:**
> The current QMK `process_combo.c` tracks multiple partially-matching combos
> via a candidate buffer. The design here mirrors that approach: when a key press
> could start multiple combos (e.g., `A+B→X` and `A+C→Y`), all are tracked as
> candidates. Each subsequent key either narrows the candidate set or eliminates
> all candidates (triggering replay). This prevents the single-combo limitation
> from the v1.0 draft.
>
> **Event replay on cancellation:**
> When a combo match fails (wrong key, timeout, early release), the held events
> are replayed through the pipeline starting *after* the combo SM to avoid
> re-triggering combo detection. This ensures partially-typed combo keys produce
> their expected individual output rather than being silently swallowed.

### 4.6 Integration with Existing Code

#### 4.6.1 Modified action_exec()

```c
// Location: quantum/action.c (refactored)

// Legacy compatibility - convert old keyevent_t to unified_event_t
//
// keyevent_type_t values (from keyboard.h):
//   TICK_EVENT = 0, KEY_EVENT = 1, ENCODER_CW_EVENT = 2,
//   ENCODER_CCW_EVENT = 3, COMBO_EVENT = 4
static unified_event_t* keyevent_to_unified(keyevent_t* ke, unified_event_t* out) {
    switch (ke->type) {
        case KEY_EVENT:
            out->type = ke->pressed ? EVT_KEY_PRESS : EVT_KEY_RELEASE;
            break;
        case ENCODER_CW_EVENT:
            out->type = EVT_ENCODER_CW;
            break;
        case ENCODER_CCW_EVENT:
            out->type = EVT_ENCODER_CCW;
            break;
        case COMBO_EVENT:
            // Combo events are synthetic key events generated by the combo system.
            // Map them to EVT_COMBO_TRIGGER / EVT_COMBO_RELEASE so state machines
            // downstream can distinguish physical keys from combo outputs.
            out->type = ke->pressed ? EVT_COMBO_TRIGGER : EVT_COMBO_RELEASE;
            break;
        case TICK_EVENT:
        default:
            out->type = EVT_TICK;
            break;
    }
    out->pos = ke->key;
    out->timestamp = ke->time;
    out->pressed = ke->pressed;
    out->metadata = 0;
    return out;
}

// Main event dispatcher (refactored)
void action_exec(keyevent_t event) {
    // Convert to unified event
    unified_event_t unified_evt;
    keyevent_to_unified(&event, &unified_evt);
    
    // Output buffer
    action_output_t output = {0};
    
    // Debug logging
    #ifdef EVENT_DEBUG
    dprintf("EVENT: type=%d pressed=%d pos=(%d,%d)\n",
            unified_evt.type, unified_evt.pressed,
            unified_evt.pos.row, unified_evt.pos.col);
    #endif
    
    // Process through state machine pipeline
    state_result_t result = sm_process(sm_get_pipeline(), &unified_evt, &output);
    
    // Handle result
    switch (result) {
        case STATE_ACCEPT:
            // Execute action output
            execute_action_output(&output);
            break;
            
        case STATE_HOLD:
            // Event held for later resolution
            break;
            
        case STATE_TRANSFORM:
            // Recursively process transformed event
            action_exec(unified_to_keyevent((unified_event_t*)output));
            break;
            
        case STATE_REJECT:
            // Fall back to legacy processing if needed
            // For compatibility during migration
            break;
    }
}

// Execute action output
static void execute_action_output(action_output_t* out) {
    switch (out->type) {
        case ACTION_KEY_PRESS:
            register_code(out->data.keycode);
            break;
        case ACTION_KEY_RELEASE:
            unregister_code(out->data.keycode);
            break;
        case ACTION_MOD_PRESS:
            add_mods(out->data.mods);
            send_keyboard_report();
            break;
        case ACTION_MOD_RELEASE:
            del_mods(out->data.mods);
            send_keyboard_report();
            break;
        case ACTION_LAYER_ON:
            layer_on(out->data.layer);
            break;
        case ACTION_LAYER_OFF:
            layer_off(out->data.layer);
            break;
        case ACTION_LAYER_TOGGLE:
            layer_invert(out->data.layer);
            break;
        case ACTION_MACRO_START:
            macro_start(out->data.macro_id);
            break;
        default:
            break;
    }
}
```

#### 4.6.2 Modified keyboard_task()

```c
// Location: quantum/keyboard.c (refactored section)

void keyboard_task(void) {
    // Existing matrix scanning and event detection...
    // (unchanged)
    
    // NEW: Tick all state machines periodically
    sm_tick_all(sm_get_pipeline());
    
    // Existing quantum_task() and other tasks...
    // (unchanged)
}
```

### 4.7 Process Record Quantum Chain — Migration Strategy

The `process_record_quantum()` chain in `quantum.c` (lines 262-390) is where the
majority of QMK's feature processing lives. This chain calls ~25 `process_*`
functions in a specific boolean short-circuit order. Each function returns `true`
to continue processing or `false` to consume the event. This is the single most
complex part of the migration and must be handled explicitly.

#### 4.7.1 Current Chain (from quantum.c)

The current processing chain is a cascading `&&` expression:

```c
// quantum.c:262-390 (simplified)
bool process_record_quantum(keyrecord_t *record) {
    uint16_t keycode = get_record_keycode(record, true);
    
    // Pre-processing hooks
    if (!preprocess_secure(keycode, record)) return false;
    if (preprocess_tap_dance(keycode, record)) return false;
    
    // Main processing chain — short-circuit on false
    if (
        process_key_lock(&keycode, record) &&         // Key locking
        process_dynamic_macro(keycode, record) &&      // Dynamic macros
        process_last_key(keycode, record) &&           // Last key repeat
        process_repeat_key(keycode, record) &&         // Repeat key
        process_clicky(keycode, record) &&             // Audio clicky
        process_haptic(keycode, record) &&             // Haptic feedback
        process_record_via(keycode, record) &&         // VIA protocol
        process_auto_mouse(keycode, record) &&         // Auto mouse layer
        process_record_kb(keycode, record) &&          // Keyboard-level hook
        process_secure(keycode, record) &&             // Secure features
        process_sequencer(keycode, record) &&          // Sequencer
        process_midi(keycode, record) &&               // MIDI
        process_audio(keycode, record) &&              // Audio
        process_backlight(keycode, record) &&          // Backlight
        process_steno(keycode, record) &&              // Stenography
        process_music(keycode, record) &&              // Music mode
        process_caps_word(keycode, record) &&          // Caps word
        process_key_override(keycode, record) &&       // Key overrides
        process_tap_dance(keycode, record) &&          // Tap dance
        process_unicode_common(keycode, record) &&     // Unicode
        process_leader(keycode, record) &&             // Leader key
        process_auto_shift(keycode, record) &&         // Auto shift
        process_dynamic_tapping_term(keycode, record) && // Dynamic tapping term
        process_space_cadet(keycode, record) &&        // Space cadet
        process_magic(keycode, record) &&              // Magic keycodes
        process_grave_esc(keycode, record) &&          // Grave escape
        process_rgb(keycode, record) &&                // RGB lighting
        process_joystick(keycode, record) &&           // Joystick
        process_programmable_button(keycode, record) && // Programmable button
        process_autocorrect(keycode, record) &&        // Autocorrect
        process_tri_layer(keycode, record) &&          // Tri-layer
        true
    ) {
        // Default processing
    }
}
```

#### 4.7.2 Migration Categories

Each `process_*` function falls into one of three migration categories:

| Category | Description | Migration Approach |
|----------|-------------|-------------------|
| **Full SM** | Complex stateful features that benefit from explicit state machines | Convert to standalone state machine in pipeline |
| **Wrapper SM** | Stateless or simple features that work fine as-is | Wrap in thin SM adapter that delegates to existing `process_*` function |
| **Passthrough** | Features unrelated to key event processing | Keep in post-pipeline hook, no SM needed |

#### 4.7.3 Function-by-Function Migration Map

```
┌──────────────────────────────────────────────────────────────────────────────┐
│              PROCESS_RECORD_QUANTUM MIGRATION MAP                            │
├────────────────────────┬──────────┬──────────────────────────────────────────┤
│ Function               │ Category │ Migration Plan                           │
├────────────────────────┼──────────┼──────────────────────────────────────────┤
│                        │          │                                          │
│ FULL STATE MACHINE CONVERSION (Phase 2)                                     │
│ ───────────────────────────────────────                                      │
│ process_combo          │ Full SM  │ → combo_state SM (§4.5.2)               │
│ process_tap_dance      │ Full SM  │ → tap_dance_state SM (new)              │
│ process_leader         │ Full SM  │ → leader_state SM (new)                 │
│ process_auto_shift     │ Full SM  │ → auto_shift_state SM (new)             │
│ process_caps_word      │ Full SM  │ → caps_word_state SM (new)              │
│ process_key_override   │ Full SM  │ → key_override_state SM (new)           │
│ process_space_cadet    │ Full SM  │ → space_cadet_state SM (new)            │
│                        │          │                                          │
│ WRAPPER STATE MACHINE (Phase 3)                                             │
│ ───────────────────────────────────                                          │
│ process_key_lock       │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_dynamic_macro  │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_last_key       │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_repeat_key     │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_secure         │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_magic          │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_grave_esc      │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_unicode_common │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_autocorrect    │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_tri_layer      │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_sequencer      │ Wrapper  │ Thin SM: delegates to existing function  │
│ process_record_kb/user │ Wrapper  │ Thin SM: user/keyboard hooks             │
│ process_programmable_  │ Wrapper  │ Thin SM: delegates to existing function  │
│   button               │          │                                          │
│ process_dynamic_       │ Wrapper  │ Thin SM: delegates to existing function  │
│   tapping_term         │          │                                          │
│                        │          │                                          │
│ PASSTHROUGH (No SM needed)                                                  │
│ ───────────────────────────                                                  │
│ process_clicky         │ Pass     │ Post-pipeline hook (output side effect)  │
│ process_haptic         │ Pass     │ Post-pipeline hook (output side effect)  │
│ process_record_via     │ Pass     │ Post-pipeline hook (VIA protocol)        │
│ process_auto_mouse     │ Pass     │ Post-pipeline hook (mouse layer logic)   │
│ process_midi           │ Pass     │ Post-pipeline hook (MIDI output)         │
│ process_audio          │ Pass     │ Post-pipeline hook (audio output)        │
│ process_backlight      │ Pass     │ Post-pipeline hook (LED control)         │
│ process_steno          │ Pass     │ Post-pipeline hook (steno protocol)      │
│ process_music          │ Pass     │ Post-pipeline hook (music mode)          │
│ process_rgb            │ Pass     │ Post-pipeline hook (RGB control)         │
│ process_joystick       │ Pass     │ Post-pipeline hook (joystick output)     │
│                        │          │                                          │
└────────────────────────┴──────────┴──────────────────────────────────────────┘
```

#### 4.7.4 Wrapper SM Pattern

For features that don't need full state machine conversion, a thin wrapper
preserves existing behavior while plugging into the new pipeline:

```c
// Location: quantum/sm_wrapper.h
// Generic wrapper that adapts a process_*() function to a state machine

typedef bool (*process_fn_t)(uint16_t keycode, keyrecord_t *record);

typedef struct {
    process_fn_t process_fn;   ///< Existing process_* function pointer
} sm_wrapper_state_t;

static state_result_t sm_wrapper_handler(state_machine_t* sm,
                                          unified_event_t* event, void* output) {
    sm_wrapper_state_t* state = (sm_wrapper_state_t*)sm->state_data;
    
    // Skip non-key events
    if (event->type != EVT_KEY_PRESS && event->type != EVT_KEY_RELEASE) {
        return STATE_REJECT;
    }
    
    // Convert unified event back to keyrecord_t for legacy function
    keyrecord_t record = unified_to_keyrecord(event);
    uint16_t keycode = get_keycode_for_pos(event->pos);
    
    // Call legacy function: returns true = "continue", false = "consumed"
    bool continue_processing = state->process_fn(keycode, &record);
    
    if (!continue_processing) {
        // Legacy function consumed the event — it handled its own output
        // (e.g., called register_code() directly). Signal acceptance.
        return STATE_ACCEPT;
    }
    
    // Legacy function passed — let pipeline continue
    return STATE_REJECT;
}

// Convenience macro to create a wrapper SM for an existing process_* function
#define DEFINE_WRAPPER_SM(name, fn) \
    static sm_wrapper_state_t g_##name##_wrapper = { .process_fn = (fn) }; \
    static state_machine_t sm_##name = { \
        .name = #name, \
        .state_data = &g_##name##_wrapper, \
        .state_size = sizeof(sm_wrapper_state_t), \
        .handler = sm_wrapper_handler, \
        .tick_handler = NULL, \
        .cleanup_fn = NULL, \
    };
```

Usage example:
```c
// Wrap process_key_lock in a state machine
DEFINE_WRAPPER_SM(key_lock, process_key_lock)

// Wrap process_dynamic_macro in a state machine
DEFINE_WRAPPER_SM(dynamic_macro, process_dynamic_macro)

// Registration preserves original processing order
void register_legacy_sm_chain(void) {
    sm_register(&sm_key_lock);
    sm_register(&sm_dynamic_macro);
    // ... register in original chain order
}
```

#### 4.7.5 Post-Pipeline Hooks

Features that produce side effects (haptic, audio, RGB, etc.) don't need to
participate in event consumption decisions. They observe the final pipeline
output without intercepting events:

```c
// Location: quantum/event_state.h

typedef void (*post_pipeline_hook_fn)(unified_event_t* event, action_output_t* output);

// Register a post-pipeline observer (called after pipeline produces output)
void sm_register_post_hook(post_pipeline_hook_fn hook);

// Location: quantum/event_state.c
#define SM_MAX_POST_HOOKS 16
static post_pipeline_hook_fn g_post_hooks[SM_MAX_POST_HOOKS];
static uint8_t g_post_hook_count = 0;

void sm_register_post_hook(post_pipeline_hook_fn hook) {
    if (g_post_hook_count < SM_MAX_POST_HOOKS) {
        g_post_hooks[g_post_hook_count++] = hook;
    }
}

// Called after sm_process() produces STATE_ACCEPT
static void sm_run_post_hooks(unified_event_t* event, action_output_t* output) {
    for (uint8_t i = 0; i < g_post_hook_count; i++) {
        g_post_hooks[i](event, output);
    }
}
```

#### 4.7.6 Pipeline Registration Order

The SM pipeline registration order must preserve the semantic ordering of the
current `process_record_quantum()` chain. Events flow through state machines in
registration order, and `STATE_ACCEPT` stops propagation (like returning `false`
from a `process_*` function):

```c
void event_state_init(void) {
    // Phase 2: Full state machine conversions (event consumers)
    combo_state_init();          // was: process_combo
    taphold_state_init();        // was: action_tapping_process
    tap_dance_state_init();      // was: process_tap_dance
    leader_state_init();         // was: process_leader
    auto_shift_state_init();     // was: process_auto_shift
    caps_word_state_init();      // was: process_caps_word
    key_override_state_init();   // was: process_key_override
    space_cadet_state_init();    // was: process_space_cadet
    
    // Phase 3: Wrapper SMs (preserve existing process_* functions)
    register_legacy_sm_chain();
    
    // Phase 3: Post-pipeline hooks (side-effect observers)
    sm_register_post_hook(haptic_post_hook);
    sm_register_post_hook(audio_post_hook);
    sm_register_post_hook(rgb_post_hook);
    // ... etc.
}
```

> **Design note — Why not convert everything to a full state machine:**
> Converting all ~25 `process_*` functions to full state machines at once is
> neither practical nor necessary. Many features (key lock, dynamic macro,
> grave escape, etc.) are essentially stateless transformations that work
> correctly as-is. The wrapper pattern lets us integrate them into the pipeline
> immediately while preserving their existing, tested implementations.
> Full SM conversion is reserved for features with complex temporal state
> (combos, tap-hold, tap-dance, leader, auto-shift) where the explicit
> state machine model provides the most architectural benefit.
>
> The incremental migration path is:
> 1. Build core framework + full SMs for combo, tap-hold
> 2. Wrap remaining `process_*` functions with `DEFINE_WRAPPER_SM`
> 3. Replace the `process_record_quantum()` chain with pipeline dispatch
> 4. Over time, convert wrappers to full SMs where beneficial

---

## 5. Implementation Plan

> **Timeline note:** The v1.0 draft estimated 9 weeks. After review, this was
> revised to ~25 weeks to account for the complexity of the `process_record_quantum()`
> chain migration, multi-key tap-hold edge cases, integration testing across
> the full matrix of feature combinations, and split keyboard considerations.
> A prototype phase is added before the full implementation to validate core
> design decisions.

### 5.0 Phase 0: Prototype (Estimated: 3 weeks)

| Task | Description | Priority |
|------|-------------|----------|
| 0.1 | Build minimal core framework (event types, pipeline, sm_process) | P0 |
| 0.2 | Implement tap-hold SM prototype with multi-key support | P0 |
| 0.3 | Validate on real hardware (Keychron Q3 Max or similar) | P0 |
| 0.4 | Measure actual memory and timing overhead | P0 |
| 0.5 | Revise design based on prototype findings | P0 |

> The prototype surfaces real-world issues (interrupt timing, USB report
> latency, memory pressure) before committing to the full implementation.
> The tap-hold SM is chosen because it exercises the most complex interactions
> (multi-key, timer-based, event buffering, event replay).

### 5.1 Phase 1: Core Framework (Estimated: 3 weeks)

| Task | Description | Priority |
|------|-------------|----------|
| 1.1 | Create `event_state.h` with type definitions | P0 |
| 1.2 | Implement `event_state.c` with pipeline operations | P0 |
| 1.3 | Create `event_sequence.h/c` with pattern matching | P0 |
| 1.4 | Implement `sm_wrapper.h` for legacy function adapters | P0 |
| 1.5 | Implement post-pipeline hook system | P1 |
| 1.6 | Add unit tests for core framework | P0 |
| 1.7 | Add documentation and examples | P1 |

### 5.2 Phase 2: Full State Machine Implementations (Estimated: 8 weeks)

Each feature gets its own sub-phase with dedicated integration testing:

| Task | Description | Estimate | Priority |
|------|-------------|----------|----------|
| 2.1 | Tap-hold SM (multi-key, PERMISSIVE_HOLD, HOLD_ON_OTHER_KEY_PRESS, RETRO_TAPPING, QUICK_TAP_TERM, per-key variants) | 3 weeks | P0 |
| 2.2 | Combo SM (multi-candidate, event replay) | 2 weeks | P0 |
| 2.3 | Tap dance SM | 1 week | P1 |
| 2.4 | Leader SM | 0.5 weeks | P1 |
| 2.5 | Auto shift SM | 0.5 weeks | P1 |
| 2.6 | Caps word SM | 0.5 weeks | P1 |
| 2.7 | Key override SM | 0.5 weeks | P1 |

> **Why tap-hold gets 3 weeks:** The current `action_tapping.c` is 547 lines
> with ~10 configuration variants that interact in subtle ways. Home row mods
> with overlapping key presses and rapid typing expose edge cases that take
> time to validate.

### 5.3 Phase 3: Legacy Integration (Estimated: 4 weeks)

| Task | Description | Priority |
|------|-------------|----------|
| 3.1 | Wrap remaining `process_*` functions with `DEFINE_WRAPPER_SM` | P0 |
| 3.2 | Replace `process_record_quantum()` chain with pipeline dispatch | P0 |
| 3.3 | Implement post-pipeline hooks for haptic/audio/RGB/etc. | P0 |
| 3.4 | Modify `action_exec()` to use state machines | P0 |
| 3.5 | Integrate with `keyboard_task()` | P0 |
| 3.6 | Add configuration options | P1 |
| 3.7 | Performance optimization and profiling | P2 |

### 5.4 Phase 4: Split Keyboard Support (Estimated: 2 weeks)

| Task | Description | Priority |
|------|-------------|----------|
| 4.1 | Define serialization format for `unified_event_t` over split link | P0 |
| 4.2 | Determine master-side vs slave-side SM execution policy | P0 |
| 4.3 | Implement event forwarding from slave to master | P0 |
| 4.4 | Test split keyboard combos, tap-hold across halves | P0 |

> See §10 Appendix E for detailed split keyboard considerations.

### 5.5 Phase 5: Migration & Testing (Estimated: 5 weeks)

| Task | Description | Priority |
|------|-------------|----------|
| 5.1 | Migrate existing combo implementation | P0 |
| 5.2 | Migrate existing tap dance implementation | P0 |
| 5.3 | Comprehensive integration testing (feature combinations matrix) | P0 |
| 5.4 | Test against popular keymaps (home row mods, combos, leaders) | P0 |
| 5.5 | Performance regression testing | P0 |
| 5.6 | Update documentation | P1 |
| 5.7 | Deprecation warnings for old APIs | P1 |

### 5.6 File Structure

```
quantum/
├── event_state.h           # Core type definitions + pipeline API
├── event_state.c           # Pipeline implementation (sm_process, sm_tick_all)
├── event_sequence.h        # Sequence pattern API
├── event_sequence.c        # Sequence pattern implementation
├── action_output.h         # Action output types
├── action_output.c         # Action execution (execute_action_output)
├── sm_wrapper.h            # DEFINE_WRAPPER_SM macro + wrapper pattern
│
├── process_taphold_state.c # Tap-hold SM (multi-key pooled slots)
├── process_combo_state.c   # Combo SM (multi-candidate + replay)
├── process_tap_dance_state.c # Tap dance SM
├── process_leader_state.c  # Leader SM
├── process_auto_shift_state.c # Auto shift SM
├── process_caps_word_state.c  # Caps word SM
├── process_key_override_state.c # Key override SM
├── process_space_cadet_state.c  # Space cadet SM
│
└── tests/
    ├── test_event_state.c
    ├── test_sequence.c
    ├── test_taphold_state.c
    ├── test_combo_state.c
    ├── test_sm_wrapper.c
    └── ...
```

---

## 6. API Reference

### 6.1 Event Creation

| Function/Macro | Description |
|----------------|-------------|
| `EVT_KEY(pos, pressed)` | Create key event |
| `EVT_TICK()` | Create tick event |
| `EVT_ENCODER(id, dir, pressed)` | Create encoder event |

### 6.2 State Machine Registration

| Function | Description |
|----------|-------------|
| `sm_register(sm)` | Register state machine in pipeline |
| `sm_unregister(sm)` | Remove state machine from pipeline |
| `sm_get_pipeline()` | Get pipeline head |

### 6.3 Event Processing

| Function | Description |
|----------|-------------|
| `sm_process(head, event, output)` | Process event through pipeline |
| `sm_tick_all(head)` | Send tick to all state machines |

### 6.4 Sequence Matching

| Function | Description |
|----------|-------------|
| `seq_init(seq, steps, len, timeout)` | Initialize sequence pattern with steps |
| `seq_init_strict(seq, steps, len, timeout)` | Initialize strict sequence |
| `seq_match(seq, event)` | Match event against pattern (type + key identity) |
| `seq_timeout(seq)` | Check if sequence timed out |
| `seq_reset(seq)` | Reset sequence matcher |
| `seq_progress(seq)` | Get current progress |

---

## 7. Examples

### 7.1 Creating a Custom Feature

```c
// Example: Triple-tap Escape key triggers special action

#include "event_state.h"

typedef struct {
    uint8_t tap_count;
    uint16_t last_tap_time;
} triple_tap_state_t;

static triple_tap_state_t g_triple_tap = {0};

static state_result_t triple_tap_handler(state_machine_t* sm, 
                                         unified_event_t* event, void* output) {
    triple_tap_state_t* state = (triple_tap_state_t*)sm->state_data;
    
    // Only monitor escape key
    if (!IS_ESCAPE_KEY(event->pos)) return STATE_REJECT;
    
    if (event->pressed) {
        // Check if within tap window (300ms)
        if (state->last_tap_time && timer_elapsed(state->last_tap_time) < 300) {
            state->tap_count++;
            
            if (state->tap_count == 3) {
                // Triple tap! Execute special action
                action_output_t* out = (action_output_t*)output;
                out->type = ACTION_MACRO_START;
                out->data.macro_id = MACRO_TRIPLE_ESCAPE;
                state->tap_count = 0;
                return STATE_ACCEPT;
            }
        } else {
            state->tap_count = 1;
        }
        state->last_tap_time = event->timestamp;
    }
    
    return STATE_REJECT;
}

static state_machine_t sm_triple_tap = {
    .name = "triple_tap_escape",
    .state_data = &g_triple_tap,
    .state_size = sizeof(triple_tap_state_t),
    .handler = triple_tap_handler,
    .tick_handler = NULL,
};

void triple_tap_init(void) {
    sm_register(&sm_triple_tap);
}
```

### 7.2 Using Sequence Pattern Matching

```c
// Example: Detect specific key sequence "press A, press B, release A, release B"

sequence_step_t my_sequence[] = {
    { .type = EVT_KEY_PRESS,   .keycode = KC_A, .pos = KEYPOS_ANY },
    { .type = EVT_KEY_PRESS,   .keycode = KC_B, .pos = KEYPOS_ANY },
    { .type = EVT_KEY_RELEASE, .keycode = KC_A, .pos = KEYPOS_ANY },
    { .type = EVT_KEY_RELEASE, .keycode = KC_B, .pos = KEYPOS_ANY }
};

sequence_pattern_t my_pattern;

void init_sequence_detector(void) {
    seq_init(&my_pattern, my_sequence, 4, 500); // 500ms timeout
}

state_result_t sequence_handler(state_machine_t* sm, 
                                unified_event_t* event, void* output) {
    sequence_result_t match = seq_match(&my_pattern, event);
    
    switch (match) {
        case SEQ_MATCH_COMPLETE:
            // Pattern detected! (A+B pressed then released in order)
            seq_reset(&my_pattern);
            // Execute action
            return STATE_ACCEPT;
            
        case SEQ_MATCH_PARTIAL:
            // Pattern in progress
            return STATE_HOLD;
            
        case SEQ_MATCH_FAILED:
            // Timeout or mismatch
            seq_reset(&my_pattern);
            return STATE_REJECT;
            
        default:
            return STATE_REJECT;
    }
}
```

```c
// Example: Wildcard sequence — detect any three rapid key presses
sequence_step_t triple_tap[] = {
    { .type = EVT_KEY_PRESS, .keycode = KC_WILDCARD, .pos = KEYPOS_ANY },
    { .type = EVT_KEY_PRESS, .keycode = KC_WILDCARD, .pos = KEYPOS_ANY },
    { .type = EVT_KEY_PRESS, .keycode = KC_WILDCARD, .pos = KEYPOS_ANY }
};
```

---

## 8. Migration Guide

### 8.1 For Feature Developers

#### Before (Legacy Combo):
```c
// Old combo implementation
if (combo_pressed) {
    register_code(combo_keycode);
}
```

#### After (State Machine):
```c
// New combo state machine
static state_result_t combo_handler(...) {
    // ... state machine logic ...
    action_output_t* out = (action_output_t*)output;
    out->type = ACTION_KEY_PRESS;
    out->data.keycode = combo_keycode;
    return STATE_ACCEPT;
}
```

### 8.2 For Keyboard Maintainers

No changes required! The new system is backward compatible. Existing keymaps continue to work without modification.

### 8.3 For Users

No changes required! All existing features continue to work. New features can be added more easily.

---

## 9. Testing Strategy

### 9.1 Unit Tests

```c
// test_event_state.c

void test_seq_init(void) {
    sequence_step_t steps[] = {
        { .type = EVT_KEY_PRESS,   .keycode = KC_A, .pos = KEYPOS_ANY },
        { .type = EVT_KEY_RELEASE, .keycode = KC_A, .pos = KEYPOS_ANY }
    };
    sequence_pattern_t pattern;
    
    seq_init(&pattern, steps, 2, 100);
    
    assert_eq(pattern.length, 2);
    assert_eq(pattern.steps[0].type, EVT_KEY_PRESS);
    assert_eq(pattern.steps[0].keycode, KC_A);
    assert_eq(pattern.steps[1].type, EVT_KEY_RELEASE);
    assert_eq(pattern.timeout_ms, 100);
}

void test_seq_match_complete(void) {
    sequence_step_t steps[] = {
        { .type = EVT_KEY_PRESS,   .keycode = KC_A, .pos = KEYPOS_ANY },
        { .type = EVT_KEY_RELEASE, .keycode = KC_A, .pos = KEYPOS_ANY }
    };
    sequence_pattern_t pattern;
    
    seq_init(&pattern, steps, 2, 100);
    
    unified_event_t press = EVT_KEY(MAKE_KEYPOS(0,0), true);
    unified_event_t release = EVT_KEY(MAKE_KEYPOS(0,0), false);
    // Mock get_keycode_for_pos to return KC_A for (0,0)
    
    assert_eq(seq_match(&pattern, &press), SEQ_MATCH_PARTIAL);
    assert_eq(seq_match(&pattern, &release), SEQ_MATCH_COMPLETE);
}

void test_seq_match_wrong_key(void) {
    sequence_step_t steps[] = {
        { .type = EVT_KEY_PRESS, .keycode = KC_A, .pos = KEYPOS_ANY }
    };
    sequence_pattern_t pattern;
    
    seq_init(&pattern, steps, 1, 100);
    
    unified_event_t press_b = EVT_KEY(MAKE_KEYPOS(0,1), true);
    // Mock get_keycode_for_pos to return KC_B for (0,1)
    
    assert_eq(seq_match(&pattern, &press_b), SEQ_MATCH_NONE);
}

void test_seq_timeout(void) {
    sequence_step_t steps[] = {
        { .type = EVT_KEY_PRESS, .keycode = KC_WILDCARD, .pos = KEYPOS_ANY }
    };
    sequence_pattern_t pattern;
    
    seq_init(&pattern, steps, 1, 10);
    
    // Simulate time passage
    // ... (timer mocking)
    
    assert_true(seq_timeout(&pattern));
}

// Test that STATE_ACCEPT stops pipeline propagation
void test_accept_stops_propagation(void) {
    // SM_A accepts all events, SM_B should never see them
    static bool sm_b_called = false;
    
    state_result_t sm_a_handler(state_machine_t* sm, unified_event_t* e, void* o) {
        return STATE_ACCEPT;
    }
    state_result_t sm_b_handler(state_machine_t* sm, unified_event_t* e, void* o) {
        sm_b_called = true;
        return STATE_REJECT;
    }
    
    // Register SM_A before SM_B
    state_machine_t a = { .handler = sm_a_handler };
    state_machine_t b = { .handler = sm_b_handler };
    sm_register(&a);
    sm_register(&b);
    
    unified_event_t evt = EVT_KEY(MAKE_KEYPOS(0,0), true);
    action_output_t out = {0};
    state_result_t result = sm_process(sm_get_pipeline(), &evt, &out);
    
    assert_eq(result, STATE_ACCEPT);
    assert_false(sm_b_called);  // SM_B must NOT have been called
}

// Test transform depth limit
void test_transform_depth_limit(void) {
    // SM that always transforms — should hit depth limit, not stack overflow
    state_result_t always_transform(state_machine_t* sm, unified_event_t* e, void* o) {
        memcpy(o, e, sizeof(unified_event_t));  // "transform" to same event
        return STATE_TRANSFORM;
    }
    
    state_machine_t looper = { .handler = always_transform };
    sm_register(&looper);
    
    unified_event_t evt = EVT_KEY(MAKE_KEYPOS(0,0), true);
    action_output_t out = {0};
    state_result_t result = sm_process(sm_get_pipeline(), &evt, &out);
    
    assert_eq(result, STATE_REJECT);  // Depth limit reached → reject
    // No stack overflow occurred
}
```

### 9.2 Integration Tests

```c
// test_combo_integration.c

void test_combo_detection(void) {
    // Set up combo: A+B → C
    combo_init();
    register_combo(KEY_A, KEY_B, KEY_C, 100);
    
    // Press A
    unified_event_t a_press = EVT_KEY(KEY_A_POS, true);
    sm_process(sm_get_pipeline(), &a_press, &output);
    
    // Press B
    unified_event_t b_press = EVT_KEY(KEY_B_POS, true);
    state_result_t result = sm_process(sm_get_pipeline(), &b_press, &output);
    
    // Should produce C press
    assert_eq(result, STATE_ACCEPT);
    assert_eq(output.type, ACTION_KEY_PRESS);
    assert_eq(output.data.keycode, KEY_C);
}
```

### 9.3 Performance Tests

```c
// benchmark_event_processing.c

void benchmark_sm_pipeline(void) {
    uint32_t start = timer_read_raw();
    
    for (int i = 0; i < 10000; i++) {
        unified_event_t evt = EVT_KEY(MAKE_KEYPOS(0,0), true);
        sm_process(sm_get_pipeline(), &evt, &output);
    }
    
    uint32_t elapsed = timer_elapsed_raw(start);
    printf("Pipeline processing: %lu us per event\n", elapsed / 10000);
}
```

---

## 10. Appendices

### Appendix A: Memory Footprint Analysis

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| `unified_event_t` | 8 | Base event structure (no void* pointer) |
| `state_machine_t` | 28 | Per-feature overhead (no void* context in events) |
| `sequence_step_t` | 6 | Per step in a sequence pattern |
| `sequence_pattern_t` | 56 | Per-pattern (8 steps × 6 + overhead) |
| Tap-hold state (pooled) | 120 | 8 slots × 12 bytes + waiting buffer |
| Combo state (multi-candidate) | 144 | 8 candidates + held event buffer |
| SM wrapper state | 4 | Per wrapped legacy function |
| Post-pipeline hooks | 64 | 16 hooks × 4-byte function pointer |
| **Total overhead** | **~500** | For typical feature set with all SMs |

> Memory increase from v1.0 (~200 bytes) due to multi-key tap-hold pool,
> multi-candidate combo tracking, and event replay buffers. This is still
> well within QMK's 2KB NFR-1 budget and comparable to the memory used by
> the current `waiting_buffer` + `combo_buffer` in existing code.

### Appendix B: Timing Budget

| Stage | Max Time | Notes |
|-------|----------|-------|
| Event creation | 5 μs | Simple struct construction |
| State machine dispatch | 10 μs per SM | Function call + switch |
| Sequence matching | 5 μs per pattern | Array comparison |
| **Total per event** | **<50 μs** | With 3-4 state machines |

### Appendix C: Compatibility Matrix

| Feature | Legacy Supported | New SM Available | Migration Approach |
|---------|------------------|------------------|--------------------|
| Tap-hold | Yes | Yes (multi-key pool) | Full SM (Phase 2) |
| Combos | Yes | Yes (multi-candidate) | Full SM (Phase 2) |
| Tap dance | Yes | Planned | Full SM (Phase 2) |
| Leader | Yes | Planned | Full SM (Phase 2) |
| Auto shift | Yes | Planned | Full SM (Phase 2) |
| Caps word | Yes | Planned | Full SM (Phase 2) |
| Key override | Yes | Planned | Full SM (Phase 2) |
| Space cadet | Yes | Planned | Full SM (Phase 2) |
| Key lock | Yes | Wrapper SM | Wrapper (Phase 3) |
| Dynamic macro | Yes | Wrapper SM | Wrapper (Phase 3) |
| Grave escape | Yes | Wrapper SM | Wrapper (Phase 3) |
| Haptic | Yes | Post-hook | Passthrough (Phase 3) |
| RGB | Yes | Post-hook | Passthrough (Phase 3) |
| Audio | Yes | Post-hook | Passthrough (Phase 3) |

### Appendix D: Troubleshooting

**Q: Events not being processed?**
- Check state machine registration order
- Verify handler returns correct state result
- Use `EVENT_DEBUG` for logging

**Q: Memory usage too high?**
- Review state machine count
- Reduce `TAPHOLD_MAX_CONCURRENT` or `COMBO_MAX_CANDIDATES`
- Reduce sequence pattern lengths
- Consider lazy initialization

**Q: Feature not triggering?**
- Check timing parameters
- Verify event types match expectations
- Use sequence progress debugging
- Check if an earlier SM in the pipeline is consuming (STATE_ACCEPT) the event

### Appendix E: Split Keyboard Considerations

Split keyboards (e.g., Keychron Q11, Sofle, Corne) run firmware on both halves
with a serial or I2C link between them. The event processing architecture must
account for this topology.

#### E.1 Current Split Architecture

In the current QMK split design:
- The **slave** half scans its matrix and sends raw matrix state to the master
- The **master** half merges both matrices and runs all event processing
- Features like combos, tap-hold, and layers execute only on the master

#### E.2 Impact on State Machine Pipeline

The SM pipeline should continue to execute only on the master half. This means:

1. **`unified_event_t` serialization is NOT needed** for the basic case — the
   slave sends raw matrix state, and the master creates `unified_event_t` from
   the merged matrix (same as current behavior).

2. **State machines see events from both halves** with no special handling —
   key positions include the half identifier (row offsets for slave half).

3. **Combos across halves work correctly** because the master processes all
   events from both matrices in a single pipeline.

#### E.3 Future: Slave-Side Processing

If future optimization moves some processing to the slave (e.g., debouncing),
the `unified_event_t` would need serialization over the split link:

```c
// Potential serialization format (8 bytes, matching unified_event_t)
typedef struct {
    uint8_t  type;        // event_type_t (1 byte with -fshort-enums)
    uint8_t  row;         // keypos_t.row
    uint8_t  col;         // keypos_t.col
    uint8_t  flags;       // pressed (bit 0) + metadata (bits 1-7)
    uint16_t timestamp;   // relative to sync epoch
    uint16_t _reserved;   // padding / future use
} split_event_wire_t;

// The natural field sizes of unified_event_t (no packed attribute) make
// serialization straightforward without alignment concerns.
```

#### E.4 Testing Considerations

Split keyboard testing requires:
- Combo detection across halves (keys on left + right = combo)
- Tap-hold interruption across halves (tap-hold on left, interrupt from right)
- Layer switching on one half affecting keymap on both halves
- Latency measurement of the split link under event processing load

---

## Change Log

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2024-01-15 | Initial proposal |
| 2.0 | 2026-04-11 | Post-review revision: fixed 5 critical bugs (recursive stack overflow, STATE_ACCEPT propagation, single-instance tap-hold/combo, missing COMBO_EVENT handling, void* in event struct), added multi-key tap-hold pool, multi-candidate combo with event replay, key-aware sequence matching, process_record_quantum() migration strategy with wrapper SM pattern, split keyboard considerations, revised timeline from 9→25 weeks |

---

## Authors

- QMK Architecture Team

---

## License

This document is part of QMK and licensed under GPL v2.
