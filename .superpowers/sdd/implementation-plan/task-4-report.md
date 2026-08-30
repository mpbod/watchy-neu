# Task 4 report: built-in shell, settings, recovery, and package portal

## Result

Task 4 is implemented on the committed HAL/package runtime through `8a13e2c`.
The firmware now has filesystem-independent built-in rendering and navigation,
validated field-by-field NVS settings, latched safe-mode recovery, package app
launch/fallback behavior, manual and NTP time flows, and an actual ESP-IDF HTTP
package portal in both AP and stored-credential client modes.

The package ceilings remain unchanged: 80 KiB WPK, 64 KiB ELF, 32 KiB total
assets, 16 KiB manifest, and 80 KiB relocated/runtime allocation. HTTP receives
WPK bodies in 1 KiB chunks into package-manager-owned staging; it does not hold
the complete request body in the transport layer.

## Files

- `components/watchy_shell/`: new component with public settings, shell, UI,
  render, and portal contracts; compact 5x7 font/primitives; portable policies;
  field-wise NVS persistence; embedded mobile HTML/CSS/JS; and real
  `esp_http_server`/Wi-Fi adapters.
- `components/watchy_packages/include/watchy/package_runtime.h` and
  `src/idf_runtime.c`: public catalog snapshot, removal, safe-mode purge, and
  serialized streamed-upload staging APIs. Portal code uses these APIs instead
  of package index or package filesystem internals.
- `components/watchy_packages/include/watchy/packages.h` and
  `src/package_state.c`: transactional package unregister that compacts health
  and installed records and preserves valid active/pending/prior invariants.
- `components/watchy_hal/include/watchy/display.h`, `src/display.c`,
  `include/watchy/power.h`, and `src/power.c`: validated persisted partial
  refresh limit and motion-wake-aware deep-sleep source configuration.
- `main/main.c` and `main/CMakeLists.txt`: deterministic wake routing, built-in
  screen event loop, package app execution, built-in watchface fallback,
  latched safe mode, diagnostics, settings actions, manual time, NTP, portal
  lifecycle, and stack guards. Superseded placeholder `app_hooks.*` were
  removed.
- `tests/host/test_shell.c`, `test_portal.c`, `test_packages.c`, and
  `CMakeLists.txt`: portable behavior coverage and package unregister
  regression coverage.

## Red/green record

Production changes were preceded by focused failing host cases:

1. Shell/settings tests initially failed to configure because the new policy
   sources and headers did not exist. Green covers field-wise invalid-setting
   fallback, button/RTC/motion routing, launcher navigation, safe-mode routing,
   idle-to-watchface behavior, and package-render warning fallback.
2. Portal tests initially failed for missing routes and policy code. Green
   covers exact token matching, method/path parsing, invalid/query/escaped URL
   rejection, exact content type, known non-chunked length, 80 KiB ceiling,
   3.55 V threshold, package bytes plus 64 KiB free-space reserve, concurrent
   upload exclusion, stable package-error mapping, and idle deadline wrap.
3. Font and removal tests initially failed for missing `watchy/ui.h`, `ui.c`,
   and `watchy_package_unregister`. Green exercises real framebuffer pixels and
   transactional index/health/selection compaction.
4. Manual-time/portal-mode tests failed for missing edit state and explicit
   AP/client actions. Green makes portal startup a second deliberate Menu
   selection and makes manual increments/decrements/save observable actions.
5. Wi-Fi validation first accepted short PSKs and orphaned passwords. Green
   accepts open networks or valid 8-63 character PSKs (and 64-digit hex PSKs)
   and falls back safely for invalid persisted pairs.
6. The package list first exposed removal as its normal action. Green selects
   installed packages for lifecycle execution; removal stays in the portal and
   safe-mode recovery path.
7. Review regressions first reproduced acceptance of a quote in a URL version
   and failure to remove a pending watchface with a different active prior.
   Green uses a narrow URL-safe component profile and transactionally clears a
   removed pending/prior transition while retaining the active watchface.
8. A latched-safe-mode RTC wake first routed to the normal watchface. Green
   treats the state-machine input as a safe-mode request, so RTC/deep-sleep
   cycles remain in safe mode until the explicit normal reboot action.
