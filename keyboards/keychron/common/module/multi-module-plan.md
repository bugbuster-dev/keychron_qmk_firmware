# Multi-Module Hook Support Plan

## Goals

1. Multiple modules can coexist, each hooking selected QMK callbacks.
2. Each module owns its own non-overlapping set of hooks. One hook =
   one owning module. Modules that need to share a hook are out of
   scope for this plan — a dedicated aggregator module will be
   introduced separately if/when that need arises.
3. Sector-preserving reload stays coherent across sibling modules in
   the same sector (already implemented; see Phase 3 status).
4. Each module gets correct per-module context in its callbacks.

## Non-goals

- **Hook chaining / fan-out**: multiple modules claiming the same hook
  is explicitly unsupported. Loading a second module that claims an
  already-claimed hook continues to be rejected with a conflict error.
- Coordinating priority, ordering, or early-exit semantics across
  modules — deferred to a future aggregator module.
- **Tap dance and leader integration**: see "Hooks deferred from this
  plan" below — both have keymap-side dependencies that this plan does
  not address.

## Status overview

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Hook indices, header version bump (firmware) | **Done** (commit `1050f5fa`) |
| 1b | Hook index refinements (PRE/PROCESS_RECORD split, HOUSEKEEPING, SHUTDOWN) | **Done** (commit `d7f51b3a`) |
| 2 | Direct dispatchers for new hooks | **Done** (commit `4f13b4d8`) — 5 dispatchers + 4 keymap integrations |
| 3 | Sector-preserving reload coherence | **Already implemented** in current `module_loader.c`; see "Phase 3 status" |
| 4 | Host-side (qmk-tools) version bump and constants | **Done** (qmk-tools commit `fb424bf` + `17008b9`) |
| 5 | Example modules and module API surface | **Partial** — `pre_record_logger.c` done (qmk-tools commit `bee3414`); `hooks_template.c` renamed to `combo_hooks_template.c` (commit `fd55961`) |
| 6 | Manual integration testing | **Partial** — combo + pre_record_logger coexistence in same sector verified on hardware |

## Hooks deferred from this plan

Two hook categories were reserved by index in Phase 1 but turned out
not to fit the simple "strong-override the QMK weak callback"
dispatch pattern:

### Tap dance — deferred

QMK tap dance is **data-driven, not callback-driven**. The keymap
declares an array `tap_dance_action_t tap_dance_actions[]` whose
entries hold function pointers (`on_each_tap`, `on_dance_finished`,
`on_reset`, `on_each_release`). QMK's `process_tap_dance.c` calls
those pointers directly per-action; **no global `tap_dance_*_user()`
weak symbol exists** for a module dispatcher to override.

For a module to participate in tap dance, the keymap (or
`tap_dance_eeprom_apply()`) must explicitly write the module's hook
function pointers into `tap_dance_actions[slot].fn.on_X` at setup
time. That is keymap-level glue, not a module-loader concern, and is
not addressed here.

The hook indices `MODULE_TAPDANCE_HOOK_ON_EACH_TAP` (12),
`MODULE_TAPDANCE_HOOK_ON_DANCE_FINISHED` (13), and
`MODULE_TAPDANCE_HOOK_ON_RESET` (14) remain reserved for a future
phase that introduces a `module_register_tap_dance(slot, def)` API.

### Leader — deferred

QMK leader has weak `leader_start_user()` / `leader_end_user()`
symbols, but Keychron's `leader_eeprom` integration already takes
ownership of `leader_end_user()` from the keymap (see e.g.
`keyboards/keychron/q3_max/ansi_encoder/keymaps/keychron/keymap.c`).
That keymap definition invokes `leader_eeprom_try_match()`, drives
the LED indicator, and gates re-matching via `leader_already_matched`.

A module dispatcher providing a strong override of `leader_end_user`
would collide with the keymap definition at link time. Resolving
that collision requires either (a) refactoring the keymap-side
matching logic into `leader_eeprom` and exposing a default handler
for the dispatcher to fall through to, or (b) a cooperative scheme
where the keymap explicitly calls into the module dispatcher. Both
are larger changes than fits this plan.

