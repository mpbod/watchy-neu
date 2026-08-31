# Task 6 rescue acceptance report

## Scope and ruling

This rescue fixes the shared Task 6 runtime/tooling and adds fixture-backed
builder coverage. Tasks 7–9 renderer projects do not exist in this worktree,
so no successful eight-WPK build is claimed.

The approved Tasks 7–9 plan authors the stable IDs without numeric hyphens:
`watchy.firstparty.grid01`, `.grid02`, `.grid03`, `.term01`, `.term02`,
`.term03`, `.slab`, and `.orbit`. The filesystem/output slugs are hyphenated
where applicable (`grid-01`, `term-01`, `grid-01.wpk`, etc.) to match the
requested project naming. The builder keeps these concerns separate.

## RED evidence

- ABI 1.2 package fixture failed while `tools/watchy_pkg.py` still capped the
  host at 1.1.
- Matrix assertions failed for stable IDs and capabilities.
- Builder CLI tests failed because `--only` was absent and missing projects
  raised a traceback.
- Expanded helper tests initially failed to compile against descriptor layout
  assertions until the assertions were made exact and alignment-portable.
- Full Python initially failed its settings-label contract: it required the
  obsolete `"DISPLAY FX %s"` source string even though the approved shell
  labels are `"Motion Wake"` and `"Display Motion"`.

## Implemented

- `watchy_pkg.py` now accepts ABI 1.0, 1.1, and 1.2 packages and rejects newer
  minor versions; existing ABI compatibility remains append-only.
- The first-party matrix is an independent, sorted eight-face allowlist with
  exact capabilities: Grid01=787, Grid02=515, Grid03=515, Term01=771,
  Term02=531, Term03=531, Slab=531, Orbit=531.
- `build_first_party.py` supports `--list`, `--dry-run`, `--only`, and
  `--reproducible`, emits short nonzero CLI errors, writes manifests outside
  source projects, and reports missing renderer projects honestly.
- Each selected project is built twice serially in known `round-1` and
  `round-2` directories. Each round invokes the native build-system `clean`
  target, then build; the tool discovers the one real package ELF by ELF
  contents/export rather than a guessed slug filename, and invokes
  `watchy_pkg.py audit-elf`, `build`, and `verify` in order.
- Complete-set staging promotes only after all selected packages pass and
  byte-compare. Promotion uses a sibling backup and restores it on failure;
  failed/mismatched rounds clean staging/work debris and preserve prior output.
- Face helpers now cover checked Gregorian dates, 12/24-hour formatting,
  normalized offsets across month/year boundaries, recomputed weekdays,
  invalid-output preservation, and signed 64-bit UTC-normalized lunar phase
  arithmetic at the approved 2000-01-06 18:14 UTC epoch.
- Descriptor C++ assertions cover prefix/offset layout and the v1.2 system
  transition field. The shared face context has no heap/STL/runtime ownership;
  unload clears host/context state and a subsequent load is supported.

## Verification evidence

```text
python3 -m unittest tests.python.test_first_party tests.python.test_watchy_pkg -v
Ran 22 tests ... OK

cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
100% tests passed, 0 tests failed out of 13

python3 tools/build_first_party.py --list
python3 tools/build_first_party.py --dry-run --only grid-01 term-02
both completed successfully

python3 tools/build_first_party.py --only grid-01
exit 2; stderr: watchy-build: renderer project is not present: .../grid-01
```

The fixture tests independently assert two clean/build rounds, real ELF
discovery, audit/build/verify ordering, deterministic mismatch failure,
complete-set preservation, and absence of `.staging`/`.work` debris. The
existing package tests cover wrong architecture, wrong ELF type, undefined
relocations, runtime limits, WPK limits, and atomic package writes.

The active interpreter lacks the pinned font dependencies (`fontTools`), so
the system-interpreter discovery run cannot execute the already-existing font
suite. In a temporary venv populated from the pinned requirements, the full
Python suite ran 41 tests successfully and `generate_fonts.py --check` was
clean. The stale motion assertion was corrected with the approved label
evidence above.

## Commits

- `36a2b56 fix: harden first-party package builder`
- `b14e076 fix: make first-party face helpers ABI-safe`
- `9734fc6 fix: reject stale face lifecycle contexts`
- `a9c326b docs: record Task 6 rescue evidence`
- `2c6ece2 docs: capture pinned Python verification`
- `96a56b3 test: keep first-party runner injectable`

Remaining work is Tasks 7–9 renderer implementation and their real ESP-IDF
build/audit evidence; this report intentionally does not claim those builds.
