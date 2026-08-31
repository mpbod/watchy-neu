# Task 3 Report: Catalog Metadata and Built-In Selection

## Scope

Implemented only the package catalog and built-in watchface policy required by
Task 3. No selector, menu, shell rendering, or activation-routing policy was
changed.

## RED evidence

Tests were added before production code for:

- atomic built-in selection, including byte-for-byte preservation of the
  committed in-memory and persisted index when the store rejects the save;
- preservation of installed ordering, types, health, quarantine, and other
  unrelated index fields while clearing active, pending, and prior watchface
  references;
- validated manifest-backed catalog name and version metadata;
- exact maximum name/version lengths and one-byte-too-long rejection;
- empty and missing values, duplicate/partial metadata, malformed JSON,
  missing and unreadable manifests, and id/version/type mismatches;
- deterministic all-zero catalog output after any metadata failure, including
  failure after an earlier valid record.

Command:

```text
export PATH="/Users/maxb/.platformio/packages/tool-cmake/bin:/Users/maxb/.platformio/packages/tool-ninja:$PATH"
cmake --build build/host --target watchy_packages_tests
```

Expected result observed: exit 1 with 16 compiler errors. The first failures
were undeclared `watchy_package_select_builtin` and
`watchy_package_catalog_snapshot`; the remaining failures reported absent
`watchy_package_info_t.name` and `.version` members. This confirms the tests
failed because the Task 3 API and metadata did not yet exist.

## GREEN implementation and evidence

- Added bounded, NUL-terminated `name` and `version` catalog fields.
- Added `watchy_package_select_builtin`, which copies the committed index to
  scratch, clears only the three package-watchface selection fields, and uses
  the existing atomic index commit path.
- Added a caller-workspace catalog snapshot policy. Each installed record is
  populated only after its canonical manifest is successfully read and its id,
  version, and type match the indexed record. Any read, parse, bounds, or
  identity failure clears the entire output catalog before returning the exact
  package/store error.
- Updated the IDF snapshot facade to read installed `manifest.json` files while
  holding the existing package-manager mutex. Added the IDF built-in facade
  with the same init, lock, mutation, unlock lifetime.
- Built-in selection never opens, validates, or runs an ELF.

Focused commands and results:

```text
cmake --build build/host --target watchy_packages_tests watchy_portal_tests
./build/host/tests/host/watchy_packages_tests
./build/host/tests/host/watchy_portal_tests
```

Result: `PASS 49 package tests`; `portal tests passed`.

Failure-injection result: the package suite forced the index store save to
fail during built-in selection and observed `WATCHY_PACKAGE_ERR_STORE`, exactly
one attempted save, and byte-for-byte unchanged manager and persisted indexes.
The same suite injected missing/unreadable manifest storage and all requested
metadata failures, verified precise errors, and verified an entirely zeroed
catalog after each failure.

Full host result:

```text
ctest --test-dir build/host --output-on-failure
```

Result: 10/10 tests passed, 0 failed.

Firmware warning build:

```text
platformio run -e watchy_v2
```

Result: success under the component's `-Wall -Wextra -Werror` and
`-Wframe-larger-than=2048` settings. (`platformio.ini` names the actual
environment `watchy_v2`; the plan's generic `watchy` environment does not
exist.) Stack-usage evidence reports 80 bytes for
`watchy_package_catalog_snapshot`, 384 bytes for its installed-manifest reader,
and 32 bytes each for the two IDF facade functions.

Whitespace verification:

```text
git diff --check
```

Result: clean.

## Fix Round 1

### Review finding and RED evidence

The original IDF facade called `watchy_packages_runtime_init()` before clearing
the package watchface selection. On a cold boot that initializer reconciled
package storage, including opening, reading, validating, and potentially
deleting an installed `package.so`. A malformed or unavailable package could
therefore prevent selecting built-in Hairline.

Host tests were added first for a two-phase initialization seam. They require
cold built-in selection to initialize and persist only the index, while
malformed-ELF, missing-ELF, and package-filesystem-failure fixtures observe
zero reconcile, open, read, validate, and delete calls. They also require a
later full initialization to reconcile exactly once after success, retry after
failure, avoid repeated initialization, and precisely propagate index-load and
index-save failures.

Command:

```text
export PATH="/Users/maxb/.platformio/packages/tool-cmake/bin:/Users/maxb/.platformio/packages/tool-ninja:$PATH"
cmake --build build/host --target watchy_packages_tests
```

Expected result observed: exit 1 with 20 compiler errors for the absent
`watchy_package_runtime_init_ops_t`,
`watchy_package_runtime_init_state_t`, `watchy_package_runtime_prepare`, and
`watchy_package_runtime_select_builtin` seam.

### GREEN implementation and evidence

- Split runtime readiness into `index_ready` and `storage_reconciled` phases.
  Failed index initialization or storage reconciliation remains retryable, and
  a successful full initialization reconciles only once.
- Made the IDF built-in facade acquire the init lock, ensure the package mutex,
  initialize only NVS/index state, and atomically clear/persist the selection
  under the package mutex. It does not enter storage reconciliation.
- Standardized ordering as init lock followed by package mutex for full init,
  built-in selection, and safe-mode purge. This serializes cold built-in/full
  init races without deadlocking or corrupting the two-phase state.
- Preserved exact NVS/index and selection-commit errors. A failed commit leaves
  the active selection unchanged and can be retried without reinitializing the
  index.

Focused and full host commands:

```text
cmake --build build/host
./build/host/tests/host/watchy_packages_tests
./build/host/tests/host/watchy_portal_tests
ctest --test-dir build/host --output-on-failure
```

Result: `PASS 51 package tests`; `portal tests passed`; 10/10 CTest targets
passed. Failure injection covered malformed and missing ELF fixtures, package
filesystem failure, index-load failure, selection-save failure, failed full
reconciliation followed by retry, and repeated successful initialization.

Firmware warning build:

```text
platformio run -e watchy_v2
```

Result: success under `-Wall -Wextra -Werror` and
`-Wframe-larger-than=2048`; firmware uses 1,340,323 of 1,835,008 bytes. Stack
usage is 32 bytes for each new/refactored init-policy callback, policy entry
point, IDF init/built-in facade, and safe-mode purge frame.

Whitespace verification:

```text
git diff --check
```

Result: clean.
