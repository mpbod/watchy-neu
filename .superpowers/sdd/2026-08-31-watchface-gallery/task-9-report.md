# Task 9 — Slab and Orbit first-party watchfaces

## Implementation

Added two independent ABI 1.2 WPK projects, both with stable capability mask
`531` and the standard 49,152-byte runtime budget:

| Face | Identifier | Layout / data contract |
| --- | --- | --- |
| Slab | `watchy.firstparty.slab` | Exact 100px white/black slabs; cropped Heros 82 hours and inverted minutes; Plex 9 weekday, `DD MON`, and real battery in their handed-off corners. |
| Orbit | `watchy.firstparty.orbit` | 14px inset; bounded local moon calculation from the approved 2000-01-06 18:14 UTC epoch; 62px two-pixel phase disc; three geometric glyphs; `--° · NN%`; Heros 60 clock; 3px rule; date and UTC+09:00 `TYO` footer. |

Both renderers clear the full 200 x 200 canvas and use the common first-render
and hour-boundary full-refresh policy. Routine same-hour frames are partial.
There are no network calls, DST claims, direct ESP-IDF calls, heap/STL
ownership, exceptions, or RTTI.

`moon_octant()` now uses bounded signed 32-bit modular arithmetic. It reduces
the day offset and performs the 86,400-second multiply by repeated bounded
addition, which preserves the approved epoch result while avoiding freestanding
Xtensa `__divdi3`/`__moddi3` imports.

Fixed renderer font-style structs are deliberately placed in writable `.data`:
the package loader can relocate their generated-font pointers there, whereas
the linker correctly rejects dynamic relocations in `.rodata`.

## Tests and fixtures

Added Slab/Orbit entries to `watchy_first_party_face_tests` and native PBM
fixtures before the production renderer implementation. The tests independently
assert descriptor metadata, first/hour-boundary refresh policy, Slab half
polarity, cropped numeral regions and corner metadata, Orbit 62px disc/rule/
footer anchors, and its top-right status composition.

Orbit additionally renders four hand-derived UTC fixtures, not just the golden:

| Phase | UTC fixture | Expected octant | Interior samples |
| --- | --- | ---: | --- |
| New | 2000-01-06 18:14 | 0 | black / black |
| First quarter | 2000-01-14 03:26 | 2 | black / white |
| Full | 2000-01-21 12:37 | 4 | white / white |
| Last quarter | 2000-01-28 21:48 | 6 | white / black |

The first milestone attempt exposed that the original last-quarter timestamp
was two seconds before the integer octant boundary. The literal fixture was
moved safely inside octant 6; it does not alter production moon math.

Focused host verification after the final renderer/common-helper changes:

```text
cmake --build build/host --target watchy_first_party_face_tests && \
  ./build/host/tests/host/watchy_first_party_face_tests
EXIT=0
```

No font source or generated strike changed, so no font-generation check was
needed. No full firmware suite was run, per task scope.

## Package audit and reproducibility

The final command completed two clean native ESP-IDF rounds for every selected
face; every resulting ELF passed `exports=watchy_package_entry undefined=0
symbol_relocations=0`, every WPK verified, and each pair byte-compared:

```text
python3 tools/build_first_party.py --reproducible
EXIT=0
```

| Package | Bytes | SHA-256 |
| --- | ---: | --- |
| grid-01.wpk | 28,060 | `465a34f57d867c580075052d56f3c608c86242528118fb5e4e06c91b87356dc8` |
| grid-02.wpk | 21,452 | `abb2a52cb898b293c3fbe6da8c0fa432f8f7883c82fb05736c61b460418d2674` |
| grid-03.wpk | 23,648 | `2c3b978153d6b57bad454a811f877ecab17cde949cfc3a17c9c3d83daccb5687` |
| orbit.wpk | 16,869 | `7fa83099e530cd7291f0f37bbc115c94573eae166953a928b1a888cb4dc5ae33` |
| slab.wpk | 16,595 | `ef516afbeefca6a7af36d40d7fb1b8057c2de508363351aec92e3e9b02ad9270` |
| term-01.wpk | 20,456 | `838f7253e86a71e990f0224b4512a24f321342592a36411f0675f1c6790318c8` |
| term-02.wpk | 19,460 | `f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88` |
| term-03.wpk | 17,032 | `f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4` |

During the first audit, Orbit exposed package-linker errors from two font
pointer relocations in `.rodata`, then from compiler-provided 64-bit division
helpers used by the original lunar reduction. Direct ELF relocation inspection
identified both causes. The writable fixed-style data and bounded modular moon
implementation above produced the clean final audit; a direct Orbit ELF check
also reported `undefined=0 symbol_relocations=0` before the all-eight rerun.

## Native visual evidence

Generated and inspected these uncommitted review artifacts under
`build/task9-artifacts/`:

- `slab-1x.png`, `slab-4x.png`
- `orbit-1x.png`, `orbit-4x.png`
- `orbit-phases-contact.png` from the actual four phase render assertions

Inspection found Slab's exact horizontal inversion boundary, surviving one-bit
Plex stems, intentionally cropped large numerals, and uncut lower corner
metadata. Orbit shows the 14px disc inset, two-pixel outline, uncut phase
interiors for all four cardinal phases, intact 3px rule, readable fixed-offset
footer, and no panel-edge clipping. Artifact SHA-256 values:

```text
slab-1x.png              7a527b7ba00a87218683dc7abcf1443cbba54006a898b283356caef777f95f45
slab-4x.png              03e4315654e1000be3a1d2cb0b076a0e158e9098724b4ae3b2633aeedf3cc8cd
orbit-1x.png             df8514db528ee5651e27f110d76f9f7242b8a2664bcd74a7fe57c96586836519
orbit-4x.png             a04f6eca5a51add5a80bbeeb5182734d0b1ea0eced6b99bac585e3d516589f21
orbit-phases-contact.png c6aeef67ce8970bc4fe4ea5a34065c2198800b8fb6b90232231d20b2d7f6e136
```

## Self-review and residual concern

`git diff --check` is clean. Reviewed package manifests, ABI 1.2 descriptors,
capability mask, full canvas clearing, fixed-offset-only TYO wording, and
clipped draw paths. The builder-generated absolute dependency-lock rewrites
were intentionally discarded; the committed locks use portable relative paths.

The remaining concern is physical e-paper contrast/ghosting and device-scale
appearance, especially the intentionally cropped 82px Slab figures. No Watchy
hardware UAT is claimed in this scoped task.

## Repair round 1 — handoff geometry correction

The Slab 82px Heros baselines were corrected from the rejected `111/205` to
`82/182`. The white hour ink is now centered in y=0..99 and the inverse minute
ink in y=100..199; the only retained cropping is the font's natural boundary.
The weekday/date/battery corners remain unobscured. New hand-derived assertions
require substantial large-number ink away from each boundary and require zero
hour ink at y=96..99 and zero inverse-minute ink at y=196..199. Thus a divider
or panel-edge-amputated number cannot pass merely through its PBM golden.

Orbit now maps every moon octant exactly as follows: `NEW`, `WAX CRES`,
`FIRST QTR`, `WAX GIBB`, `FULL`, `WAN GIBB`, `LAST QTR`, `WAN CRES`.
The header's three fixed handoff glyphs are a filled 10x10 square at (92,35),
an outlined 10x10 square at (106,35), and a filled 10px circle in x=120..129 /
y=35..44. They have no invented dynamic meaning. Every cardinal fixture now
independently compares the literal expected label and glyph pixels in addition
to its phase-disc geometry: NEW (2000-01-06 18:14 UTC), FIRST QTR
(2000-01-14 03:26), FULL (2000-01-21 12:37), and LAST QTR (2000-01-28 21:48).
This rejects the former generic `PHASE` title and the former outlined
square/circle/diamond row.

At the repaired milestone gate the targeted host face test passed without
golden rewriting:

```text
cmake --build build/host --target watchy_first_party_face_tests && \
  ./build/host/tests/host/watchy_first_party_face_tests
EXIT=0
```

`python3 tools/build_first_party.py --reproducible` also passed for all eight
WPKs (two reproducible rounds each; package ELF audit has zero undefined
symbols and zero symbol relocations). The builder's absolute local dependency
lock rewrites were reverted to portable relative paths after the audit.

The regenerated native artifacts were visually compared with
`reference-slab.png` and `reference-orbit.png`: Slab has centered, unclipped
large figures with correct upper/lower inversion, and Orbit has the intended
right-header label/glyph geometry, inset disc, rule, and unclipped footer.
The four-panel phase contact sheet was also inspected for one-bit stem survival
and all cardinal phase polarities. Repaired artifact SHA-256 values:

```text
slab-1x.png              930c06f0fce7d57faf8275739998c61dde1889a9f0ddd2b73ae678e8aeb122e7
slab-4x.png              bf3c551ceb9d1da58bea4681344b786473ce9838e56fbc0ffd23c44aec84fd85
orbit-1x.png             7dd29167b8d90154ade7eeec30768a50968590e45a8c6de5d2c1052179463eb5
orbit-4x.png             b9b18a14826420deb683384ea951ca5002060a2a309da0594b03122ddc969dd4
orbit-phases-contact.png 4e3c628d5f2bae9b58bec199b32fcd3a57ef19bbac9190be563b115739da959f
```

| Package | Bytes | SHA-256 after repair |
| --- | ---: | --- |
| grid-01.wpk | 28,060 | `465a34f57d867c580075052d56f3c608c86242528118fb5e4e06c91b87356dc8` |
| grid-02.wpk | 21,452 | `abb2a52cb898b293c3fbe6da8c0fa432f8f7883c82fb05736c61b460418d2674` |
| grid-03.wpk | 23,648 | `2c3b978153d6b57bad454a811f877ecab17cde949cfc3a17c9c3d83daccb5687` |
| orbit.wpk | 16,985 | `8b5c20086e75ab11d9a988cd37f8fb923347f44125ea2ed8177adeaac21e8efa` |
| slab.wpk | 16,595 | `e1ca5d417205108a38bdc238b65eee180885da02a2d272ec644f506385855ea8` |
| term-01.wpk | 20,456 | `838f7253e86a71e990f0224b4512a24f321342592a36411f0675f1c6790318c8` |
| term-02.wpk | 19,460 | `f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88` |
| term-03.wpk | 17,032 | `f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4` |

Final self-review: `git diff --check` is clean. No generated font changed,
therefore no font check was required; no full firmware suite was run by scope.
Remaining concern is still hardware-only e-paper contrast/ghosting; this repair
does not claim device UAT.
