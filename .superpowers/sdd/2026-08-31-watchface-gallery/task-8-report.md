# Task 8 — Term first-party watchfaces

## Implementation

Added three independent ABI 1.2 watchface packages:

| Face | Identifier | Capabilities | Runtime budget |
| --- | --- | ---: | ---: |
| Term 01 | `watchy.firstparty.term01` | 771 | 49,152 bytes |
| Term 02 | `watchy.firstparty.term02` | 531 | 49,152 bytes |
| Term 03 | `watchy.firstparty.term03` | 531 | 49,152 bytes |

The common `term_render.hpp` keeps the package path allocation-free and
package-safe: it obtains clock/battery/Bluetooth state through the host ABI,
uses fixed-size text, uses clipped `ui_draw` calls, and has no exceptions,
RTTI, STL, or ESP-IDF calls. Every renderer clears its 200 x 200 target and
uses the common full-first-render/hour-boundary refresh policy.

- Term 01 draws the inverse `WATCH.LOCAL` bar, radio-neutral `BT·--` status,
  real date/time, `NO DATA --°`, and a fixed 7 x 13 solid cursor. It has no
  idle animation or timer-driven cursor change.
- Term 02 uses only explicit UTC offsets: LON UTC+0, NYC UTC-5, and TYO UTC+9.
  It makes no DST claim. The three rows each draw exactly twelve clipped 7 x 9
  progress cells from their fixed-offset local minutes, plus real footer date
  and battery.
- Term 03 clears to black, renders white solid hours and one-pixel outlined
  minutes, and renders ten 14 x 6 segments with 3px gaps. Ceiling rounding is
  deliberate so the 68% fixture fills seven segments. Its footer is `--°` and
  explicit fixed-offset `TYO HH:MM`.

## Tests, fixtures, and audits

Independent pre-renderer assertions were added for descriptor identifiers,
capabilities, first/hour-boundary refresh behavior, Term 01 inverse header and
static cursor, Term 02 ruled header/footer and 3 x 12 progress cells, and Term
03 black-canvas polarity plus exactly seven filled interior battery segments.
The three PBM fixtures were added before the renderers and were frozen from the
native fixture run only after those assertions passed.

The successful milestone command was:

```text
cmake --build build/host --target watchy_first_party_face_tests && \
  WATCHY_UPDATE_GOLDENS=1 ./build/host/tests/host/watchy_first_party_face_tests
```

It built and ran successfully with the Term/Grid host cases. No full host or
firmware suite was run. Fonts did not change, so no generator check was needed.
The reproducible package command completed two clean rounds per package:

```text
python3 tools/build_first_party.py --only term-01 term-02 term-03 --reproducible
```

Each round passed `audit-elf` with
`exports=watchy_package_entry undefined=0 symbol_relocations=0`; all resulting
WPKs passed the package verifier and byte-for-byte reproducibility gate.

| Package | WPK bytes | File SHA-256 |
| --- | ---: | --- |
| Term 01 | 21,488 | `3b0a8628530cfc271ed293bb1caa7ca2c880c0d827672392d46b3cfe2efe161c` |
| Term 02 | 19,460 | `f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88` |
| Term 03 | 17,032 | `f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4` |

Native 1x and nearest-neighbor 4x PNGs are in `build/task8-artifacts/`.
Inspection against the geometry handoff found unclipped 12/13/14px insets,
Term 01 header/footer and cursor placement, Term 02 rules plus all thirty-six
progress-cell outlines, and Term 03 full inversion, outlined minute weight,
footer, and seven 68%-battery fills.

## Self-review

- Confirmed all text uses IBM Plex Mono strikes; no Grid renderer or Grid golden
  output was modified.
- Confirmed all panel writes use the clipped draw API and the only state is the
  existing common face lifecycle state.
- Confirmed LON/NYC/TYO offsets are explicit and local offset conversion handles
  date rollover through the shared checked helper.
- `git diff --check` reported no whitespace errors.

## Residual concern

The native PBM review shows no clipping at panel edges, but physical e-paper
contrast/ghosting inspection remains release-UAT work; no device was available
for this scoped task.

## Fix round 1 — Term 01 capability boundary

Review found that Term 01 declared capability 771, which intentionally omits
Battery, but its inverse header had read and displayed the fixture battery
percentage. The header now renders only `BT·ON` or radio-neutral `BT·--`; it
does not call the battery helper. The geometry handoff now records that this is
an approved omission rather than an incomplete status field.

The focused Term 01 host path now passes a host capability table with
`battery == nullptr` while retaining the fixed 68% fixture for Term 02/03.
Consequently, a renderer that reintroduces battery text/access produces a
different Term 01 PBM and fails the golden comparison. The repaired 1x and 4x
Term 01 artifacts were regenerated under `build/task8-artifacts/` and inspected:
the shorter Bluetooth-only status remains right-aligned within the header and
all handoff padding/cursor boundaries remain unclipped.

```text
cmake --build build/host --target watchy_first_party_face_tests && \
  WATCHY_UPDATE_GOLDENS=1 ./build/host/tests/host/watchy_first_party_face_tests
EXIT=0 (focused face target, golden round-trip comparison, no full suite)

python3 tools/build_first_party.py --only term-01 term-02 term-03 --reproducible
EXIT=0 (two clean rounds/package; ELF audit, WPK verification, reproducibility)
```

| Package | repaired WPK bytes | repaired file SHA-256 |
| --- | ---: | --- |
| Term 01 | 20,456 | `838f7253e86a71e990f0224b4512a24f321342592a36411f0675f1c6790318c8` |
| Term 02 | 19,460 | `f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88` |
| Term 03 | 17,032 | `f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4` |
