# Task 4 Report: On-Watch Watchface Selector Policy

## Scope

Implemented the watchface selector and Menu/Settings policy state only. This
task does not draw the new screens, persist a package choice, or invoke the
package manager. Rendering remains Task 5 and end-to-end activation remains
Task 11.

## RED evidence

Tests were changed before production code. The first focused run compiled and
failed against the approved base with the old policy:

```text
FAIL tests/host/test_shell.c:297: WATCHY_SHELL_LAUNCHER_ITEMS == 3u
FAIL tests/host/test_shell.c:331: shell.screen == WATCHY_SHELL_DIAGNOSTICS
```

This directly recorded the old six-item launcher and the absence of nested
Diagnostics/About Settings routes. The complete selector suite was then added
and failed to compile on the intentionally absent selector screen,
three-visible-row constant, independent app/face maps, selector helpers, and
copied action-request interface.

## GREEN implementation

- The top Menu now has three deterministic routes in exact order: Watchface
  selector, Apps, and Settings. Up/Down wrap over exactly those three entries.
- Settings policy now has ten entries in exact order: Clock, Motion Wake,
  Display Motion, Set Time, NTP Sync, Wi-Fi, Portal, Refresh, Diagnostics, and
  About. Diagnostics and About return to Settings rather than the launcher.
- Independent bounded app and watchface maps filter mixed catalogs without
  assuming catalog order. A separate recovery map preserves safe-mode removal
  of both package types. Counts above `WATCHY_PACKAGE_INSTALLED_MAX` are
  deliberately truncated before any catalog array access.
- Selector position zero is always Hairline. Healthy active WPK metadata sets
  the initial cursor; pending-only, missing, and quarantined active metadata
  fall back to Hairline. Quarantined faces stay mapped and visible but produce
  the bounded package error with no activation action.
- Apps and selector pagination use three-row windows and deterministic wrap.
  Coverage includes zero, one, three, four, exact-capacity, and over-capacity
  catalogs.
- `SELECT_BUILTIN` and `SELECT_WATCHFACE` are policy-only action values.
  `watchy_shell_take_action_request` atomically copies action, catalog index,
  and stable package reference, then clears the latched request. Repeated takes
  return no action. Catalog refresh or caller-buffer mutation after latching
  cannot change the request payload.
- Missing or unreadable catalogs clear all maps before returning, so stale
  rows cannot remain. Safe mode maps no WPK faces, exposes only Hairline in a
  selector state, and cannot produce `SELECT_WATCHFACE`.
- The bounded package-reference cache is shell-policy static storage, not
  automatic stack storage. The firmware has one shell owner; owner checks fail
  closed rather than copying an identity from another shell instance.

## Verification

All host CMake commands used the pinned toolchain:

```text
export PATH="/Users/maxb/.platformio/packages/tool-cmake/bin:/Users/maxb/.platformio/packages/tool-ninja:$PATH"
```

Focused warning build and execution:

```text
cmake --build build/host --target watchy_shell_tests
./build/host/tests/host/watchy_shell_tests
```

Result: `shell tests passed` under `-Wall -Wextra -Werror`.

Full host regression:

```text
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Result: 10/10 tests passed, 0 failed.

Firmware warning/frame build:

```text
platformio run -e watchy_v2
```

Result: success under `-Wall -Wextra -Werror`,
`-Wframe-larger-than=2048`, and `-fstack-usage`; flash usage is 1,340,875 of
1,835,008 bytes. Generated stack reports show 32-byte frames for catalog
mapping, selector input, and action-request take; 128 bytes for the legacy
action wrapper; and 1,584 bytes for `app_main`, below the 2,048-byte limit.
`sizeof(watchy_shell_t)` is 192 bytes on the host.

Whitespace verification:

```text
git diff --check
```

Result: clean.

## Deferred by design

Task 5 must render the Menu, selector rows/status metadata, Hairline, and the
ten-item Settings viewport. Task 11 must consume the new selection actions and
perform package selection, persistence, lifecycle render, promotion, or
rollback. This task intentionally performs none of those operations.

## Fix Round 1: Active and Pending Cursor Precedence

### Reviewer finding and root cause

The active-cursor predicate required `active` and healthy metadata but did not
exclude `pending`. A healthy catalog entry carrying both flags was therefore
initialized as the selector's active row, contrary to the rule that pending
must never be presented as active. Existing coverage tested a pending-only row
and did not exercise the overlapping flags.

### RED evidence

Added `test_selector_never_treats_a_pending_face_as_active` with one healthy
watchface marked both `active = true` and `pending = true`. After entering the
selector, the test requires position zero, Hairline.

```text
FAIL tests/host/test_shell.c:685: shell.selection == 0u
```

### GREEN implementation and verification

The cursor eligibility predicate now explicitly requires
`!package->pending`, in addition to active and non-quarantined status. No
mapping, action, rendering, or persistence behavior changed.

```text
cmake --build build/host --target watchy_shell_tests
./build/host/tests/host/watchy_shell_tests
```

Result: `shell tests passed`.

```text
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Result: 10/10 tests passed, 0 failed.

```text
platformio run -e watchy_v2
```

Result: firmware build succeeded under the existing warnings-as-errors and
frame-size flags; flash usage remains 1,340,875 of 1,835,008 bytes.

```text
git diff --check
```

Result: clean.
