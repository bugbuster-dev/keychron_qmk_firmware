<!-- markdownlint-disable-file -->

# Task Research Notes: Merge keychron_q3_max into keychron_q3_max_merge_6f5058f

## Research Executed

### Branch Analysis

- `keychron_q3_max_merge_6f5058f` (current branch, HEAD at `6f5058f7d0`)
  - 60 commits ahead of merge-base with q3_max
  - Contains upstream Keychron additions: new keyboards, bug fixes, features
  - Significant USB stack refactoring (usb_main.c split into multiple files)
  
- `keychron_q3_max` (source branch, HEAD at `ba53d7dca8`)
  - 63 commits ahead of merge-base
  - QMKata/Firmata development branch for Q3 Max
  - Bulk-deleted 1487 files (non-Q3-Max keyboards) for development convenience
  - Added QMKata library and Q3 Max user customizations

- Common ancestor: `e5e57f406e4281f3a9e61afdb6c3e7d323c0cf7b`

### Merge Conflict Analysis (dry-run)

- Total conflicts: 139 files
- Delete-vs-modify conflicts: 136 (q3_max deleted, current branch modified)
- True content conflicts: 3 files
  - `keyboards/keychron/common/keychron_common.c` — both branches modified
  - `keyboards/keychron/common/keychron_common.mk` — both branches modified  
  - `tmk_core/protocol/chibios/usb_main.c` — both branches modified (current branch massively refactored)

### Key Discovery: USB Stack Refactoring

The current merge branch refactored `tmk_core/protocol/chibios/usb_main.c` into:
- `usb_main.c` (reduced)
- `usb_driver.c` + `usb_driver.h`
- `usb_endpoints.c` + `usb_endpoints.h`
- `usb_report_handling.c` + `usb_report_handling.h`

The q3_max branch patched the OLD monolithic `usb_main.c`, so its changes must be manually ported to the new structure.

## Recommended Approach

**Strategy: Merge with "keep ours" for deletions + manual content resolution**

1. Use `git merge --no-commit --no-ff keychron_q3_max`
2. Bulk-resolve 136 delete-vs-modify conflicts with `git checkout --ours`
3. Manually resolve 3 content conflicts
4. The usb_main.c conflict requires porting changes into the refactored file structure

## Build Error Analysis (Task 7)

### Root Cause: Duplicate functions due to upstream refactoring

The upstream branch added `keychron_raw_hid.c` (new file, not in q3_max), which **takes over** the role of `via_command_kb()`, `get_support_feature()`, and `raw_hid_receive()` from `keychron_common.c`. The upstream refactored these functions into `keychron_raw_hid.c` with improved implementations (using `keychron_raw_hid.h` enums and constants).

However, `keychron_common.c` still contains the OLD versions of these functions (lines 228-324), which were the q3_max branch's version. During the merge, both files got compiled (via `keychron_common.mk` which includes BOTH `.c` files), resulting in:

1. **Duplicate `via_command_kb()`** — defined in BOTH `keychron_common.c:254` and `keychron_raw_hid.c:196`
2. **Duplicate `get_support_feature()`** — defined in BOTH `keychron_common.c:240` and `keychron_raw_hid.c:44`
3. **Duplicate `raw_hid_receive()`** — defined in BOTH `keychron_common.c:317` and `keychron_raw_hid.c:200`
4. **Duplicate `PROTOCOL_VERSION`** — `#define` in `keychron_common.c:229` AND `keychron_raw_hid.h:19`
5. **Missing includes** — `keychron_common.c` doesn't `#include "raw_hid.h"` or `"version.h"` (these were present in q3_max's version but lost during merge conflict resolution because the upstream HEAD version didn't have them)

### Why the build fails with THOSE specific errors

The linker may or may not catch the duplicate definitions (C allows multiple definitions in some cases with `--allow-multiple-definition`), but the compiler fails FIRST because `keychron_common.c` calls `raw_hid_send()` (from `raw_hid.h`) and uses `QMK_BUILDDATE` (from `version.h`) without including those headers.

### Evidence trail

- **q3_max `keychron_common.c`**: Had `#include "raw_hid.h"` (line 19) and `#include "version.h"` (line 20), and had `via_command_kb()` as the ONLY definition
- **q3_max `keychron_common.mk`**: Did NOT compile `keychron_raw_hid.c` (file didn't exist)
- **Upstream `keychron_common.c`**: Removed `#include "raw_hid.h"` and `#include "version.h"`, but KEPT the old `via_command_kb()` function body — this is likely an upstream bug/oversight since `keychron_raw_hid.c` now provides the definitive version
- **Upstream `keychron_raw_hid.c`**: New file that provides the canonical `via_command_kb()` → delegates to `kc_raw_hid_rx()` with improved feature handling
- **Upstream `keychron_common.mk`**: Compiles BOTH `keychron_common.c` AND `keychron_raw_hid.c`

### Recommended Fix

**Remove the legacy `via_command_kb()`, `get_support_feature()`, `raw_hid_receive()`, and associated code from `keychron_common.c`**, and move the QMKata hook into `keychron_raw_hid.c` where the canonical `kc_raw_hid_rx()` lives.

Specifically:

#### In `keychron_common.c`:
- **DELETE** lines 228-324 (the entire block from the commented-out `raw_hid_receive_keychron` through EOF)
- This removes the duplicate `PROTOCOL_VERSION` define, the duplicate `get_support_feature()`, the duplicate `via_command_kb()`, and the duplicate `raw_hid_receive()`
- The QMKata-specific `case RAWHID_QMKATA_MSG:` must be relocated to `keychron_raw_hid.c`

#### In `keychron_raw_hid.c`:
- Add `#ifdef QMKATA_ENABLE` / `#include "qmkata/QMKata.h"` at the top (after existing includes)
- Add `case RAWHID_QMKATA_MSG: qmkata_recv_data(data, length); return true;` in `kc_raw_hid_rx()` switch statement (e.g., after the `FACTORY_TEST_ENABLE` case block around line 186)

This approach:
- Eliminates all duplicate definitions
- Preserves the upstream's improved raw_hid architecture
- Preserves the QMKata hook in the correct location
- Does NOT need `#include "raw_hid.h"` or `#include "version.h"` in `keychron_common.c` anymore

## Implementation Guidance

- **Objectives**: Fix build error by deduplicating `via_command_kb` and friends; incorporate QMKata hook into upstream's `keychron_raw_hid.c`
- **Key Tasks**: (1) Delete lines 228-324 from `keychron_common.c`, (2) Add QMKata case to `keychron_raw_hid.c`, (3) Build-verify `make keychron/q3_max/ansi_encoder:via`
- **Dependencies**: None
- **Success Criteria**: Q3 Max firmware compiles cleanly with QMKata support; no duplicate symbol errors