The hook indices `MODULE_LEADER_HOOK_START` (15) and
`MODULE_LEADER_HOOK_END` (16) remain reserved for a future phase.

## Design

### Phase 1 — Hook table expansion (firmware) — Done

**Commit `1050f5fa`** (firmware) + **`fb424bf`** (qmk-tools).

Single-owner table shape preserved:

```c
typedef struct {
    void*   func;       // NULL = not claimed
    uint8_t module_id;  // owner slot (for release / deinit)
} module_hook_entry_t;

static module_hook_entry_t g_module_hooks[MODULE_HOOK_MAX];
```

`claim_hook()` retains conflict rejection: if
`g_module_hooks[hook].func != NULL`, fail the load.

Hook constants are grouped by feature category via an infix tag
(`COMBO`, `KEY`, `TAPDANCE`, `LEADER`) so the namespace stays
self-documenting as it grows. Lifecycle hooks (`INIT`/`DEINIT`)
are universal and keep the unqualified `MODULE_HOOK_` prefix. The
namespace itself remains flat: one global `g_module_hooks` table,
one bitmap, one-hook-one-owner across all categories.

`MODULE_HEADER_VERSION` bumped from 1 to 2. `hook_bitmap` stays
`uint32_t` (32 hooks fit). On-flash hook table grew from 64 to
128 bytes. v1 modules rejected by loader version check.

**Verified end-to-end**: module v2 upload to device confirmed working.

### Phase 1b — Hook index refinements — Done

**Commit `d7f51b3a`** (firmware) + **`17008b9`** (qmk-tools).

Index 11 renamed from `MODULE_KEY_HOOK_PROCESS_RECORD_USER` to
`MODULE_KEY_HOOK_PRE_PROCESS_RECORD` (targets `pre_process_record_user`).
Index 18 added as `MODULE_KEY_HOOK_PROCESS_RECORD` (cooperative
dispatch). Index 19 added as `MODULE_HOOK_HOUSEKEEPING`. Index 20
added as `MODULE_HOOK_SHUTDOWN`. `MODULE_HOOK_MAX` stays 32.

Current layout in `module_loader.h`:

```c
/* Combo hooks */
#define MODULE_COMBO_HOOK_SHOULD_TRIGGER             0
#define MODULE_COMBO_HOOK_PROCESS_EVENT              1
#define MODULE_COMBO_HOOK_GET_TERM                   2
/* Lifecycle (universal) */
#define MODULE_HOOK_INIT                             3
#define MODULE_HOOK_DEINIT                           4
/* Combo hooks (continued) */
#define MODULE_COMBO_HOOK_GET_MUST_HOLD              5
#define MODULE_COMBO_HOOK_GET_MUST_TAP               6
#define MODULE_COMBO_HOOK_GET_MUST_PRESS_IN_ORDER    7
#define MODULE_COMBO_HOOK_PROCESS_KEY_RELEASE        8
#define MODULE_COMBO_HOOK_PROCESS_KEY_REPRESS        9
#define MODULE_COMBO_HOOK_REF_FROM_LAYER             10
/* Key processing hooks */
#define MODULE_KEY_HOOK_PRE_PROCESS_RECORD           11
#define MODULE_KEY_HOOK_PROCESS_RECORD               18
#define MODULE_KEY_HOOK_LAYER_STATE_SET              17
/* Tap dance hooks — reserved, not currently dispatched */
#define MODULE_TAPDANCE_HOOK_ON_EACH_TAP             12
#define MODULE_TAPDANCE_HOOK_ON_DANCE_FINISHED       13
#define MODULE_TAPDANCE_HOOK_ON_RESET                14
/* Leader hooks — reserved, not currently dispatched */
#define MODULE_LEADER_HOOK_START                     15
#define MODULE_LEADER_HOOK_END                       16
/* Lifecycle hooks (universal) */
#define MODULE_HOOK_HOUSEKEEPING                     19
#define MODULE_HOOK_SHUTDOWN                         20

#define MODULE_HOOK_MAX                              32
```

### Phase 2 — Direct dispatchers — Done

**Commit `4f13b4d8`** (firmware dispatchers) + **`63bd81f9`** (keymap integration).

