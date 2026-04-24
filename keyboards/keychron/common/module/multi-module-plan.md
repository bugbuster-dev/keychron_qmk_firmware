# Multi-Module Hook Support Plan

## Goals

1. Multiple modules can coexist, each hooking QMK callbacks (combo,
   tap_dance, leader, process_record_user, layer_state_set).
2. Shared hooks chain in slot order with early-exit semantics for
   bool-returning hooks.
3. Sector-preserving reload stays coherent even when sibling modules
   share hooks.
4. Each module gets correct per-module context in its callbacks.

## Current limitation

The first iteration of the module loader assumes one-hook-one-owner:
`g_module_hooks[MODULE_HOOK_MAX]` has a single `func`/`module_id` entry
per hook. Attempting to load a second module that claims an already-
claimed hook is rejected with a conflict error.

Observed failure during sector-preserving reload: loading slot 0 succeeds
and claims its hooks. Loading slot 1 in the same sector (valid use case —
two independent modules with distinct responsibilities) fails the
conflict check and returns finalize error 1. The current workaround
(skip sibling cleanup during reload, see `module_loader.c`) avoids
destroying freshly-written siblings but doesn't address hook chaining.

## Design

### Phase 1 — Hook table redesign (firmware)

Replace the single-owner table with a per-hook × per-slot matrix:

```c
typedef struct {
    void*   func;       // NULL = not claimed
    uint8_t slot_id;    // owner slot (for release / deinit)
} module_hook_slot_t;

// [hook_index][chain_position], chain walked in ascending slot order
static module_hook_slot_t g_module_hooks[MODULE_HOOK_MAX][MODULE_FLASH_SLOT_COUNT];
```

Memory cost: `MODULE_HOOK_MAX=32` × `MODULE_FLASH_SLOT_COUNT=8` × 8 bytes
= 2 KB RAM. Acceptable on STM32F4.

New API:

```c
// Iterator over all modules claiming a given hook (slot-ascending order).
typedef struct { uint8_t hook; uint8_t idx; } module_hook_iter_t;
bool module_hook_iter_next(module_hook_iter_t* it,
                            uint8_t* slot_id_out, void** func_out);

// Additive claim (no conflict unless same slot already has this hook).
static bool claim_hook(uint8_t slot_id, uint32_t hook_index, void* func);

// Release everything a slot owns (used by module_unload).
static void release_all_hooks_for_slot(uint8_t slot_id);
```

Remove hook-conflict rejection from `module_load()` and
`module_boot_scan()`. Multiple modules on the same hook is now legal.

Expand hook indices:

```c
// Existing: 0..10 (combo + init/deinit)
#define MODULE_HOOK_PROCESS_RECORD_USER          11
#define MODULE_HOOK_TAP_DANCE_ON_EACH_TAP        12
#define MODULE_HOOK_TAP_DANCE_ON_DANCE_FINISHED  13
#define MODULE_HOOK_TAP_DANCE_ON_RESET           14
#define MODULE_HOOK_LEADER_START                 15
#define MODULE_HOOK_LEADER_END                   16
#define MODULE_HOOK_LAYER_STATE_SET              17

#define MODULE_HOOK_MAX                          32   // was 16
```

Bump `MODULE_HEADER_VERSION` from 1 to 2. `hook_bitmap` stays `uint32_t`
(32 hooks fit).

### Phase 2 — Dispatcher chaining (`module_dispatch.c`)

Chaining helpers:

```c
// Stop at first false. Used for process_record_user, combo_should_trigger,
// combo_must_hold, etc.
bool module_dispatch_chain_bool_any_false(uint8_t hook, /* args */);

// Call all, ignore return. Used for process_combo_event,
// layer_state_set, tap_dance on_each_tap, etc.
void module_dispatch_chain_void(uint8_t hook, /* args */);

// First module that returns a non-default value wins.
// Used for get_combo_term (QMK semantics).
uint16_t module_dispatch_chain_first_nondefault_u16(uint8_t hook,
                                                     uint16_t def, /* args */);
```

New strong overrides:

- `process_record_user()` — chained, early-exit on false (QMK semantics).
- `layer_state_set_user()` — chained, each module returns possibly-
  modified `state`; fold left across the chain.
- `leader_start_user()` / `leader_end_user()` — chained void.
- Tap dance callbacks — chained (see 2.3 below).

Tap dance integration (Option A, initial scope):

Modules only hook `on_each_tap` / `on_dance_finished` / `on_reset` via
chained dispatch. `tap_dance_eeprom` retains ownership of
`tap_dance_actions[]`. Module hooks receive `(state, user_data)` and
decide whether to act. Modules registering new tap_dance actions via
a dedicated API is deferred to a future phase.

### Phase 3 — Sector-preserving reload with dispatch rebuild

#### Two-phase reload flow

During reload (host has sent explicit sector-erase, not yet DEL 0xFD):
- `module_load()` writes flash only.
- Skip sibling cleanup (already done by the sector-erase command).
- Skip erase (already done).
- Skip `claim_hook()` and `init_fn` calls.

