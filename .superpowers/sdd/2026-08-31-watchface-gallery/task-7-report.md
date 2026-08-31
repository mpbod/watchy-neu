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

## Visual-alignment fix round (2026-09-01)

The native-scale handoff review corrected the initial geometry drift before
freezing the PBMs:

- Grid 01 now uses a 14px inset, `MON` at left and `DD MON` at right, a bold
  Heros 62 clock with the handoff whitespace, and an inset footer whose right
  link value is right-aligned to avoid clipping.
- Grid 02 now uses a 26px vertical `MON DD MON YYYY` rail, `NEXT` followed by
  `NO EVENT` and `--:--`, no agenda heading/rule, and a regular Heros 62
  bottom-baseline clock.
- Grid 03 now ends its top row at y=118, uses regular Heros 74 numerals,
  weekday/day/month date content, weather placeholders, and a fixed-offset TYO
  clock without fake battery or displaced UTC detail.

Only deterministic Heros regular 62px and 74px strikes were added. Their
generated C outputs retain the existing source/license provenance banners.

### Commands and results

```text
python3 tools/generate_fonts.py
python3 tools/generate_fonts.py --check
font strikes are up to date

/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host-make --target watchy_first_party_face_tests -j2
./build/host-make/tests/host/watchy_first_party_face_tests
100% built; test exited 0

WATCHY_UPDATE_GOLDENS=1 ./build/host-make/tests/host/watchy_first_party_face_tests
./build/host-make/tests/host/watchy_first_party_face_tests
goldens regenerated; clean comparison exited 0

env IDF_PATH=/Users/maxb/.platformio/packages/framework-espidf \
  IDF_PYTHON_ENV_PATH=/Users/maxb/.espressif/python_env/idf5.5_py3.14_env \
  ESP_IDF_VERSION=5.5.0 \
  ESP_ROM_ELF_DIR=/Users/maxb/.platformio/packages/tool-esp-rom-elfs \
  PATH=/Users/maxb/.platformio/packages/toolchain-xtensa-esp-elf/bin:/Users/maxb/.platformio/packages/tool-ninja:/Users/maxb/.platformio/packages/tool-cmake/bin:$PATH \
  python3 tools/build_first_party.py --only grid-01 grid-02 grid-03 --reproducible
EXIT=0
all three projects: clean round 1 + clean round 2, Xtensa ELF audit,
WPK build/verify, and byte reproducibility comparison passed
```

Final published package audits and hashes:

| package | WPK bytes | SHA-256 |
| --- | ---: | --- |
| Grid 01 | 23,912 | `7730424423d7595896f4394c54aae98db8bbb2193a6705902984ecb5d03b6c28` |
| Grid 02 | 18,036 | `e5342077618509c64fd3e834c38448d66ddca636e4cd0552eaa82154ca02c1c8` |
| Grid 03 | 21,708 | `c3353d5d52e0fef02503d707856118991dbb3db3c6043061f0f38d2577f846bb` |

Each published WPK independently re-verified as ABI 1.2 with its expected
identifier/version; the build log reports `exports=watchy_package_entry
undefined=0 symbol_relocations=0` for each ELF. Native 1x and nearest-neighbor
4x PNGs are under `build/task7-artifacts/` and were inspected at all panel
edges. No Task 8 or Task 9 files were touched. Per the direct fix-round
instruction, no additional full-suite or firmware command was run after the
milestone gate.

### Self-review

The independent host assertions cover the fixed fixture, descriptor metadata,
font roles, content fit, region geometry, full-clear behavior, exact standards
PBM polarity, and Full/Partial/Full refresh sequencing. Optional capability
failures still resolve to honest placeholders and Grid 03 performs no battery
or radio read. The remaining acceptance concern is ordinary device-side
e-paper tuning of negative tracking at native scale; the reviewed 1x/4x
artifacts show no panel-edge clipping, but physical hardware was not available.

## Reviewer fix round 2 (2026-09-01)

