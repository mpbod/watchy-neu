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