On DEL 0xFD (reload complete), firmware rebuilds the dispatch table for
the affected sector:
1. Walk each slot in the sector.
2. For each slot with a valid header (MODL magic, correct version, valid
   layout, valid CRC): `claim_hook()` for every hook in its bitmap and
   run its `init_fn`.
3. Invalid or blank slots are skipped.

The rebuild is a restricted `module_boot_scan()` over one sector.

#### Why deinit is already handled

The host's sector-erase sysex command (`QMKATA_ID_MODULE` SET with
`slot_id=0xFE`) already calls `module_unload(s)` on every slot in the
sector before erasing flash. `module_unload()` runs deinit, releases
hooks, and invalidates the header. So by the time the reload's
`module_load()` calls begin, the dispatch table has no entries for any
slot in that sector — nothing to deinit during rebuild.

#### State transitions

```
static uint32_t s_last_erased_sector   = 0xFFFFFFFF;  // sentinel
static bool     s_reload_in_progress   = false;

module_loader_mark_sector_erased(sector_base):
    s_last_erased_sector = sector_base
    s_reload_in_progress = true

module_loader_clear_sector_erased():
    if s_last_erased_sector != 0xFFFFFFFF:
        module_sector_activate(s_last_erased_sector)  // rebuild
    s_last_erased_sector = 0xFFFFFFFF
    s_reload_in_progress = false
```

`module_sector_activate()` is factored-out boot_scan logic restricted to
one sector.

#### Failure handling

If host write of a slot fails mid-reload, host still calls DEL 0xFD (the
current code already does this in the failure path of `keyb_sector_reload`).
Firmware rebuilds whatever was successfully written. Slots with invalid
or blank flash are skipped. Consistent final state.

### Phase 4 — Host side (qmk-tools)

- `ModuleBuild.apply_relocations_and_crc` writes `version = 2`.
- Add new `MODULE_HOOK_*` constants to `module_api.h` (host-side) so
  module authors reference them by name.
- Existing reload flow unchanged — `keyb_sector_reload_done()` already
  sends DEL 0xFD. Firmware now does more work on that signal.

### Phase 5 — Module API and examples

- `module_api.h`: add new hook IDs and typedefs for each new hook's
  function signature.
- Example module: `process_record_user` keycode logger. Demonstrates
  chaining alongside a combo module in an adjacent slot.
- Example module: `tap_dance` interceptor. Shows how a module adds
  custom behavior beyond what `tap_dance_eeprom` provides.

### Phase 6 — Testing strategy

Integration test plan (manual on keyboard):

1. Load combo module to slot 0. Verify combos fire.
2. Load `process_record_user` logger to slot 4 (different sector).
   Verify no interference.
3. Load logger to slot 1 (same sector as combo). Verify both active.
4. Load a second logger to slot 2. Verify chain order: slot 1 sees
   keys first, then slot 2.
5. Load a logger in slot 1 that returns false for some key. Verify
   slot 2's hook is not called for that key (early-exit semantics).
6. Upload updated combo binary over slot 0. Verify sibling logger at
   slot 1 survives and stays active after reload.
7. Unload slot 0. Verify slot 1 still works; deinit ran on slot 0.

## Open risks

1. **Dispatch overhead on hot path**: `process_record_user` is called for
   every keypress. Iterating up to 8 slots per keypress = 8 null checks
   + branches. Measured cost likely <5 µs on Cortex-M4 @ 84 MHz.
   Acceptable.
2. **Dispatch rebuild atomicity**: during rebuild, hooks are briefly
   NULL. A keypress mid-rebuild would see a partial table. Mitigation:
   run rebuild with interrupts disabled, or at minimum document that
   input events during a reload may be dropped. DEL 0xFD completes in
   <1 ms — the user is not typing during upload.
3. **Header version migration**: existing v1 modules in flash at boot
   will be rejected by `module_boot_scan()`'s version check. Users re-
   flash. Acceptable for a development feature.
4. **Tap dance scope**: Option A means modules cannot define new tap
   dance entries, only intercept those defined by `tap_dance_eeprom`.
   Module-owned tap dance actions is a later phase.
5. **Leader callback semantics**: QMK leader keymaps define a
   leader_end_user that dispatches on matched sequence. Module-provided
   leader behavior needs a mechanism to register sequences; for this
   plan, modules only hook start/end — matching remains in
   `leader_eeprom`.

## Delivery order

1. **Phase 1** — Hook table redesign. Self-contained, no behavior change
   visible to modules yet (conflict rejection removed but existing
   modules don't collide).
2. **Phase 2** — Dispatcher chaining. New dispatchers added, no wiring to
   QMK callbacks yet for the new hooks.
3. **Phase 3** — Sector reload integration with dispatch rebuild. Fixes
   the current slot-1 finalize-error-1 failure.
4. **Phase 4** — Host: version bump, `module_api.h` constants.
5. **Phase 5** — Examples and docs.
6. **Phase 6** — Manual integration testing.

Each phase is independently testable. Phase 3 is the critical fix for
the current multi-module failure.