9. The first clean guarded target build failed because `app_main` had a
   2,272-byte frame. The root cause was the automatic 16-entry package catalog;
   moving that serialized kernel object to static storage reduced the final
   `app_main` frame to 848 bytes.

Final host command:

```text
PATH="$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH" \
  cmake -S . -B build-host
PATH="$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH" \
  cmake --build build-host -j 4
PATH="$HOME/.platformio/packages/tool-cmake/bin:$HOME/.platformio/packages/tool-ninja:$PATH" \
  ctest --test-dir build-host --output-on-failure
```

Result: 7/7 CTest executables passed. The suites contain core 15, HAL 13,
package 32, shell/settings 9, and portal 5 focused cases, plus SDK C/C++ ABI
checks.

## Route and security behavior

- `GET /api/v1/status` returns only battery availability/mV/percent, storage
  availability/total/free, portal network mode, and IP address.
- `GET /api/v1/packages` returns reference, type, active, pending, and
  quarantine fields from a locked package-manager catalog snapshot.
- `POST /api/v1/packages` requires the exact
  `application/octet-stream` content type, a known nonzero non-chunked length,
  the existing 80 KiB WPK limit, at least 3.55 V, and free storage of body size
  plus 64 KiB. It streams into exclusive package staging and validates/installs
  through the package manager.
- `POST /api/v1/watchface/<id>/<version>/activate` and
  `DELETE /api/v1/packages/<id>/<version>` accept only exact method/path forms
  and validated URL-safe components.
- Every mutating route requires the random 128-bit-per-session token in
  `X-Watchy-Token`. Token comparison is exact and constant-work for equal
  lengths. The token is embedded only in the served page.
- AP sessions use `Watchy-<last-six-MAC-hex>` and a newly generated 16-character
  WPA2 password shown on the watch. Client sessions use validated stored
  credentials and show their assigned address. HTTP and all radios stop on
  Back, startup failure, normal sleep cleanup, or ten minutes without request
  activity.
- JSON errors expose stable codes only. Responses never contain Wi-Fi
  passwords, NVS values, filesystem paths, pointers, package internals, or
  diagnostic logs. Package-derived page content uses DOM `textContent`, and
  JSON package references are escaped.

## Target build

Commands:

```text
pio run -e watchy_v2 -t clean
pio run -e watchy_v2
build-host/tests/host/watchy_elf_fixture_validator \
  .pio/build/watchy_v2/esp-idf/watchy_packages/watchy_package_smoke.so
```

- Clean ESP32 / ESP-IDF 5.5.0 build: success in 44.13 seconds.
- RAM: 101,160 / 327,680 bytes (30.9%).
- Flash: 1,321,743 / 1,835,008 bytes (72.0%).
- The real Xtensa shared-object fixture passed the production validator.
- `-Wframe-larger-than=2048` is applied to main, shell, and package code.
  Final relevant frames: `app_main` 848 bytes and HTTP `receive_upload` 1,264
  bytes; no frame warning occurred.

## Self-review

- Re-read the Task 4 brief and checked each screen, wake, settings, recovery,
  route, upload, timeout, and information-disclosure requirement against the
  final tree.
- Confirmed safe mode bypasses all third-party load paths, is retained across
  deep sleep, renders without package storage, can purge a corrupt index and
  package tree through a public recovery API, and clears only on normal reboot.
- Confirmed package-render and package-app failures converge on built-in UI and
  warning state, and that RTC/button/motion wakes follow their distinct routes.
- Confirmed the HTTP handler owns only a 1 KiB receive chunk, package staging is
  exclusive, the full WPK is allocated only inside the existing bounded
  installation phase, and shutdown stops the HTTP task before aborting staging
  to avoid a descriptor race.
- Confirmed package removal commits a valid NVS index before best-effort tree
  cleanup and cannot leave active/pending/prior references to an uninstalled
  package.
- Confirmed AP passwords and session tokens are regenerated for every start;
  neither is emitted by a JSON route or serial log.

## Concerns

- No physical Watchy, live AP/client association, browser upload, RTC/NTP, or
  e-paper interaction was available, so hardware behavior is not claimed.
- Client portal and NTP flows deliberately require credentials already present
  in validated settings; a fresh device can always use the generated AP portal.
- The HTTP server is intentionally plain HTTP on a short-lived local AP/LAN;
  mutating-route protection is the random on-page session token, not TLS.