Five dispatchers implemented in `module_dispatch.c`:
- `pre_process_record_user` — strong override, routes to `MODULE_KEY_HOOK_PRE_PROCESS_RECORD`
- `module_dispatch_process_record` — cooperative helper for `MODULE_KEY_HOOK_PROCESS_RECORD`
- `layer_state_set_user` — strong override, routes to `MODULE_KEY_HOOK_LAYER_STATE_SET`
- `housekeeping_task_user` — strong override, routes to `MODULE_HOOK_HOUSEKEEPING`
- `shutdown_user` — strong override, routes to `MODULE_HOOK_SHUTDOWN`

All four Keychron keymaps (ansi/iso × keychron/default) now call
`module_dispatch_process_record()` as the first line of their
`process_record_user`, gated on `MODULE_LOADER_ENABLE`.

Two dispatch mechanisms are used in Phase 2:

**(1) Strong override** of an uncontested QMK weak symbol — the
dispatcher *is* the QMK callback. No keymap action required.

```c
// pre_process_record_user — strong override (no keymap collision)
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    void* fn = g_module_hooks[MODULE_KEY_HOOK_PRE_PROCESS_RECORD].func;
    if (!fn) return true;  // QMK default: continue processing
    return ((module_pre_process_record_fn)fn)(keycode, record);
}
```

**(2) Cooperative dispatch** — the dispatcher exposes a public
helper that keymaps call explicitly from inside their own callback.
Used when the QMK weak symbol is already strong-defined by every
keymap (i.e. `process_record_user`).

```c
// module_dispatch.c — public helper, no QMK symbol override
bool module_dispatch_process_record(uint16_t keycode, keyrecord_t *record) {
    void* fn = g_module_hooks[MODULE_KEY_HOOK_PROCESS_RECORD].func;
    if (!fn) return true;
    return ((module_process_record_fn)fn)(keycode, record);
}

// keymap.c — opt-in: keymap chooses placement
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!module_dispatch_process_record(keycode, record)) {
        return false;  // module suppressed the keypress
    }
    // ... keymap's own logic ...
    return true;
}
```

The keymap is in charge of *when* the module hook fires inside its
`process_record_user`. The recommended pattern is to call the helper
as the first line, so module logic runs before keymap logic, but
keymaps may place the call anywhere (after a partial keymap-side
filter, only on certain layers, etc.). Keymaps that don't add the
call simply make `MODULE_KEY_HOOK_PROCESS_RECORD` unreachable from
that keymap — opt-in per keymap.

#### Hook set delivered in Phase 2

| Constant | QMK callback | Mechanism | Signature | Default when unclaimed |
|----------|--------------|-----------|-----------|------------------------|
| `MODULE_KEY_HOOK_PRE_PROCESS_RECORD` | `pre_process_record_user` | Strong override | `bool(uint16_t, keyrecord_t*)` | `true` (continue) |
| `MODULE_KEY_HOOK_PROCESS_RECORD` | `process_record_user` (via cooperative call) | Keymap-side call to `module_dispatch_process_record()` | `bool(uint16_t, keyrecord_t*)` | `true` (continue) |
| `MODULE_KEY_HOOK_LAYER_STATE_SET` | `layer_state_set_user` | Strong override | `layer_state_t(layer_state_t)` | `state` unchanged |
| `MODULE_HOOK_HOUSEKEEPING` | `housekeeping_task_user` | Strong override | `void(void)` | no-op |
| `MODULE_HOOK_SHUTDOWN` | `shutdown_user` | Strong override | `bool(bool)` | `true` (allow shutdown) |

All four strong-override targets (`pre_process_record_user`,
`layer_state_set_user`, `housekeeping_task_user`, `shutdown_user`)
are verified unused by every Keychron keymap and common code on
this branch, so the dispatcher's strong definitions do not collide.

#### Why both PRE and PROCESS_RECORD

The two hooks cover complementary use cases:

- **PRE_PROCESS_RECORD**: module runs unconditionally on every
  keypress, *before* the keymap. Useful for keypress logging,
  global remapping, OS-detection-driven layer switches, and any
  module that wants first-look authority regardless of whether the
  keymap cooperates. Returning `false` short-circuits the entire
  pipeline including the keymap's `process_record_user`.

