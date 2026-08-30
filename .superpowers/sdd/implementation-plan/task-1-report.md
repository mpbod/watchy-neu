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
