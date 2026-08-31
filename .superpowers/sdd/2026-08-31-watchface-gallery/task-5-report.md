# Task 5 report

## Result

Replaced the incomplete shell renderer with a formatted, clipped C renderer in `components/watchy_shell/src/shell_render.c`. It has explicit paths for Hairline, Menu, selector, Settings, Apps, Manual Time, NTP, Connectivity, Portal, Diagnostics, About, Error, and Safe Mode. Menu, selector, and Settings use the handed-off 200x200 language: Plex header, Heros primary labels, Plex metadata, full selected-row inversion, x=44 labels, a white 13px right rail with black triangles, and a thumb calculated from `total - 1`. Selector status precedence is `QUARANTINED`, `PENDING`, `ACTIVE`, then version; Hairline is `BUILT-IN`.

Hairline uses Heros 40 at baseline y=91, Plex date at baseline y=175, uppercase `DD MON`, and the exact clamped battery rule width `(percent * 200 + 50) / 100` at y=197..199. Invalid/unknown battery values render a zero-width rule. SAFE and package-warning indicators do not move the core composition.

## Test evidence

`tests/host/test_shell_render.c` is unconditional. Every plain CTest run compares all four committed PBMs and independently checks:

- all three Menu selections, row bounds, rail boundary, label regions, and selected-row inversion;
- selector Hairline plus active, pending, quarantined, and version metadata fixtures across first/middle/last positions and page windows;
- all ten Settings labels/order fixtures, first/middle/last pages, and metadata variants;
- Apps filtering through `app_indices` and a second page;
- Hairline 0%, 68%, 100%, invalid battery, date/time regions, and SAFE/warning invariance;
- every old shell screen, actual diagnostic report statuses/details, and clipped-canvas guard bytes.

`check_shell_golden_mutation.cmake` mutates one byte of a committed PBM and requires the comparison to fail. It is registered as `watchy_shell_render_golden_mutation`.

## Verification

```text
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
12/12 tests passed

./build/host/tests/host/watchy_shell_render_tests --compare-menu /tmp/watchy-mutated-menu.pbm
mutation_exit=1 (expected rejection)

platformio run -e watchy_v2
SUCCESS; RAM 118924/327680 (36.3%), flash 1356631/1835008 (73.9%)

cat .pio/build/watchy_v2/components/watchy_shell/src/shell_render.c.su
watchy_shell_render frame: 272 bytes

git diff --check
clean
```

PBMs are under `tests/golden/shell/`. Native 1x and nearest-neighbor 4x PNG review artifacts plus SHA-256 values are under `.superpowers/sdd/2026-08-31-watchface-gallery/task-5-artifacts/`. PBM output was generated twice and byte-compared before hashes were refreshed. Native 1x images were inspected against the standalone HTML handoff for header/rail proportions, row geometry, and Hairline baselines.

## Commit

This rescue implementation is committed as a new `feat: complete watchface gallery shell renderer` commit; no prior history was amended.
