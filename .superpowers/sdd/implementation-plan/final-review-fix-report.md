# Final whole-branch review fix report

Date: 2026-08-30
Baseline: `fd304cd55047f79708e2633263b996400c1b31a1`

This was the single final whole-branch fix wave. It preserves the pure ESP-IDF
Watchy 2.0 architecture, ABI 1.1, the existing WPK ceilings, the single
`watchy_package_entry` ELF export, and the built-in UI/recovery path. No physical
Watchy result is claimed here; every item in `docs/hardware-acceptance.md`
remains open.

## Finding resolutions and rationale

### Critical 1: portal authentication and lifetime

- Every `GET`, `POST`, and `DELETE` route now authenticates before routing or
  activity accounting. The credential is a fresh 128-bit value shown in full on
  the watch, used as the HTTP Basic password with username `watchy`, and never
  inserted into HTML, JavaScript, JSON, logs, or public errors.
- Unauthenticated requests do not refresh the ten-minute idle deadline. A
  separate thirty-minute absolute deadline cannot be extended, including while
  upload chunks are arriving. HTTP 401 responses advertise the Basic challenge.
- HTTP Basic was selected deliberately: it protects the initial `GET /` without
  first serving a bearer secret, while same-origin browser requests can reuse the
  credential entered from the watch. STA portal support remains available.
- The watch instruction formatter bounds every displayed line and includes the
  complete username, token, SSID, AP password when applicable, and address.

### Important 1: fresh-device Wi-Fi provisioning

- Authenticated AP mode exposes `POST /api/v1/wifi`; client mode rejects that
  route. SSID/password validation is atomic, sensitive temporary buffers are
  cleared, settings are persisted, and the shell reloads settings after portal
  shutdown.
- Shell/NTP, portal client mode, and package networking now share the NVS
  namespace and keys in `watchy/wifi_credentials.h` (`watchy_cfg`, `ssid`, and
  `wifi_pass`). An erased device can therefore be provisioned in AP mode and the
  saved credentials are subsequently consumed by all STA paths.

### Important 2: watchdog return-path enforcement

- `CONFIG_ESP_TASK_WDT_PANIC=y` is enabled. Every package callback completion now
  reports whether it returned inside the 4000 ms budget; entry, load, start,
  event, render, stop, and unload paths reject an over-budget return and close
  the session.
- Cleanup status is propagated through `runner_finish`, including explicit stop
  and package-requested exit. Failed cleanup is recorded as an unsuccessful
  attempt, so pending rollback, failure accounting, and eventual quarantine are
  retained instead of reporting success.
- A truly non-returning callback is still handled by the on-target task-WDT
  panic/reset. Reset and persistent quarantine behavior remains a physical test.

### Important 3: ELF containment

- Firmware now proves `sh_offset <= file_end` before subtracting, then checks the
  section size against the remaining PT_LOAD file range. Matching hostile ELF
  fixtures are rejected by both the C validator and Python tool.

### Important 4: upload peak RAM

- Portal finalization allocates one bounded internal-RAM WPK buffer. The
  installer reuses that mutable allocation for the durable staging readback
  instead of allocating a second full WPK.
- Allocation is rejected unless both total internal free heap and the largest
  internal block admit the request while retaining a 32 KiB internal reserve.
  This choice preserves the existing transactional installer and exact 80 KiB
  WPK ceiling without introducing a second parser implementation.
- Transaction tests exercise aliased caller/readback storage and both heap
  boundaries. Maximum-size fragmented-heap behavior remains open on hardware.

### Important 5: asynchronous radios

- Network/BLE operations execute once, remain pending until the underlying
  state reaches a terminal observation, and fail after a wrap-safe 15 s
  deadline. Cancellation and timeout roll back started operations.
- A terminal result occupies its slot until the package observes it, so it
  cannot be overwritten by a new request. Package host teardown stops all
  package-owned radios.

### Important 6: package recovery

- A fresh empty index is persisted immediately. Boot now cross-validates every
  indexed package directory, manifest identity/version/type, ELF, and declared
  asset type/size, commits an unregister/pruned index before deletion, and then
  removes stale staging entries and unindexed package versions.
- State directories without an installed package are removed. Exact
  `.watchy-state-XXXXXXXX` files are scrubbed at boot and host initialization;
  ordinary user files with similar names are preserved.
- Purge commits an empty index before deleting staging, packages, and state.
  Last-version removal commits unregister first, then removes package-ID and
  state directories. If deletion is interrupted, the already-committed index
  makes the next boot retry safe.
- Attempt accounting now begins before manifest/ELF preflight, so invalid
  pending selections cannot bypass rollback/failure accounting.

