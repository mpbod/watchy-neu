# Task 1 — Deterministic Typography Handoff

## Result

Vendored the specified IBM Plex Mono and TeX Gyre Heros OTF sources with their
upstream license texts. `tools/generate_fonts.py` deterministically rasterizes
the configured glyphs into sorted, MSB-first one-bit strikes and produces the
standalone `watchy_font_t` ABI in `sdk/ui/generated/`.

## Official source provenance

| Asset | Official source | SHA-256 |
| --- | --- | --- |
| IBM Plex Mono Bold | https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/otf/IBMPlexMono-Bold.otf | `27a69e26eb871f52431f45fef5e938acd45e752d9132faaf38aa4a045168b3b6` |
| IBM Plex Mono Medium | https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/otf/IBMPlexMono-Medium.otf | `f7db820bddfbf7fce52946e69fee89a150938d401d82850cdf975b2c4c31b97b` |
| IBM Plex Mono Regular | https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/otf/IBMPlexMono-Regular.otf | `372fe8f8a459baef84ee346b0a478084e80c46bd299a0c27bcb0f3412f5a9d28` |
| IBM Plex Mono SemiBold | https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/otf/IBMPlexMono-SemiBold.otf | `bc63f5550b440d5edd4da15c4740e7468d2064a72f5509aca55348f930588267` |
| TeX Gyre archive | https://mirrors.ctan.org/fonts/tex-gyre.zip | `1773c470f9e388e087b68e3426e115af2cd236845a7e05ceb25b2a503409a7a3` |
| TeX Gyre Heros Bold | https://ctan.org/pkg/tex-gyre (archive member `tex-gyre/opentype/texgyreheros-bold.otf`) | `b170162835f4efc288886dd4231406dc47e19b614cf4416836635599d44a7d60` |
| TeX Gyre Heros Regular | https://ctan.org/pkg/tex-gyre (archive member `tex-gyre/opentype/texgyreheros-regular.otf`) | `6ae1a09d5a940367b7aaaa91ee8bd8a2c333bfe193e7096e23f931357d62081f` |

The brief's IBM Plex tree location has moved within the same official
repository. The direct official files above were discovered through the
repository's content API under `packages/plex-mono`; the requested filenames,
family, and weights are unchanged. No host-installed font was consulted.

## Licenses

- IBM Plex source license: `https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/otf/license.txt`, source SHA-256 `91c25c350d3cac39da2736d74f7ba37ef648f5237a4e330a240615bc8d8c4360`; vendored as `assets/fonts/ibm-plex-mono/OFL.txt` (SIL Open Font License 1.1, vendored SHA-256 `9590325331b1975eac408dc78e7d369c042f565cee8aa9e34d6b40524f400972`).
- TeX Gyre source license: `https://ctan.org/pkg/tex-gyre` archive member `tex-gyre/doc/GUST-FONT-LICENSE.txt`, source SHA-256 `2bd69affc3da00715116f713f57eab9707e96daf3562ad0215987b15b9c16f73`; vendored as `assets/fonts/tex-gyre-heros/GUST-FONT-LICENSE.txt` (GUST Font License, vendored SHA-256 `e120f067589d589a9fa436db09940bd2e03b477cf29b15443c6f87d3861f734b`).

The vendored copies preserve the license terms; their hashes differ from
upstream only for normalized line-ending/trailing-whitespace representation.
`tests/python/test_fonts.py` validates both license identities.

## Implementation

- `tools/font-requirements.txt` pins `fonttools==4.63.0` and `Pillow==12.3.0`.
- `tools/font_strikes.json` preserves every handed-off strike ID, font role,
  pixel size, threshold, and glyph override verbatim.
- `tools/generate_fonts.py` accepts only vendored source-path patterns,
  sorts/uniquifies codepoints, loads Pillow with `ImageFont.Layout.BASIC`,
  packs rows MSB-first, and emits one `const watchy_font_t` per strike.
- `sdk/ui/generated/watchy_fonts.h` and `.c` define `watchy_font_t`,
  `watchy_font_glyph_t`, all strike descriptors, and binary-search
  `watchy_font_find_glyph`.
