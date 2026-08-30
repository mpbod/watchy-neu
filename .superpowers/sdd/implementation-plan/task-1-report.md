# Task 1 Report

Date: 2026-08-30

## Changed files

- `tests/host/test_core.c`
- `sdk/include/watchy/sdk.h`
- `sdk/include/watchy/sdk.hpp`
- `components/watchy_core/CMakeLists.txt`
- `components/watchy_core/include/watchy/runtime.h`
- `components/watchy_core/include/watchy/wpk.h`
- `components/watchy_core/src/runtime.c`
- `components/watchy_core/src/refresh_policy.c`
- `components/watchy_core/src/wpk.c`
- `.superpowers/sdd/implementation-plan/task-1-report.md`

## Red / green verification

### Red: extended tests before implementation

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests
```

Exit code: `1`

Output:

```text
clang: error: no such file or directory: '/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c'
clang: error: no such file or directory: '/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c'
clang: error: no such file or directory: '/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c'
```

This captured the intended red state after adding tests for truncation,
overflow/out-of-order sections, stricter lifecycle rejection, explicit full
refresh reset, and ABI-minor compatibility.

### Green: direct host build and test binary

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests && /tmp/watchy_core_tests
```

Exit code: `0`

Output:

```text
PASS 10 tests
```

### Green: C++ header-only wrapper smoke compile

Command:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  -x c++ -c -o /tmp/watchy_sdkhpp_smoke.o -
```

Input:

```cpp
#include "watchy/sdk.hpp"

int main() {
    watchy::Canvas canvas;
    watchy::Clock clock;
    watchy::Input input;
    watchy::Motion motion;
    watchy::Battery battery;
    watchy::Haptics haptics;
    watchy::Storage storage;
    watchy::Network network;
    watchy::Bluetooth bluetooth;
    watchy::System system;

    (void)canvas.valid();
    (void)clock.now(nullptr);
    (void)input.is_pressed(WATCHY_BUTTON_UP);
    (void)motion.sample(nullptr);
    (void)battery.read(nullptr);
    (void)haptics.pulse(10, 1);
    (void)storage.write("/tmp", nullptr, 0);
    (void)network.connected();
    (void)bluetooth.enabled();
    (void)system.millis();
    return 0;
}
```

Exit code: `0`

Output:

```text
[no output]
```

### Environment gap noted

Command:

```sh
cmake -S /Users/maxb/Work/watchy-stuff/watchy-fw -B /tmp/watchy-fw-host-build
```

Exit code: `127`

Output:

```text
zsh:1: command not found: cmake
```

`IDF_PATH` was also unset in this environment, so ESP-IDF configuration was not
verified here.

## Design choices

- Kept `sdk.h` strictly C ABI friendly with fixed-width scalars, plain structs,
  function tables, and `extern "C"` guards so the same definitions work from C
  and C++.
- Made `sdk.hpp` header-only and non-owning by wrapping raw v1 function tables
  without allocating, transferring ownership, throwing exceptions, or using STL
  boundary types.
- Chose a 64-byte packed WPK header containing a stored SHA-256 digest and the
  three section ranges required by the tests. Parsing copies the header into an
  aligned local value and exposes the stored digest pointer without attempting
  verification.
- Enforced WPK section ordering directly as `manifest -> elf -> assets` with
  explicit overflow checks before arithmetic so malformed headers fail safely.
- Implemented the runtime lifecycle as a minimal one-package state machine with
  only the allowed forward path and unload-to-empty transition.
- Made refresh policy conservative: explicit full refresh always resets the
  partial counter, and hitting the configured partial limit promotes to full and
  resets the counter for the next cycle.

## Self-review

- The host tests now cover all required new edge cases from the task brief.
- The runtime and WPK code stay narrowly scoped to Task 1 and do not reach into
  loader, hardware, shell, portal, or tooling concerns.
- The C++ wrappers compile, but only through a smoke test; no behavioral host
  tests exercise real host capability tables yet.
- Component metadata is minimal on purpose so standalone host compilation is not
  coupled to ESP-IDF-specific logic.

## Concerns

- `cmake` is not installed in this environment, so I could not verify the host
  CMake path in addition to the required direct `cc` build.
- `IDF_PATH` is unset, so the new `components/watchy_core/CMakeLists.txt` was
  not exercised inside a real ESP-IDF configure/build in this session.

## Round 1 review fixes

### Files touched

- `tests/host/test_core.c`
- `tests/host/test_sdk_c.c`
- `tests/host/test_sdk_cpp.cpp`
- `tests/host/CMakeLists.txt`
- `sdk/include/watchy/sdk.h`
- `sdk/include/watchy/sdk.hpp`
- `components/watchy_core/src/wpk.c`
- `components/watchy_core/src/refresh_policy.c`

### Red: deterministic WPK layout test fails before parser change

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests_round1_red && /tmp/watchy_core_tests_round1_red
```