### Important 7: reset, RTC, and safe-mode power routing

- Back+Down is accepted on cold boot and other non-deep-sleep reset causes, but
  not on RTC/timer/button/motion deep-sleep wakes. Timer wake is represented
  distinctly.
- Invalid RTC state routes cold/button/other boots to manual-time recovery.
  Noninteractive timer wakes remain low duty. If the minute timer cannot be
  configured, a fail-closed button-only sleep path disables stale wake sources
  and arms only the buttons.
- Latched safe mode skips the 30 s interactive shell on RTC, timer, and motion
  wakes. Normal sleep setup also disables stale wake sources before rearming.

### Important 8: hardware sample

- The sample uses `assets/probe.bin` and `state/presses.bin`, which match the
  real host path contract. The kernel runner now delivers Back to an app first;
  it only performs fallback termination if the package remains active.
- The host test uses the production path resolver and app-button adapter,
  verifies persisted state, and proves the sample's Back exit request is
  reachable.

### Important 9: reproducibility

- CI uses immutable image
  `espressif/idf@sha256:00e94c6ff8bc1f7bd22b234ff43db3f3056cb33dedac6819df9fbedbcb5c6ebb`.
- Firmware requires ESP-IDF `==5.5.0`; firmware, SDK template, and both samples
  require `espressif/elf_loader` `1.3.3`. Generated dependency locks are
  committed and record loader component hash
  `95d0cc617069a3f0b935d9b20bdc6065e6be8fc65fb63215fd492f0ac0588954`.
- Flexible `1.3.*`/`1.3.x` source, manifest, and current-document pins were
  removed. Historical reports were intentionally not edited.

### Minor 1: Python atomic output failures

- A valid WPK now reaches the atomic output path before injected `write`,
  `fsync`, and `replace` failures. Each case preserves the previous destination
  and removes the temporary sibling. A destination symlink is rejected without
  changing its target.

### Minor 2: PCF8563 alarm contract

- `Clock::set_alarm` now programs the available PCF8563 minute/hour/day/weekday
  next-match fields and enables AIE while clearing AF. The full `watchy_time_t`
  must be valid; year, month, seconds, and UTC offset do not participate in the
  hardware match. SDK comments and documentation state that this is not an
  absolute timestamp alarm.

### Minor 3: diagnostic claims

- Built-in diagnostic entries are explicitly passive. A successful passive
  initialization/read is rendered as `READY`; only a separately scoped active
  acceptance result may render `PASS`. Documentation and the checklist now say
  exactly what the passive screen establishes.
- Narrowing the claim was chosen instead of synthetic automated acceptance:
  buttons, haptics, radios, persistent storage, and display quality require
  human observation or instruments on the physical watch.

### Minor 4: physical checklist integrity

- No checklist box was checked. Display, input, sensors, radio, filesystem,
  package execution/recovery, maximum upload heap, wake behavior, safe mode,
  and deep-sleep current remain explicitly unaccepted until device evidence is
  recorded.

## Focused red/green evidence

The amended tests cover Basic authentication and activity/lifetime policy,
erased-settings provisioning, watchdog callback return policy, hostile ELF
containment, single-buffer install and heap admission, async terminal/cancel/
timeout behavior, recovery transaction decisions and temporary names, reset/
RTC/sleep routing, the real hardware-sample adapter, exact dependency pins,
atomic Python failures, PCF8563 alarm bytes, and passive-vs-active diagnostics.

Representative RED observations before their production changes were:

- portal watch-instruction and absolute-upload checks failed because no secure
  formatter/absolute upload enforcement existed;
- the hostile section-offset fixture was accepted by the firmware validator;
- async tests allowed a queued request to become immediately successful and an
  unseen terminal result to be overwritten;
- the one-buffer installer and hardware sample adapter tests failed before the
  mutable staging alias and real runner helper existed;
- the valid destination-symlink test failed before explicit rejection;
- the watchdog facade regression failed because `runner_finish` was `void` and
  runner stop discarded callback cleanup failure.

Each focused regression passed after the corresponding change. The final
watchdog regression command was:

```text
python3 -m unittest tests.python.test_reproducibility.ReproducibilityContracts.test_runner_stop_propagates_callback_cleanup_failure -v
Ran 1 test ... OK
```

## Final verification evidence

### Fresh host build and CTest

```text
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . \
  -B /tmp/watchy-final-verify-host.TKVYFd -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/Users/maxb/.platformio/packages/tool-ninja/ninja \
  -DCMAKE_C_COMPILER=/usr/bin/cc -DCMAKE_CXX_COMPILER=/usr/bin/c++
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build \
  /tmp/watchy-final-verify-host.TKVYFd
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir \
  /tmp/watchy-final-verify-host.TKVYFd --output-on-failure
```