- Both generated banners include computed generator, configuration, and source
  SHA-256 values, and intentionally contain neither timestamps nor absolute
  paths.

## TDD evidence

RED: before `tools/generate_fonts.py` existed,
`python3 -m unittest tests.python.test_fonts -v` failed with the expected
`FileNotFoundError` for that generator. A second focused RED test failed until
generated-header provenance contained every source digest.

GREEN (pinned isolated environment):

```text
build/font-venv/bin/python -m unittest tests.python.test_fonts -v
Ran 5 tests ... OK

build/font-venv/bin/python tools/generate_fonts.py --check
font strikes are up to date
```

The test suite renders twice in-process, checks byte equality against the
checked-in artifacts, validates sorted unique codepoints and source locations,
verifies banner hashes/no timestamp/no absolute worktree path, validates both
licenses, and compiles generated C with `cc -std=c11 -Wall -Wextra -Werror`.

## Deterministic artifact hashes

| File | SHA-256 |
| --- | --- |
| `sdk/ui/generated/watchy_fonts.h` | `251e4236117e48f004c7621edfc3fcbd0fed1569dd6e7be3ec929ef509f3d679` |
| `sdk/ui/generated/watchy_fonts.c` | `b2250bb963b66fb7a9eae4e48f477b8eb89fad9babb4199f91897b85abb02679` |
| `tools/font_strikes.json` | `7887311789cf9d1b059d74cf27c10df01cc9f707e15f2acc170042e9e2b9e9de` |
| `tools/generate_fonts.py` | `756dff070ac3258b9faa5f99bd9c80c4323d20b4cbf75a8a0e30aba2ff33249a` |

## Self-review and concerns

`git diff --check` was clean. The generator uses no system font discovery,
only the six specified vendored OTF paths. The temporary pinned virtualenv and
temporary generated artifacts were removed after evidence capture to keep disk
use bounded. No implementation concern remains; the IBM Plex repository path
move is recorded above for reproducible provenance.

## Commit

Commit subject: `feat: add deterministic handoff typography`

## Fix Round 1

Review findings are addressed by `tools/font_provenance.lock.json`. Its font
allowlist is exactly the six approved OTF paths and each entry records the
expected file SHA-256 plus either an IBM Plex commit-pinned raw URL
(`bf260093582f04622aacc1e9f9ca604d7ccd0c42`) or the CTAN archive SHA-256
and exact archive member. The generator independently validates the lock,
the exact OTF directory contents, regular/non-symlink paths, and every locked
hash before it reads configuration or emits output.

The lock also covers the canonicalized full-text hashes of OFL, GUST Font
License, and a newly vendored `LPPL-1.3c.txt`, because the GUST license refers
to LPPL 1.3c. Canonicalization is intentionally limited to CR/LF conversion,
trailing spaces/tabs at line ends, and one final newline; tests demonstrate
that interior whitespace is preserved.

`tests/python/test_fonts.py` now hard-codes the complete 17-strike contract
independently of `font_strikes.json`, validates all required codepoints against
each source OTF's FontTools cmap, and exercises provenance rejection outside
the allowlist. The generator validates every C ABI integer before emission:
glyph count, bitmap length/offset/extent, codepoint, bearings, dimensions,
advance, stride, pixel size, ascent/descent, and line height. Diagnostics name
the strike and glyph codepoint. Focused pinned verification passed with 11/11
tests, C compilation, and byte-identical `--check` regeneration.

Updated deterministic artifact hashes:

| File | SHA-256 |
| --- | --- |
| `sdk/ui/generated/watchy_fonts.h` | `4a5c1172e88cabb31c2d3846643a92fccc84ec9501d96bf37ce88ca9c519e07b` |
| `sdk/ui/generated/watchy_fonts.c` | `9a04ffcdf072770efa0f4baf8454438b4aa37b066b7d48942bcebd3672c9087d` |
| `tools/font_provenance.lock.json` | `4c80cd9fde73951c7df2097c9717de4938c930fd06f5a045bf65f5a633a54787` |
| `assets/fonts/tex-gyre-heros/LPPL-1.3c.txt` | `d7bd10db098a03db9e36e41f3e6f67548602d11486904fb4f1d98b3d64ab7258` |