- **PROCESS_RECORD**: module participates only on keymaps that opt
  in by calling the dispatcher helper. Useful for module behavior
  that should compose with the keymap's own logic — e.g. the
  keymap handles its own keycodes, then defers to the module for
  unknown keycodes; or vice versa. The keymap chooses placement,
  giving fine-grained control over interleave order.

A module typically claims one or the other, not both. Module
authors writing a "logger that sees every key" use PRE; module
authors writing "extra keycode handlers that the keymap can opt
into" use PROCESS_RECORD.

#### Why this set

- **`pre_process_record_user`** rather than only relying on
  `process_record_user`: it gives modules a path that does not
  require keymap cooperation, which matters for distributing
  general-purpose modules to users who haven't modified their
  keymap. It's also strictly more powerful: a `false` return
  suppresses the keypress before the keymap sees it.
- **`process_record_user` via cooperative dispatch**: matches
  module authors' intuition (the QMK callback name they already
  know) and lets a module's behavior interleave with keymap logic.
  The opt-in per-keymap design avoids forcing a refactor across
  all four Keychron keymaps just to support modules that may not
  even be present.
- **`layer_state_set_user`**: covers layer-change observation and
  transformation; the only mainstream weak callback in that area
  with no Keychron-tree definition.
- **`housekeeping_task_user`**: runs every main-loop tick. Lets a
  module do background work (debounced flushes, periodic checks)
  without claiming `matrix_scan_user` or starting its own task.
- **`shutdown_user`**: symmetric counterpart to `MODULE_HOOK_INIT`.
  `MODULE_HOOK_DEINIT` only fires on `module_unload()`, not on
  reboot or jump-to-bootloader. A module that needs to flush state
  before power loss claims `MODULE_HOOK_SHUTDOWN`.

`keyboard_pre_init_user` was considered and rejected: it runs
**before** `module_boot_scan()`, so no module is loaded yet — there
is no way for a module to subscribe to its own pre-init.
`keyboard_post_init_user` is already taken by `q3_max_user.c`
(it drives `module_boot_scan()` itself among other setup), so it
cannot be a dispatcher override either; the per-module
`MODULE_HOOK_INIT` already covers post-load initialization.

### Phase 3 — Sector-preserving reload — Already implemented

The current `module_loader.c` already handles sector-preserving
reload coherently for the multi-module case. The originally-described
slot-1 finalize-error-1 failure does not reproduce under the
one-hook-one-owner semantics, because:

1. **Sibling cleanup is skipped during reload**
   (`module_load()` line 264, `if (sector_base != s_last_erased_sector)`).
   Slot 0's hooks remain claimed when slot 1 is being written.
2. **Hook conflict check now operates correctly**
   (line 274–282): with disjoint hook bitmaps between siblings, the
   check passes and slot 1's claim succeeds. With overlapping hook
   bitmaps it fails, which is the intended one-hook-one-owner
   enforcement.
3. **Erase is skipped when the host already erased**
   (line 290): `s_last_erased_sector` tracks the sector erased by
   the host's explicit sysex command and prevents the per-slot load
   from re-erasing freshly-written siblings.
4. **`init_fn` runs eagerly per-slot during reload.** Earlier drafts
   of this plan proposed deferring `init_fn` calls to DEL 0xFD with
   a "rebuild" pass; that turned out to be unnecessary complexity.
   Modules are independent (no cross-module discovery), so init
   ordering between siblings doesn't matter.

`module_loader_clear_sector_erased()` (called on DEL 0xFD) only
clears the sentinel; no rebuild pass is needed because hooks were
already claimed during the per-slot loads.

The plan's previously-described "Phase 3 — Two-phase reload with
dispatch rebuild" is therefore obsolete and not pursued. If a future
need arises (e.g. atomic activation across siblings, or
cross-module dependency ordering), it can be revisited as a
separate piece of work.

### Phase 4 — Host side (qmk-tools) — Done

**Commits `fb424bf`** + **`17008b9`** (qmk-tools).