Exit code: `1`

Output:

```text
FAIL /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c:97: wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT
```

This is the first new gap check (`gap_before_manifest`) failing at assertion
level against the old permissive parser.

### Red: C ABI coverage fails while `size_t` leaks across the SDK boundary

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c \
  -o /tmp/watchy_sdk_c_tests_round1_red
```

Exit code: `1`

Output:

```text
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c:14:16: error: static assertion failed: watchy_host_caps_v1_t.size must be uint32_t
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c:55:17: error: incompatible function pointer types initializing 'watchy_status_t (*)(void *, const char *, void *, size_t, size_t *)' (aka 'watchy_status_t (*)(void *, const char *, void *, unsigned long, unsigned long *)') with an expression of type 'watchy_status_t (void *, const char *, void *, uint32_t, uint32_t *)' (aka 'watchy_status_t (void *, const char *, void *, unsigned int, unsigned int *)') [-Wincompatible-function-pointer-types]
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c:56:18: error: incompatible function pointer types initializing 'watchy_status_t (*)(void *, const char *, const void *, size_t)' (aka 'watchy_status_t (*)(void *, const char *, const void *, unsigned long)') with an expression of type 'watchy_status_t (void *, const char *, const void *, uint32_t)' (aka 'watchy_status_t (void *, const char *, const void *, unsigned int)') [-Wincompatible-function-pointer-types]
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c:68:100: error: incompatible pointer types passing 'uint32_t *' (aka 'unsigned int *') to parameter of type 'size_t *' (aka 'unsigned long *') [-Werror,-Wincompatible-pointer-types]
```

### Red: C++ wrapper coverage fails while wrapper signatures still mirror `size_t`

Command:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp \
  -o /tmp/watchy_sdk_cpp_tests_round1_red
```

Exit code: `1`

Output:

```text
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp:15:15: error: static assertion failed due to requirement 'std::is_same<unsigned long, unsigned int>::value': watchy_host_caps_v1_t.size must be uint32_t
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp:54:9: error: cannot initialize a member subobject of type 'watchy_status_t (*)(void *, const char *, void *, size_t, size_t *)' (aka 'watchy_status_t (*)(void *, const char *, void *, unsigned long, unsigned long *)') with an lvalue of type 'watchy_status_t (void *, const char *, void *, std::uint32_t, std::uint32_t *)' (aka 'watchy_status_t (void *, const char *, void *, unsigned int, unsigned int *)'): type mismatch at 4th parameter ('size_t' (aka 'unsigned long') vs 'std::uint32_t' (aka 'unsigned int'))
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp:55:9: error: cannot initialize a member subobject of type 'watchy_status_t (*)(void *, const char *, const void *, size_t)' (aka 'watchy_status_t (*)(void *, const char *, const void *, unsigned long)') with an lvalue of type 'watchy_status_t (void *, const char *, const void *, std::uint32_t)' (aka 'watchy_status_t (void *, const char *, const void *, unsigned int)'): type mismatch at 4th parameter ('size_t' (aka 'unsigned long') vs 'std::uint32_t' (aka 'unsigned int'))
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp:62:87: error: cannot initialize a parameter of type 'size_t *' (aka 'unsigned long *') with an rvalue of type 'std::uint32_t *' (aka 'unsigned int *')
/Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp:76:87: error: cannot initialize a parameter of type 'size_t *' (aka 'unsigned long *') with an rvalue of type 'std::uint32_t *' (aka 'unsigned int *')
```

