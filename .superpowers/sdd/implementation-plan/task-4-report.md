# Task 4 report: built-in shell, settings, recovery, and package portal

## Result

Task 4 was built on the approved HAL/package-runtime base commit `8a13e2c`.
The initial Task 4 implementation head was `b523279`; this report includes the
subsequent review-fix commit that contains the report itself (the exact final
HEAD is returned in the handoff).
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
  `src/idf_runtime.c`/`src/package_policy.c`: public catalog snapshot, removal,
  safe-mode purge, serialized streamed-upload staging APIs, a tested watchface
  render/refresh completion contract, and distinct upload finalization errors.
  Portal code uses these APIs instead of package index or package filesystem
  internals.
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
  `CMakeLists.txt`: portable behavior coverage for launcher mapping/paging,
  safe-mode per-package removal, failure transitions, diagnostics, persisted
  input grammar, entropy bracketing, public HTTP errors, and package lifecycle.

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
10. Review regression: a package that exited during `start` first made the
    watchface cycle report success without a render. Green requires one
    successful runner render (which includes display refresh) and successful
    cleanup; otherwise the built-in watchface fallback remains active.
11. Review regression: the launcher first indexed the raw catalog, included
    watchfaces, and hid entries after row seven. Green constructs an app-only
    mapping, tests 0/7/8/16 app boundaries, pages seven visible rows, maps every
    selection back to the right catalog entry, and renders bounded ordinal,
    reference-prefix, and stable-hash labels.
12. Review regression: readable safe-mode metadata first still produced only a
    purge-all action. Green snapshots metadata without loading package code,
    offers per-package selection/removal with a visible success result, and
    retains purge-all only for an unreadable/corrupt index.
13. Review regression: operation failures first remained on their source
    screens, and diagnostics rendered only a serial-log notice. Green sends
    settings/NVS, manual-time, NTP, portal, display, and package-action failures
    to a bounded stable error screen, and pages structured per-service
    `PASS`/`FAIL`/`N/A`/`OFF` diagnostics on the watch.
14. Review regression: permissive persisted strings first accepted path-like or
    incomplete TZ values, malformed DNS labels, and non-printable WPA bytes.
    Green uses a conservative POSIX-TZ parser, strict DNS labels, printable
    8-63 byte WPA passphrases or exact 64-hex keys, and empty passwords for open
    networks, with adversarial cases.
15. Review regression: short token strings first authorized, AP credentials
    were sampled before a guaranteed entropy source, and tokens were created
    before radio startup. Green requires exactly 32 hexadecimal token
    characters, brackets a 256-bit seed with ESP-IDF's early-boot hardware RNG
    before ADC setup, derives a fresh AP password per AP session with SHA-256,
    and generates each 128-bit mutation token after Wi-Fi startup.
16. Review regression: receive and staging/write/finalize failures first
    collapsed to HTTP 400 or `invalid_package`. Green distinguishes client
    receive 400, package validation 422, conflicts/battery 409, and storage
    probe/stage/write/fsync/close 507 with stable public JSON codes.

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
package 34, shell/settings 15, and portal 7 focused cases, plus SDK C/C++ ABI
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
- HTTP receive/client failures return 400 `upload_incomplete`; package
  validation returns 422 `invalid_package`; state conflicts and low battery
  return 409 codes; and storage probe/staging/write/fsync/close failures return
  507 `storage_error` (capacity shortage remains 507 `insufficient_storage`).
- `POST /api/v1/watchface/<id>/<version>/activate` and
  `DELETE /api/v1/packages/<id>/<version>` accept only exact method/path forms
  and validated URL-safe components.
- Every mutating route requires the random 128-bit-per-session token in
  `X-Watchy-Token`. Both values must be exactly 32 hexadecimal characters;
  comparison is constant-work after syntax validation. The token is generated
  after Wi-Fi startup and embedded only in the served page.
- AP sessions use `Watchy-<last-six-MAC-hex>` and a fresh 16-character WPA2
  password shown on the watch. A 256-bit seed is sampled under ESP-IDF's
  supported bootloader RNG enable/fill/disable bracket during early boot,
  before battery ADC initialization, and SHA-256 with a monotonic session
  counter derives each AP password. Client sessions use validated stored
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
$HOME/.platformio/packages/tool-ninja/ninja -C .pio/build/watchy_v2 \
  esp-idf/watchy_packages/watchy_package_smoke.so
build-host/tests/host/watchy_elf_fixture_validator \
  .pio/build/watchy_v2/esp-idf/watchy_packages/watchy_package_smoke.so
```

- Clean ESP32 / ESP-IDF 5.5.0 build: success in 42.79 seconds.
- RAM: 101,192 / 327,680 bytes (30.9%).
- Flash: 1,325,403 / 1,835,008 bytes (72.2%).
- The real Xtensa shared-object fixture passed the production validator.
- `-Wframe-larger-than=2048` is applied to main, shell, and package code.
  Final relevant frames: `app_main` 1,056 bytes and HTTP `receive_upload` 1,280
  bytes; no frame warning occurred.

## Self-review

- Re-read the Task 4 brief and checked each screen, wake, settings, recovery,
  route, upload, timeout, and information-disclosure requirement against the
  final tree.
- Confirmed safe mode bypasses all third-party load paths, is retained across
  deep sleep, renders without package storage, lists/removes individual package
  metadata when the index is readable, can purge a corrupt index and package
  tree through a public recovery API, and clears only on normal reboot.
- Confirmed package-render and package-app failures converge on built-in UI and
  warning state, and that RTC/button/motion wakes follow their distinct routes.
- Confirmed the HTTP handler owns only a 1 KiB receive chunk, package staging is
  exclusive, the full WPK is allocated only inside the existing bounded
  installation phase, and shutdown stops the HTTP task before aborting staging
  to avoid a descriptor race.
- Confirmed package removal commits a valid NVS index before best-effort tree
  cleanup and cannot leave active/pending/prior references to an uninstalled
  package.
- Confirmed AP passwords are freshly derived for every AP session from an
  early-boot hardware-entropy seed, session tokens are freshly sampled after
  network start, and neither secret is emitted by a JSON route or serial log.

## Concerns

- No physical Watchy, live AP/client association, browser upload, RTC/NTP, or
  e-paper interaction was available, so hardware behavior is not claimed.
- Client portal and NTP flows deliberately require credentials already present
  in validated settings; a fresh device can always use the generated AP portal.
- The HTTP server is intentionally plain HTTP on a short-lived local AP/LAN;
  mutating-route protection is the random on-page session token, not TLS.