Result: configuration succeeded, 59/59 build steps succeeded, and 9/9 tests
passed (0 failed, 3.44 s).

### Python

```text
python3 -m unittest discover -s tests/python -v
```

Result: 19/19 tests passed (0 failed).

### Real sample ELF/WPK builds and audits

```text
python3 tools/build_samples.py --output-dir build/final-sample-wpk
```

Result:

- digital watchface: exactly `watchy_package_entry`, zero undefined symbols,
  zero symbol relocations; WPK 8685 bytes,
  SHA-256 `32efe27880d4b5a2bec27e37a44544924e8bcc101ffd9a606e2b21af4af1a4fe`;
- hardware app: exactly `watchy_package_entry`, zero undefined symbols, zero
  symbol relocations; WPK 11994 bytes,
  SHA-256 `0555a011994e7b25bd31816185667c71f0e6face3f78ff1ecc2945387b9d607c`.

PlatformIO emitted its known sample-firmware flash-size mismatch warning and
the shared-object linker emitted the expected RWX LOAD warning. Both sample
builds, explicit ELF audits, WPK builds, and WPK verification commands returned
success.

Production C validators from the fresh host build returned:

```text
watchy_elf_fixture_validator digital_watchface.so
PASS Xtensa fixture runtime_bytes=1442
watchy_elf_fixture_validator hardware_demo.so
PASS Xtensa fixture runtime_bytes=2626
watchy_wpk_fixture_validator digital.wpk
PASS WPK sample.digital@1.0.0 runtime_bytes=1442 assets=0
watchy_wpk_fixture_validator hardware-demo.wpk
PASS WPK sample.hardware@1.0.0 runtime_bytes=2626 assets=5
```

### Firmware target builds

```text
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Result: success; RAM 111004/327680 bytes (33.9%), flash 1330851/1835008
bytes (72.5%).

For the clean reproducible build, a source-only tar stream (excluding `.git`,
build outputs, managed components, and generated sdkconfig files) was built in:

```text
espressif/idf@sha256:00e94c6ff8bc1f7bd22b234ff43db3f3056cb33dedac6819df9fbedbcb5c6ebb
idf.py -B /tmp/watchy-idf-final set-target esp32
idf.py -B /tmp/watchy-idf-final build
grep -q '^CONFIG_ESP_TASK_WDT_PANIC=y$' /work/sdkconfig
```

Corrected final result: 1458/1458 build steps succeeded, loader 1.3.3 was
resolved, `watchy_fw.bin` was `0x146300` bytes, the `0x1c0000` app partition had
`0x79d00` bytes (27%) free, and `WATCHDOG_PANIC_CONFIG=PASS` was printed. An
earlier wrapper invocation had also completed all 1458 build steps but pointed
the post-build grep at the build directory; the corrected command above
returned exit 0.

### Repository audits

`git diff --check` passed. Targeted scans found no served bearer token, embedded
JavaScript token, credential logging, or flexible current loader pin. The only
`1.3.x` scan hit was in an untouched historical report. No checked item exists
in `docs/hardware-acceptance.md`; `docs/implementation-plan.md`, the progress
ledger, and prior reports are unchanged. Generated `.pio`, managed-component,
sdkconfig, and WPK outputs remain ignored and uncommitted.

## Remaining hardware-only concerns

The following require a physical Watchy 2.0 and remain open:

- AP and saved-STA portal behavior, browser Basic flow on every route,
  provisioning persistence, unauthorized/idle/absolute expiry, upload during
  expiry, absence of credentials in serial logs, and radio shutdown;
- maximum-size WPK installation under real heap fragmentation, measured largest
  internal block/reserve, flash wear, and power interruption at each install,
  index, remove, purge, and state-write boundary;
- task-WDT reset, reboot attempt accounting, rollback, and persistent
  quarantine for returning and non-returning package overruns;
- Wi-Fi/BLE event timing, terminal failure, deadline, cancellation rollback,
  and teardown on the real radio stacks;
- PCF8563 next-match alarm timing, interrupt flag clearing/rearming, timer/RTC/
  motion/button wake routing, non-deep-reset safe chord, button-only fallback,
  and safe/normal deep-sleep current;
- installation/execution of both real sample packages, display rendering, Back
  exit, and `state/presses.bin` persistence across reboot/removal;
- display quality and refresh, all buttons, BMA423 data/wake, battery
  calibration, haptics, NTP/RTC setting, NVS/LittleFS persistence, recovery UI,
  and built-in diagnostics against instruments and human observation.
