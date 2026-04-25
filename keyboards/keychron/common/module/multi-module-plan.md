# Multi-Module Hook Support Plan

## Goals

1. Multiple modules can coexist, each hooking QMK callbacks (combo,
   tap_dance, leader, process_record_user, layer_state_set).
2. Each module owns its own non-overlapping set of hooks. One hook =
   one owning module. Modules that need to share a hook are out of
   scope for this plan — a dedicated aggregator module will be
   introduced separately if/when that need arises.
3. Sector-preserving reload stays coherent across sibling modules in
   the same sector.
4. Each module gets correct per-module context in its callbacks.

## Non-goals

- **Hook chaining / fan-out**: multiple modules claiming the same hook
  is explicitly unsupported. Loading a second module that claims an
  already-claimed hook continues to be rejected with a conflict error.
- Coordinating priority, ordering, or early-exit semantics across
  modules — deferred to a future aggregator module.

## Current limitation

The first iteration of the module loader assumes one-hook-one-owner
(which we keep). However, sector-preserving reload incorrectly trips
the conflict check in a legitimate case.

Observed failure during sector-preserving reload: loading slot 0 succeeds
and claims its hooks. Loading slot 1 in the same sector (valid use case —
two independent modules with distinct, non-overlapping hooks) fails the
conflict check and returns finalize error 1. Root cause: during reload,
sibling slot 0's hooks are still claimed in the dispatch table from the
pre-erase boot scan, so when slot 1's load tries to claim its own
(distinct) hooks, the check spuriously fires against stale entries left
over from the now-erased flash.

The current workaround (skip sibling cleanup during reload, see
`module_loader.c`) avoids destroying freshly-written siblings but
leaves the dispatch table out of sync with flash, which is what causes
the false-positive conflict.

## Design

### Phase 1 — Hook table expansion (firmware)

Keep the existing single-owner table shape:

```c
typedef struct {
    void*   func;       // NULL = not claimed
    uint8_t slot_id;    // owner slot (for release / deinit)
} module_hook_owner_t;

static module_hook_owner_t g_module_hooks[MODULE_HOOK_MAX];
```

No matrix, no chaining. `claim_hook()` retains conflict rejection: if
`g_module_hooks[hook].func != NULL`, fail the load.

Expand hook indices to cover the new QMK callbacks modules can own.
Hook constants are grouped by feature category via an infix tag
(`COMBO`, `TAPDANCE`, `LEADER`, `KEY`) so the namespace stays
self-documenting as it grows. Lifecycle hooks (`INIT`/`DEINIT`) are
universal and keep the unqualified `MODULE_HOOK_` prefix. The
namespace itself remains flat — one global `g_module_hooks` table,
one bitmap, one-hook-one-owner across all categories.

```c
/* Combo (existing, renamed) */
#define MODULE_COMBO_HOOK_SHOULD_TRIGGER             0
#define MODULE_COMBO_HOOK_PROCESS_EVENT              1
#define MODULE_COMBO_HOOK_GET_TERM                   2
#define MODULE_HOOK_INIT                             3   /* lifecycle */
#define MODULE_HOOK_DEINIT                           4   /* lifecycle */
#define MODULE_COMBO_HOOK_GET_MUST_HOLD              5
#define MODULE_COMBO_HOOK_GET_MUST_TAP               6
#define MODULE_COMBO_HOOK_GET_MUST_PRESS_IN_ORDER    7
#define MODULE_COMBO_HOOK_PROCESS_KEY_RELEASE        8
#define MODULE_COMBO_HOOK_PROCESS_KEY_REPRESS        9
#define MODULE_COMBO_HOOK_REF_FROM_LAYER             10

/* Key processing (process_record_user, layer_state_set) */
#define MODULE_KEY_HOOK_PROCESS_RECORD_USER          11
#define MODULE_KEY_HOOK_LAYER_STATE_SET              17

/* Tap dance */
#define MODULE_TAPDANCE_HOOK_ON_EACH_TAP             12
#define MODULE_TAPDANCE_HOOK_ON_DANCE_FINISHED       13
#define MODULE_TAPDANCE_HOOK_ON_RESET                14

/* Leader */
#define MODULE_LEADER_HOOK_START                     15
#define MODULE_LEADER_HOOK_END                       16

#define MODULE_HOOK_MAX                              32   /* was 16 */
```