- `ModuleBuild.apply_relocations_and_crc` writes `version = 2`.
- `MODULE_HOOK_MAX = 32` in `ModuleBuild.py`; hook table size
  in assembled binary is 128 bytes.
- `module_api.h` (host-side) carries hook constants grouped by
  category: `MODULE_COMBO_HOOK_*`, `MODULE_KEY_HOOK_*`,
  `MODULE_TAPDANCE_HOOK_*`, `MODULE_LEADER_HOOK_*`, plus the
  unqualified lifecycle hooks `MODULE_HOOK_INIT` / `MODULE_HOOK_DEINIT` /
  `MODULE_HOOK_HOUSEKEEPING` / `MODULE_HOOK_SHUTDOWN`.
- `ModuleBuild.py` `HOOK_NAMES` dict includes all 21 hook labels
  for ModuleTab UI display.
- Existing reload flow unchanged — `keyb_sector_reload_done()`
  already sends DEL 0xFD; firmware uses that signal only to clear
  `s_last_erased_sector`.
- All 20 host tests passing.

### Phase 5 — Module API and examples

- `module_api.h`: typedefs for each new hook's function signature.
  Both `MODULE_KEY_HOOK_PRE_PROCESS_RECORD` and
  `MODULE_KEY_HOOK_PROCESS_RECORD` share the same signature
  (`bool(uint16_t, keyrecord_t*)`), so a single typedef
  (`module_process_record_fn`) covers both.
- A keymap snippet for opting in to `MODULE_KEY_HOOK_PROCESS_RECORD`
  is documented alongside the API. Recommended placement is the
  first line of the keymap's `process_record_user`:
  ```c
  bool process_record_user(uint16_t keycode, keyrecord_t *record) {
      if (!module_dispatch_process_record(keycode, record)) return false;
      // ... existing keymap logic ...
  }
  ```
- Example modules (deliver at least one; the others are stretch):
  - **`pre_record_logger`** — claims `MODULE_KEY_HOOK_PRE_PROCESS_RECORD`,
    logs every keypress via `mprintf`. Demonstrates the strong-override
    path; works without any keymap changes. **Done** (qmk-tools commit
    `bee3414`). Builds to 288 bytes.
  - **`combo_hooks_template`** — the original `hooks_template.c` was
    renamed to `combo_hooks_template.c` (qmk-tools commit `fd55961`)
    for clarity, distinguishing it from non-combo module templates.
  - **`record_logger`** — claims `MODULE_KEY_HOOK_PROCESS_RECORD`,
    logs only when the keymap opts in by calling
    `module_dispatch_process_record()`. Demonstrates the cooperative
    path and the per-keymap opt-in semantics. **Not yet delivered.**
  - **`housekeeping_heartbeat`** — claims `MODULE_HOOK_HOUSEKEEPING`,
    emits a periodic mprintf line. Exercises the lifecycle-hook path
    end-to-end with a non-keypress callback. **Not yet delivered.**

### Phase 6 — Testing strategy

Integration test plan (manual on keyboard):

1. Load combo module to slot 0. Verify combos fire.
2. Load `pre_record_logger` to slot 4 (different sector). Verify no
   interference with combo module; verify keypresses are logged.
3. Load `pre_record_logger` to slot 1 (same sector as combo). Verify
   both active — combo owns combo hooks, logger owns
   `MODULE_KEY_HOOK_PRE_PROCESS_RECORD`, no overlap.
4. Attempt to load a second module also claiming `PRE_PROCESS_RECORD`
   into slot 2. Verify load is rejected with a hook-conflict error
   (one-hook-one-owner enforcement).
5. Cooperative dispatch (PROCESS_RECORD):
   a. Load `record_logger` (claims `MODULE_KEY_HOOK_PROCESS_RECORD`)
      with the **default** keymap (no `module_dispatch_process_record`
      call). Verify the module loads but its hook never fires —
      keypresses are unaffected.
   b. Re-flash with the **keychron** keymap that includes the
      cooperative call at the top of `process_record_user`. Verify
      the same module's hook now fires on every keypress.
   c. Verify returning `false` from the module suppresses the
      keymap's own `process_record_user` logic for that key.
