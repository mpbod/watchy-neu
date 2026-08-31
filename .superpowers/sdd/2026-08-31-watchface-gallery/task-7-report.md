# Task 7: Grid first-party watchfaces

## Scope and implementation

Implemented the three independent Grid packages required by the gallery plan:

- Grid 01 (`watchy.firstparty.grid01`): tracked header, Heros 62 time, ruled
  footer cells, real battery percentage, and honest Bluetooth status.
- Grid 02 (`watchy.firstparty.grid02`): vertical full-date rail, agenda
  placeholders (`NO EVENT` and `--:--`), and bottom-baseline Heros time.
- Grid 03 (`watchy.firstparty.grid03`): large top time, date/weather/second-city
  cells, weather placeholders (`--°` and `NO DATA`), and fixed UTC+09 TYO time.

All three use ABI 1.2, version 1.0.0, a 49,152-byte runtime budget, and the
planned capabilities (787 for Grid 01; 515 for Grid 02/03). Renderers clear the
full 200x200 target, clip through the shared UI primitives, tolerate unavailable
optional capabilities with placeholders, and select Full only on first render
or an hour boundary; routine renders request Partial.

The PBM goldens use the fixed fixture of 2026-08-31 09:41, UTC+07, 68% battery,
and Bluetooth disabled. Host tests assert descriptor IDs/capabilities, native
ink geometry, exact PBM output, and Full/Partial/Full refresh sequencing.

## Verification

```text
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir build/host --output-on-failure
100% tests passed, 13/13

PYTHONPATH=. build/task6-venv/bin/python -m unittest discover -s tests/python -v
Ran 43 tests ... OK

PYTHONPATH=. build/task6-venv/bin/python tools/generate_fonts.py --check
font strikes are up to date
```

The first pinned host command initially failed because `cmake` was not on the
interactive PATH; rerunning with the pinned PlatformIO CMake binary succeeded.
The actual first-party builder was run through the installed ESP-IDF 5.5
toolchain. It configured and compiled Grid 01, but the local Xtensa ld 2.43.1
process segfaulted while producing `grid01.so`. A direct reproduction succeeds
when passing `--no-relax`, confirming a local linker-relaxation/toolchain issue;
the repository builder invocation itself remains unchanged and no WPK build is
claimed from this environment.

## Task 7 link-debug follow-up

At commit `4b566af`, the pinned ESP-IDF 5.5.0 / elf_loader 1.3.3 Grid01 `project_so`
link segfaulted in GNU ld 2.43.1 (`collect2 ... signal 11`) with production
`--gc-sections`. Full command/output are preserved in
`task-7-debug/idf-grid01-failing.log`; `--no-relax` was diagnostic only.

Object isolation showed that fonts, face, UI, renderer, and package objects each
triggered the crash with the bridge, while bridge-only and working sample objects
linked. A minimal fixture reproduced it with production hidden visibility. Working
samples explicitly export `watchy_package_entry` default-visible; making the Grid
wrapper default-visible changed the crash into relocation diagnostics. These then
identified const pointer-bearing generated font descriptors and the weekday
pointer table in read-only sections. The descriptor audit is RED before and GREEN
after the changes (`descriptor-audit-red.log`, `descriptor-audit-final2.log`).

The fix keeps the first-party descriptor mutable static, gives the public wrapper
default visibility, makes generated font records writable while retaining const
access through the API, and makes the weekday pointer table writable. No global
`--no-relax` workaround is used.

Pinned `build_first_party.py --only grid-01 grid-02 grid-03 --reproducible` completed
two clean rounds. Final WPK SHA-256 values:

* Grid01 `8dd10713cd5c86b3b09fb5064354ced514e5ae427b532f5992c651110cfc8347`
* Grid02 `722a29820a9689f866c6e932957b62050ef4bb4e0eeb45f29bdecc662f054428`
* Grid03 `3c554ef9e8fa29183c5080209d9f6a7212cb62fed70ab78928330cbcdd76effa`

Each ELF audit reported `exports=watchy_package_entry undefined=0 symbol_relocations=0`.
The final builder log is `task-7-debug/builder-all-section-attr.log`. A direct C
compile of generated fonts passed. Host/Python reruns are recorded in
`host-test-final.log` and `python-test-final.log`; this macOS environment lacks
`cmake` on PATH and Python `fontTools`.

## Files

The package projects, manifests, renderers, common Grid rendering helpers,
approved PBM goldens, and focused host coverage are included in this change.