Bump `MODULE_HEADER_VERSION` from 1 to 2. `hook_bitmap` stays `uint32_t`
(32 hooks fit).

### Phase 2 — Dispatcher: direct single-owner dispatch

For each new hook, the dispatcher does a single lookup and call. No
chaining helpers, no fan-out.

```c
// Example: process_record_user
bool module_dispatch_process_record_user(uint16_t keycode, keyrecord_t* rec) {
    void* fn = g_module_hooks[MODULE_KEY_HOOK_PROCESS_RECORD_USER].func;
    if (!fn) return true;  // QMK default: continue processing
    return ((module_process_record_fn)fn)(keycode, rec);
}
```

Same pattern for `layer_state_set`, `leader_start`/`leader_end`, and the
three tap_dance callbacks. If no module owns the hook, return the QMK
default (`true` for bool hooks, `state` unchanged for layer_state_set,
no-op for void hooks).

Tap dance integration (Option A, unchanged):

A module may own `on_each_tap` / `on_dance_finished` / `on_reset` hooks
but `tap_dance_eeprom` retains ownership of `tap_dance_actions[]`.
Modules registering new tap_dance actions via a dedicated API is
deferred to a future phase.

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
4. If `claim_hook()` reports a conflict during rebuild, the second
   claimant's load is aborted (its `init_fn` not called, its hooks
   released) and an error is logged. This is the legitimate
   one-hook-one-owner enforcement — modules in the same sector must
   not declare overlapping hook bitmaps.

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
- Add new hook constants to `module_api.h` (host-side), grouped by
  category: `MODULE_COMBO_HOOK_*`, `MODULE_KEY_HOOK_*`,
  `MODULE_TAPDANCE_HOOK_*`, `MODULE_LEADER_HOOK_*`, plus the
  unqualified lifecycle hooks `MODULE_HOOK_INIT` / `MODULE_HOOK_DEINIT`.
  Module authors reference them by name.
- Existing reload flow unchanged — `keyb_sector_reload_done()` already
  sends DEL 0xFD. Firmware now does more work on that signal.

### Phase 5 — Module API and examples

- `module_api.h`: add new hook IDs and typedefs for each new hook's
  function signature.
- Example module: `process_record_user` keycode logger. Demonstrates
  coexistence with a combo module in an adjacent slot (different
  hooks, no overlap).
- Example module: `tap_dance` interceptor. Shows how a module adds
  custom behavior beyond what `tap_dance_eeprom` provides.

### Phase 6 — Testing strategy

Integration test plan (manual on keyboard):

1. Load combo module to slot 0. Verify combos fire.
2. Load `process_record_user` logger to slot 4 (different sector).
   Verify no interference.
3. Load logger to slot 1 (same sector as combo). Verify both active —
   combo owns combo hooks, logger owns PRU hook, no overlap.
4. Attempt to load a second module claiming PRU into slot 2. Verify
   load is rejected with a hook-conflict error (one-hook-one-owner
   enforcement).
5. Upload updated combo binary over slot 0. Verify sibling logger at
   slot 1 survives and stays active after reload.
6. Unload slot 0. Verify slot 1 still works; deinit ran on slot 0.

## Open risks

1. **Hook-conflict UX**: users loading two modules with overlapping
   hooks see a load failure with no automatic resolution. Mitigation:
   clear error code/message identifying the conflicting hook and the
   already-owning slot. A future aggregator module is the path
   forward for genuine multi-owner scenarios.
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

1. **Phase 1** — Hook indices/version bump. Self-contained, no
   behavior change for existing modules.
2. **Phase 2** — Direct dispatchers for the new hooks. Wired into QMK
   callbacks; no-op when no module owns the hook.
3. **Phase 3** — Sector reload integration with dispatch rebuild. Fixes
   the current slot-1 finalize-error-1 failure.
4. **Phase 4** — Host: version bump, `module_api.h` constants.
5. **Phase 5** — Examples and docs.
6. **Phase 6** — Manual integration testing.

Each phase is independently testable. Phase 3 is the critical fix for
the current multi-module failure.
