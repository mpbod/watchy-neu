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
