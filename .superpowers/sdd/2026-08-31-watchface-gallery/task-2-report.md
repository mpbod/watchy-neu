# Task 2 Report: Shared One-Bit Typography and Drawing

## Scope

Implemented the package-safe C11 UI renderer described by Task 2:

- clipped one-bit pixels, fills, filled/outlined rectangles, horizontal rules, and filled/outlined circles;
- generated-font measurement and drawing with baseline/bearing metrics, tracking without a trailing gap, center/right alignment, inversion, and outline-only glyph rendering;
- strict UTF-8 scalar decoding with deterministic one-byte progress for invalid input and `?` fallback for invalid or missing glyphs;
- the temporary legacy `watchy_ui_draw_text` compatibility entry point;
- shared shell and package CMake integration using the same `ui_draw.c` and generated font source.

No generated font bytes, font configuration/provenance files, or Task 3 catalog code were changed.

## TDD Evidence

### Baseline

Using the workspace-pinned CMake and Ninja tools:

```text
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure

100% tests passed, 0 tests failed out of 9
```

### RED

The complete `tests/host/test_ui_draw.c` behavioral suite and its target were added before the API or implementation. The required target then failed for the expected missing interface:

```text
cmake -S . -B build/host -G Ninja
cmake --build build/host --target watchy_ui_draw_tests

test_ui_draw.c:1:10: fatal error: 'watchy/ui_draw.h' file not found
ninja: build stopped: subcommand failed.
```

This failure was caused by the absent Task 2 API, not by test syntax or fixture setup.

### GREEN

After the minimal API, implementation, and source integration were added:

```text
cmake --build build/host --target watchy_ui_draw_tests
./build/host/tests/host/watchy_ui_draw_tests

[4/4] Linking C executable tests/host/watchy_ui_draw_tests
ui draw tests passed
```

The target compiles with C11, `-Wall -Wextra -Werror`, `-ffunction-sections`, and `-fdata-sections`.

## Behavioral Coverage

The new host suite checks:

- guard preservation plus left, right, top, and bottom clipping for pixels, shapes, and partially visible glyphs;
- filled and multi-pixel outlined rectangles;
- one-, two-, and three-pixel rules;
- filled and outlined circle geometry;
- generated baseline and bearing placement;
- advance/tracking measurement with no trailing tracking and deterministic empty input;
- normal, outline-only, centered, right-aligned, and white-on-black glyph drawing;
- configured middle dot, degree, right arrow, em dash, and hyphen strings (`BT·--`, `--°`);
- malformed continuation bytes, overlong forms, UTF-16 surrogate encodings, values above U+10FFFF, truncated sequences, and valid-but-missing codepoints;
- equal nonzero digit advances in the 62-pixel Heros strike;
- null, invalid-format, and missing-font inputs.

The UTF-8 policy consumes exactly one byte for every invalid sequence start. Each invalid byte resolves to `?`, guaranteeing forward progress and avoiding reads past NUL. A valid codepoint absent from a strike also resolves to `?`; a strike without `?` skips that codepoint deterministically. Measurement and rendering use the same resolver and advance/tracking loop.

## Package and Shell Integration

`watchy_package_add_ui_sources(target)` adds only:

- `sdk/ui/src/ui_draw.c`;
- `sdk/ui/generated/watchy_fonts.c`;
- SDK/UI/generated public include paths;
- C11 plus function/data section compilation for link-time strike removal.

The shell component and host shell tests now compile the same shared sources instead of `components/watchy_shell/src/ui.c`. The compatibility entry points preserve the current shell ABI while later tasks migrate call sites.

An undefined-symbol inspection of the host `ui_draw.c` object showed only stack-protector symbols and `watchy_font_find_glyph`; it has no allocator, C++ runtime, STL, RTTI, exception, or ESP-IDF dependency.

## Final Verification

Final fresh verification used the pinned CMake/Ninja path and produced:

```text
cmake -S . -B build/host -G Ninja
cmake --build build/host --target watchy_ui_draw_tests
./build/host/tests/host/watchy_ui_draw_tests

ninja: no work to do.
ui draw tests passed

cmake --build build/host
ctest --test-dir build/host --output-on-failure

100% tests passed, 0 tests failed out of 10

git diff --check

(no output; exit 0)
```

## Fix Round 1

### Review Findings Addressed

- Replaced the generated-font black-pixel-count Unicode check with an independent font fixture. Its `?`, U+00B0 degree, U+00B7 middle dot, U+2014 em dash, and U+2192 right arrow glyphs have distinct advances and bit patterns. Measurement is asserted exactly at 21 pixels, and drawing is asserted at each expected set and clear pixel. A separate valid U+2603 absent-scalar case proves the distinct `?` advance and bitmap are used.
- Added a POSIX protected-page regression. For every prefix shorter than valid two-, three-, and four-byte sequences—including the empty prefix—the terminating NUL is the final readable byte before a `PROT_NONE` page. Both `watchy_ui_measure_text` and `watchy_ui_draw_text_font` run for every prefix. The fixture checks exact fallback advances, pixels, framebuffer guards, and deterministic `munmap` cleanup. Non-POSIX builds retain a guarded portability stub; the supported macOS host executes the protected-memory path.
- Added exact negative and positive `bearing_x` placement assertions.
- Added a font without `?` and proved valid-missing and invalid input are skipped without advance, tracking, or drawing, while surrounding supported input retains its normal advance.
- Deleted the obsolete `components/watchy_shell/src/ui.c`. `watchy/ui.h` is now only an include-compatible shim over `watchy/ui_draw.h`, leaving `sdk/ui/src/ui_draw.c` as the sole symbol implementation.
- Added a post-link CMake/nm assertion. The host test links with Darwin `dead_strip` or ELF `--gc-sections`, verifies referenced `watchy_font_heros_62_bold` remains, and fails if unreferenced `watchy_font_heros_82_bold` remains. This proves the per-object data sections emitted by `watchy_package_add_ui_sources` are actually removable by a garbage-collecting final link.

### Fix Round 1 RED Evidence

Before enabling the host final-link garbage collector, the new linked-symbol gate failed as intended:

```text
cmake --build build/host --target watchy_ui_draw_tests

CMake Error at tests/host/check_ui_sections.cmake:18 (message):
  unreferenced Heros 82 strike was not garbage-collected
ninja: build stopped: subcommand failed.
```

The stronger Unicode fixture exercises existing correct decoder behavior, so its sensitivity was verified with a temporary mutation that resolved every non-ASCII scalar as `?`. The rebuilt executable failed at the exact independent-width assertion:

```text
CHECK failed at tests/host/test_ui_draw.c:321:
watchy_ui_measure_text(&style,
  "\\xc2\\xb0\\xc2\\xb7\\xe2\\x80\\x94\\xe2\\x86\\x92").width == 21
```

The mutation was immediately reverted before implementation and is not present in the final diff.

### Fix Round 1 GREEN Evidence

```text
cmake -S . -B build/host -G Ninja
cmake --build build/host --target watchy_ui_draw_tests
./build/host/tests/host/watchy_ui_draw_tests

ui draw tests passed

nm -g build/host/tests/host/watchy_ui_draw_tests |
  rg 'watchy_font_(heros_62_bold|heros_82_bold)'

0000000100008158 S _watchy_font_heros_62_bold
```

The absent Heros 82 match is the asserted garbage-collection result. The shared renderer object remains package-safe:

```text
nm -u build/host/tests/host/CMakeFiles/watchy_ui_draw_tests.dir/__/__/sdk/ui/src/ui_draw.c.o

___stack_chk_fail
___stack_chk_guard
_watchy_font_find_glyph
```

There are no heap, C++ runtime, RTTI, exception, STL, or ESP-IDF imports.

Full host verification after removing the duplicate implementation:

```text
cmake --build build/host
ctest --test-dir build/host --output-on-failure

100% tests passed, 0 tests failed out of 10

git diff --check

(no output; exit 0)
```