### Green: host runtime tests after enforcing exact layout

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests_round1_green && /tmp/watchy_core_tests_round1_green
```

Exit code: `0`

Output:

```text
PASS 14 tests
```

### Green: C ABI coverage after fixed-width signature update

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c \
  -o /tmp/watchy_sdk_c_tests_round1_green && /tmp/watchy_sdk_c_tests_round1_green
```

Exit code: `0`

Output:

```text
PASS test_sdk_c
```

### Green: C++ wrapper coverage after fixed-width signature update

Command:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp \
  -o /tmp/watchy_sdk_cpp_tests_round1_green && /tmp/watchy_sdk_cpp_tests_round1_green
```

Exit code: `0`

Output:

```text
PASS test_sdk_cpp
```

### Appended self-review

- The ABI surface no longer exposes native-width integer types in the reviewed
  host capability size or storage callback signatures.
- The new C and C++ SDK tests prove both exact type expectations and simple
  callback/wrapper behavior, which closes the earlier blind spot where the
  wrapper only received a smoke compile.
- WPK parsing is now deterministic rather than merely non-overlapping: every
  section boundary must be exactly adjacent and `total_size` must match the end
  of the assets section.
- `tests/host/CMakeLists.txt` now knows about the extra SDK tests even though
  this environment still lacks `cmake`, so that integration path remains an
  external verification gap rather than an untracked omission.

## Round 2 review fix

### Files touched

- `tests/host/test_core.c`
- `components/watchy_core/src/wpk.c`

### Red: oversized caller blob is rejected only after tightening the size contract

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests_round2_red && /tmp/watchy_core_tests_round2_red
```

Exit code: `1`

Output:

```text
FAIL /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c:135: wpk_parse(bytes, sizeof(bytes), &view) == WPK_ERR_LAYOUT
```

This captures the root cause directly: a valid contiguous package whose
`header.total_size` ends at the assets section was still being accepted when the
caller passed a larger blob size.

### Green: host runtime tests after requiring exact caller blob size

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/include \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_core.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/wpk.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/runtime.c \
  /Users/maxb/Work/watchy-stuff/watchy-fw/components/watchy_core/src/refresh_policy.c \
  -o /tmp/watchy_core_tests_round2_green && /tmp/watchy_core_tests_round2_green
```

Exit code: `0`

Output:

```text
PASS 15 tests
```

### Green: full Task 1 SDK coverage still passes

Command:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_c.c \
  -o /tmp/watchy_sdk_c_tests_round2 && /tmp/watchy_sdk_c_tests_round2
```

Exit code: `0`

Output:

```text
PASS test_sdk_c
```

Command:

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -I/Users/maxb/Work/watchy-stuff/watchy-fw/sdk/include \
  /Users/maxb/Work/watchy-stuff/watchy-fw/tests/host/test_sdk_cpp.cpp \
  -o /tmp/watchy_sdk_cpp_tests_round2 && /tmp/watchy_sdk_cpp_tests_round2
```

Exit code: `0`

Output:

```text
PASS test_sdk_cpp
```

### Appended self-review

- The happy-path host fixture now passes `header->total_size` explicitly, so it
  no longer hides an oversized-input acceptance bug behind spare stack bytes.
- `wpk_parse` now has a single, tighter caller contract: undersized blobs are
  `WPK_ERR_TRUNCATED`, oversized blobs are `WPK_ERR_LAYOUT`, and exact-size
  blobs continue through section validation.
- The change stayed isolated to Task 1’s WPK contract and did not disturb the
  SDK ABI coverage added in round 1.
