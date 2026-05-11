# Key Processing Pipeline — Outcome & Lessons

> Branch: `refactor/key-processing-sm`
> Status: Pipeline orchestrator shipped, vim modal demo as proof-of-concept
> See also: [original design](2026-05-10-key-processing-statesmith-design.md) and [original impl plan](2026-05-10-key-processing-statesmith-impl.md) (now of historical interest only — the plan changed during execution)

---

## What was originally planned

The original design (linked above) proposed wholesale replacement of QMK's combo, tap dance, and leader processors with StateSmith-generated state machines wrapped in adapters. The goal was a uniform API where every key processing feature is an SM.

## What actually shipped

A much smaller, more focused outcome:

1. **Pipeline orchestrator** (`quantum/pipeline.c/h`) — uniform plugin interface (`sm_machine_t`), phase-based dispatch (PRE_TAP → POST_TAP → POST_EXEC), priority ordering, consume/pass semantics. ~150 LOC.

2. **Vim modal layer demo** (`quantum/features/vim_modal_*`) — proof that the pipeline + SM combination works when the feature genuinely needs state machine modeling. Real 5-state machine driving real behavior dispatch.

3. **Existing QMK features (combo, tap dance, leader) untouched** — after migrating them to SMs we discovered the SMs were dead weight and reverted.

## Why we pivoted

During implementation we built SM adapters for combo, tap dance, and leader. Code review revealed:

- The SMs were **shadow trackers** — the adapters maintained richer state (`active`, `count`, `committed`, `has_match`, `seq_len`) and made all real decisions from those flags
- Three `consumed`/`committed`/`matched` states were **unreachable** — no transition entered them
- The only place SM state was actually read was a dead-code branch
- ~1000 generated LOC + RAM overhead for **zero observable benefit**

The features were too small (3–4 states, dominated by data not state) to justify SM machinery. We reset the branch back to just the pipeline + design docs.

## Key insight: pipeline ≠ state machine

The pipeline orchestrator is the architectural contribution. It provides:
- Uniform plugin interface (`sm_machine_t`)
- Explicit phase ordering with barriers (PRE_TAP / POST_TAP / POST_EXEC)
- Composable feature registration
- Consume/pass semantics

**Whether each registered feature uses an SM internally is an implementation detail.** A feature can register a plain-C handler or an SM-driven handler — the pipeline doesn't care.

## When to use state machines (guidance)

| Feature shape | Use SM? | Why |
|---------------|---------|-----|
| **>5 distinct states with different behavior** | ✅ Yes | Real state explosion, transitions matter |
| **Hierarchical states** (sub-modes) | ✅ Yes | SM frameworks model this naturally |
| **Modal editors** (vim, helix) | ✅ Yes | Each mode = different key dispatch |
| **Macro recorder/player** | ✅ Yes | Idle/recording/playing with bounded buffer |
| **Multi-device wireless management** | ✅ Yes | Invalid transitions matter (can't be on USB+BT simultaneously) |
| **Sequence matching with timeout** (leader) | ❌ No | Data-driven; flag + buffer beats SM |
| **Single-flag features** (caps word, key lock) | ❌ No | One bool. SM is overkill |
| **Keycode translators** | ❌ No | `if (kc == X) do_y()`. No state. |
| **Timer-based features** (tap dance, auto-shift) | ❌ No | Data dominates; SM tracks one boolean phase |

Rule of thumb: if the adapter would need 3+ tracking flags to mirror the SM's state, the SM probably isn't earning its keep.

## Architecture (final)

```
┌──────────────────────────────────────────────────────────────┐
│ QMK Core Processing Chain  (untouched)                        │
│  process_combo → process_tap_dance → process_leader → ...     │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│ Pipeline Orchestrator  (new — composable hooks)               │
│  PRE_TAP → POST_TAP → POST_EXEC                               │
│  Each phase: sequential dispatch by priority, short-circuit   │
│              on SM_CONSUME                                    │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│ User Features  (extensible — plain C or SM, your choice)     │
│  vim_modal (SM — 5 modes warrant it)                          │
│  [future features register here]                              │
└──────────────────────────────────────────────────────────────┘
```

## Files shipped

| File | Purpose | LOC |
|------|---------|-----|
| `quantum/pipeline.h` | Pipeline interface (`sm_machine_t`, phases, result enum) | 40 |
| `quantum/pipeline.c` | Dispatcher with priority-sorted registration | 75 |
| `quantum/rules.mk` | Build rules + StateSmith generation | 25 |
| `quantum/features/vim_modal.puml` | Vim modal SM diagram | 30 |
| `quantum/features/VimModal.c/h` | Generated state machine | ~280 |
| `quantum/features/vim_modal_adapter.c/h` | Per-mode key translation | ~170 |

Integration hooks:
- `quantum/action.c` — `pipeline_process_pre_tap()` call in `action_exec()` (guarded by `KEY_PROCESSING_SM_ENABLE`)
- `quantum/keyboard.c` — `pipeline_tick()` in `keyboard_task()`, machine registration in `keyboard_post_init_quantum()`
- `builddefs/common_features.mk` — wires `quantum/rules.mk` into the build

## Build flags

```makefile
KEY_PROCESSING_SM_ENABLE = yes   # enable pipeline
VIM_MODAL_ENABLE = yes           # enable vim modal demo
```

## What was NOT done (deliberately)

- ❌ Replace combo/tap dance/leader with SMs — SMs added no value
- ❌ Migrate QMK's 30+ feature processors — most aren't state-shaped
- ❌ Touch QMK's tap-hold resolution — 10+ years of edge cases baked in
- ❌ Break existing keymaps/VIA — backward compat preserved

## Open questions / future work

1. **Are there other features that warrant SMs?** Wireless mode manager (USB/BT1/BT2/BT3/2.4G/pairing) is a strong candidate
2. **Should the pipeline support POST_EXEC observers?** Currently defined but no demo uses it
3. **Should pipeline_reset be wired to layer change?** Currently exists but never called automatically
4. **StateSmith generation as Make dependency** — currently regen is manual via `make statesmith-gen`; should it be automatic on `.puml` change?

## Lessons

1. **The pipeline orchestrator was the real win** — small (~150 LOC), uniform, composable. Worth the effort.
2. **State machines are a tool, not a paradigm** — applying them to every feature is cargo-culting. Apply them where they pay off.
3. **Generated code is liability if unused** — the dead `consumed`/`committed` states in our original combo/tap dance/leader SMs proved this. Verify your SM is actually driving behavior, not shadow-tracking.
4. **Honest code review matters** — the critical review of the SM integration prevented shipping 1000 lines of dead weight.