The follow-up visual review findings were addressed before the repaired package
gate. Grid 02 now composes `MON 31 AUG 2026` as one tracked Plex 9 semibold
line and rotates the glyph raster into the 26px rail with explicit canvas
clipping. Grid 01 uses distinct Plex 10 semibold header and Plex 8 semibold
footer roles. All three Heros clock styles use non-negative tracking (zero),
with bounded one-bit raster fitting for the 150/170/190px handoff regions;
the independent checks cover the generated raw widths and those caps.

```text
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host-make --target watchy_first_party_face_tests -j2
./build/host-make/tests/host/watchy_first_party_face_tests
100% built; targeted face test exited 0

WATCHY_UPDATE_GOLDENS=1 ./build/host-make/tests/host/watchy_first_party_face_tests
./build/host-make/tests/host/watchy_first_party_face_tests
three PBM goldens regenerated; clean comparison exited 0

python3 tools/generate_fonts.py --check
font strikes are up to date

magick tests/golden/faces/grid-01.pbm -resize 200x200 -filter point build/task7-artifacts/grid-01-1x.png
magick tests/golden/faces/grid-02.pbm -resize 200x200 -filter point build/task7-artifacts/grid-02-1x.png
magick tests/golden/faces/grid-03.pbm -resize 200x200 -filter point build/task7-artifacts/grid-03-1x.png
magick tests/golden/faces/grid-01.pbm -resize 800x800 -filter point build/task7-artifacts/grid-01-4x.png
magick tests/golden/faces/grid-02.pbm -resize 800x800 -filter point build/task7-artifacts/grid-02-4x.png
magick tests/golden/faces/grid-03.pbm -resize 800x800 -filter point build/task7-artifacts/grid-03-4x.png
native 1x and nearest-neighbor 4x artifacts inspected; header/footer/rail/cell edge audits clean

env IDF_PATH=/Users/maxb/.platformio/packages/framework-espidf \
  IDF_PYTHON_ENV_PATH=/Users/maxb/.espressif/python_env/idf5.5_py3.14_env \
  ESP_IDF_VERSION=5.5.0 \
  ESP_ROM_ELF_DIR=/Users/maxb/.platformio/packages/tool-esp-rom-elfs \
  PATH=/Users/maxb/.platformio/packages/toolchain-xtensa-esp-elf/bin:/Users/maxb/.platformio/packages/tool-ninja:/Users/maxb/.platformio/packages/tool-cmake/bin:$PATH \
  python3 tools/build_first_party.py --only grid-01 grid-02 grid-03 --reproducible
EXIT=0
all three projects: clean round 1 + clean round 2, Xtensa ELF audit, WPK build/verify,
and byte reproducibility comparison passed
```

Repair-round published WPKs (the package verifier hashes the verified payload):

| package | bytes | verifier SHA-256 | file SHA-256 |
| --- | ---: | --- | --- |
| Grid 01 | 28,060 | `77ec7ff193e27b1c7c775bb6dd5328eaf51c5146917cf8eeffb38b30782c0296` | `465a34f57d867c580075052d56f3c608c86242528118fb5e4e06c91b87356dc8` |
| Grid 02 | 21,452 | `56f4fbec15e50d1c05f67c9b43887f130d99c0015b4af012daf273870e73e7cc` | `abb2a52cb898b293c3fbe6da8c0fa432f8f7883c82fb05736c61b460418d2674` |
| Grid 03 | 23,648 | `27157af48e5c265d1f3b59e11aa9dd116e3751e8fb24ccdf397ecb6194d55a3d` | `2c3b978153d6b57bad454a811f877ecab17cde949cfc3a17c9c3d83daccb5687` |

Each repaired ELF reported `exports=watchy_package_entry undefined=0
symbol_relocations=0`, and each published WPK passed the verifier. The only
remaining concern is physical e-paper inspection; the native artifacts show
no clipping at any panel edge. No Task 8 or Task 9 files were touched. Per
the scoped instruction, no additional full-suite or firmware command was run.