6. Upload updated combo binary over slot 0 (sector-preserving reload).
   Verify sibling logger at slot 1 survives and stays active after
   reload.
7. Unload slot 0. Verify slot 1 still works; deinit ran on slot 0.
8. Trigger a reboot (e.g. `QK_BOOT`) with a module owning
   `MODULE_HOOK_SHUTDOWN`. Verify the shutdown hook fires before the
   jump-to-bootloader.

## Open risks

1. **Hook-conflict UX**: users loading two modules with overlapping
   hooks see a load failure with no automatic resolution. Mitigation:
   clear error code/message identifying the conflicting hook and the
   already-owning slot. A future aggregator module is the path
   forward for genuine multi-owner scenarios.
2. **Header version migration**: existing v1 modules in flash at boot
   are rejected by `module_boot_scan()`'s version check. Users
   re-flash. Acceptable for a development feature; verified working
   end-to-end on hardware.
3. **Tap dance scope**: modules cannot define new tap dance entries,
   only intercept those defined by `tap_dance_eeprom`, and even that
   requires keymap-level glue not provided here. Module-owned tap
   dance actions is a later phase. See "Hooks deferred from this
   plan" above.
4. **Leader callback collision**: the existing keymap-side
   `leader_end_user` in `leader_eeprom` integration prevents a clean
   dispatcher override. Module-provided leader behavior needs a
   refactor of either `leader_eeprom` or the keymap; not in scope
   here. See "Hooks deferred from this plan" above.
5. **Shutdown hook reliability**: QMK's `shutdown_user` is invoked
   from `software_reset()` and similar paths but not on every
   power-loss scenario (e.g. a true power cut bypasses it entirely).
   Modules that need durable state should persist eagerly, not rely
   on `MODULE_HOOK_SHUTDOWN` as the sole save point.
6. **Cooperative-dispatch silent unreachability**: a module claiming
   `MODULE_KEY_HOOK_PROCESS_RECORD` loads successfully even when the
   active keymap doesn't call `module_dispatch_process_record()`,
   because the loader can't introspect keymap source. The hook is
   simply unreachable from that keymap, and the module's behavior
   silently does not occur. Mitigation: document the keymap snippet
   prominently in module API docs; users debugging "why isn't my
   module firing?" should check whether the keymap opts in. Long
   term, every Keychron keymap on the modules-supported branches
   should include the call by default so this surprises only users
   running a stripped-down keymap.

## Delivery order

1. **Phase 1** — Hook indices/version bump (firmware + host).
   **Done** (commits `1050f5fa` + `fb424bf`). Verified end-to-end on
   hardware: v2 module upload succeeds.
2. **Phase 4** — Host-side companion: version bump and hook
   constants. **Done** (commits `fb424bf` + `17008b9`).
3. **Phase 1b** — Hook index refinements (PRE/PROCESS_RECORD split,
   HOUSEKEEPING, SHUTDOWN). **Done** (commit `d7f51b3a` + `17008b9`).
4. **Phase 2** — Direct dispatchers for `pre_process_record_user`,
   `process_record_user` (cooperative), `layer_state_set_user`,
   `housekeeping_task_user`, `shutdown_user`. Plus keymap integration.
   **Done** (commits `4f13b4d8` + `63bd81f9`).
5. **Phase 5** — At least one example module (PRU logger).
    **Partial** — `pre_record_logger.c` done (qmk-tools commit `bee3414`);
    `hooks_template.c` renamed to `combo_hooks_template.c` (commit `fd55961`).
6. **Phase 6** — Manual integration testing on hardware. **Partial** —
    combo + pre_record_logger coexistence in same sector (slots 0+1)
    verified on hardware. Remaining tests (hook conflict rejection,
    sector-preserving reload survival, shutdown hook) pending.

Phase 3 is not in the active sequence — already covered by the
current loader implementation.

## Release Milestones

| Tag | Firmware commit | qmk-tools commit | Description |
|-----|----------------|------------------|-------------|
| `module-support-v0.2` | `18529321` | `fd55961` | Phase 1+1b+2+4 complete; Phase 5+6 partial. Multi-module coexistence verified on hardware. |
