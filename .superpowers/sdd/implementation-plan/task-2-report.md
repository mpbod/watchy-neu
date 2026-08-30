# Task 2 Report

Date: 2026-08-30

## Outcome

Implemented the pure ESP-IDF 5.5.x Watchy 2.0 project foundation, one focused
`watchy_hal` component, the required host-testable policies, and a minimal boot
application. The existing SDK/core headers and their binary layout were consumed
without modification. No ELF runtime, portal, or package tooling was added.

## Changed files

Project and target configuration:

- `.gitignore`
- `partitions.csv`
- `platformio.ini`
- `sdkconfig.defaults`
- `components/watchy_hal/CMakeLists.txt`
- `components/watchy_hal/idf_component.yml`
- `main/CMakeLists.txt`
- `main/idf_component.yml`

Public HAL headers:

- `components/watchy_hal/include/watchy/board.h`
- `components/watchy_hal/include/watchy/battery.h`
- `components/watchy_hal/include/watchy/buses.h`
- `components/watchy_hal/include/watchy/buttons.h`
- `components/watchy_hal/include/watchy/diagnostics.h`
- `components/watchy_hal/include/watchy/display.h`
- `components/watchy_hal/include/watchy/display_policy.h`
- `components/watchy_hal/include/watchy/haptics.h`
- `components/watchy_hal/include/watchy/motion.h`
- `components/watchy_hal/include/watchy/power.h`
- `components/watchy_hal/include/watchy/radios.h`
- `components/watchy_hal/include/watchy/rtc.h`
- `components/watchy_hal/include/watchy/rtc_calendar.h`
- `components/watchy_hal/include/watchy/storage.h`

HAL implementations:

- `components/watchy_hal/src/battery.c`
- `components/watchy_hal/src/battery_policy.c`
- `components/watchy_hal/src/bus_internal.h`
- `components/watchy_hal/src/buses.c`
- `components/watchy_hal/src/buttons.c`
- `components/watchy_hal/src/buttons_policy.c`
- `components/watchy_hal/src/diagnostics.c`
- `components/watchy_hal/src/display.c`
- `components/watchy_hal/src/display_policy.c`
- `components/watchy_hal/src/framebuffer.c`
- `components/watchy_hal/src/haptics.c`
- `components/watchy_hal/src/motion.cpp`
- `components/watchy_hal/src/power.c`
- `components/watchy_hal/src/power_policy.c`
- `components/watchy_hal/src/radios.c`
- `components/watchy_hal/src/radios_policy.c`
- `components/watchy_hal/src/rtc.c`
- `components/watchy_hal/src/rtc_calendar.c`
- `components/watchy_hal/src/storage.c`
- `components/watchy_hal/src/storage_policy.c`

Application and tests:

- `main/app_hooks.c`
- `main/app_hooks.h`
- `main/main.c`
- `tests/host/CMakeLists.txt`
- `tests/host/test_hal.c`
- `.superpowers/sdd/implementation-plan/task-2-report.md`

## Design and implementation notes

- Pins are centralized in `watchy/board.h` using the exact Task 2 values.
- The custom partition table exactly fills 4 MiB: NVS `0x9000/0x6000`, PHY
  `0xf000/0x1000`, factory `0x10000/0x1c0000`, and LittleFS
  `0x1d0000/0x230000`.
- Managed dependencies are pinned to `joltwallet/littlefs==1.22.3` and
  `lewisxhe/sensorlib==0.4.1`. SensorLib is MIT licensed and provides pure
  ESP-IDF PCF8563/BMA423 support; the BMA423 service uses it directly. The RTC
  service uses checked PCF8563 register transactions on the shared new I2C
  driver so each operation can propagate an error status.
- The display owns a 5000-byte current framebuffer and an RTC-retained,
  checksummed 5000-byte previous-RAM shadow plus partial-refresh count. Invalid
  retained state forces a full update; cadence advances only after a completed
  update. It clips framebuffer writes, synchronizes SSD1681 old and new RAM,
  uses the required full/partial/power/deep-sleep command values, and times out
  active-high BUSY waits after 10 seconds.
- BMA423 optional temperature and step fields carry explicit supported,
  unsupported, or unavailable statuses. Values are never presented without the
  corresponding status.
- Battery ADC uses 16 samples, ESP32 line-fitting calibration, the Watchy 2.0
  2:1 divider, a conservative piecewise LiPo percentage curve, and an exact
  install gate of 3550 mV.
- Wi-Fi and BLE remain stopped at normal boot. Both use explicit initialized,
  active, stopping, and stopped states. Wi-Fi has stored STA and AP boundaries;
  BLE gates teardown on advertising-stop completion and provides
  non-connectable diagnostics advertising.
- Diagnostics only returns structured service results. It never renders UI and
  never enters deep sleep.
- Loader and safe-mode placeholders are isolated in `main/app_hooks.*`; no
  runtime implementation is present.

## Strict red/green evidence

All portable behavior was introduced test-first. The host CMake executable was
configured with the cached PlatformIO CMake because `cmake` is not on the shell
`PATH`.

### Environment discovery

Command:

```sh
cmake -S . -B build-host && cmake --build build-host --target watchy_hal_tests
```

Exit code: `127`

Output:

```text
zsh:1: command not found: cmake
```

The cached binary used thereafter was
`/Users/maxb/.platformio/packages/tool-cmake/bin/cmake`.

### RED 1: clipped framebuffer

Command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . -B build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host --target watchy_hal_tests
```

Exit code: `2`

Relevant output:

```text
Undefined symbols for architecture arm64:
  "_watchy_framebuffer_draw_pixel"
  "_watchy_framebuffer_fill"
  "_watchy_framebuffer_fill_rect"
```

After adding only `framebuffer.c`, the same build plus
`./build-host/tests/host/watchy_hal_tests` exited `0` with:

```text
PASS 1 HAL test
```

### RED 2: raw wake GPIO to semantic button mask

Command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host --target watchy_hal_tests
```

Exit code: `2`

Relevant output:

```text
Undefined symbols for architecture arm64:
  "_watchy_buttons_decode_wake_gpio"
```

After the minimal policy implementation, the test exited `0` with:

```text
PASS 2 HAL tests
```

### RED 3: cold-boot safe-mode chord

The same build command exited `2` with:

```text
Undefined symbols for architecture arm64:
  "_watchy_buttons_is_safe_mode_chord"
```

After implementing the Back+Down subset check, the test exited `0` with:

```text
PASS 3 HAL tests
```

### RED 4: conservative battery boundaries

The same build command exited `2` with:

```text
Undefined symbols for architecture arm64:
  "_watchy_battery_is_install_safe"
  "_watchy_battery_percent_from_mv"
```

After implementing the fixed, hand-tested curve anchors and 3550 mV gate, the
test exited `0` with:

```text
PASS 4 HAL tests
```

### RED 5: wake-cause classification

The same build command exited `2` with:

```text
Undefined symbols for architecture arm64:
  "_watchy_power_map_wake"
```

After implementing cold/ext0/ext1 classification, including mixed ext1 button
priority, the test exited `0` with:

```text
PASS 5 HAL tests
```

### Full host GREEN

Command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . -B build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir build-host --output-on-failure
```

Exit code: `0`

Output summary:

```text
4/4 Test #4: watchy_hal_tests ................. Passed
100% tests passed, 0 tests failed out of 4
```

## Target build evidence

The target build used only the installed PlatformIO platform, ESP-IDF framework,
and tool packages:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

The first attempt reached PlatformIO but exited `1` before configuration:

```text
Error: Missing the `src` folder with project sources.
```

`platformio.ini` now explicitly maps `src_dir = main`, preserving the standard
ESP-IDF `main/` layout.

The next passes exercised the real Xtensa compiler and exposed/resolved:

- ESP32 uses ADC line-fitting rather than curve-fitting calibration;
- a local GPIO configuration variable shadowed `gpio_config()`;
- Bluedroid's advertising API requires a mutable parameter object;
- registry SensorLib 0.4.1 exports `SensorBMA423.hpp` at its top include level.

The final target command exited `0` using:

```text
framework-espidf @ 3.50500.0 (5.5.0)
toolchain-xtensa-esp-elf @ 14.2.0+20241119
```

Final fresh output summary:

```text
RAM:   [==        ]  16.9% (used 55312 bytes from 327680 bytes)
Flash: [====      ]  41.4% (used 758895 bytes from 1835008 bytes)
Successfully created esp32 image.
========================= [SUCCESS] =========================
```

No environment blocker remains for compilation. Physical flash/boot testing was
not possible because no Watchy hardware or serial port was provided.

## Review fix round 1

The review findings were addressed without changing the SDK/core ABI or adding
runtime, portal, or package-tooling work.

### Files changed in the fix round

- Display retention/BUSY behavior: `watchy/display_policy.h`,
  `display_policy.c`, `display.c`.
- Calendar/RTC behavior: `watchy/rtc_calendar.h`, `rtc_calendar.c`, `rtc.c`.
- Sleep admission and wake behavior: `watchy/power.h`, `power_policy.c`,
  `power.c`, `main.c`.
- Radio teardown behavior: `watchy/radios.h`, `radios_policy.c`, `radios.c`.
- Storage recovery behavior: `watchy/storage.h`, `storage_policy.c`,
  `storage.c`.
- Failure propagation/diagnostics: `watchy/haptics.h`, `haptics.c`,
  `battery.c`, `motion.cpp`, and `diagnostics.c`.
- Build/test registration: `components/watchy_hal/CMakeLists.txt`,
  `tests/host/CMakeLists.txt`, and `tests/host/test_hal.c`.

### Fix-round RED/GREEN evidence

The portable review regressions were added before their policy modules. The
first configure/build command was:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . -B build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host --target watchy_hal_tests
```

It exited `1` at RED because the newly registered implementation did not exist:

```text
Cannot find source file:
  ../../components/watchy_hal/src/display_policy.c
No SOURCES given to target: watchy_hal_tests
```

The completed portable suite covers:

- SSD1681 BUSY high means busy, followed by two low settling samples;
- invalid retained display state forces full refresh, while a valid warm state
  preserves the exact old framebuffer and only advances cadence on commit;
- valid/invalid BCD nibbles, leap day, month length, PCF8563 century-bit decode
  and encode, Unix epoch conversion, and positive local offset conversion;
- fail-closed sleep admission for every required condition plus three-sample
  inactive-source debounce;
- INT1-only motion wake classification and no reconnect while Wi-Fi is stopping;
- fully erased versus nonblank LittleFS media classification.

The BUSY test was also mutation-checked by temporarily inverting the production
predicate to `!busy_high`. The build plus test command exited `1` with:

```text
FAIL tests/host/test_hal.c:103:
!watchy_display_busy_observe(&filter, true)
FAIL ... test_ssd1681_busy_is_active_high_and_requires_settling() == 0
```

After restoring active-high behavior and forcing a clean host rebuild (the two
same-second edits otherwise preserved the object timestamp), the executable
exited `0` with `PASS 11 HAL tests`.

GREEN command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . -B build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir build-host --output-on-failure
```

Exit code: `0`.

```text
4/4 Test #4: watchy_hal_tests ................. Passed
100% tests passed, 0 tests failed out of 4
PASS 11 HAL tests
```

### Fix-round target build evidence

An integration build first found one ESP-IDF-specific include error:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Exit code: `1`.

```text
components/watchy_hal/src/power.c:14:10: fatal error:
esp_reset_reason.h: No such file or directory
```

ESP-IDF 5.5 declares `esp_reset_reason()` in `esp_system.h`. After correcting
that include, the final verification removed the target build tree and compiled
the entire ESP-IDF target again:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2 -t clean && \
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Both commands exited `0`. Final clean-build summary:

```text
framework-espidf @ 3.50500.0 (5.5.0)
toolchain-xtensa-esp-elf @ 14.2.0+20241119
RAM:   [==        ]  16.9% (used 55528 bytes from 327680 bytes)
Flash: [====      ]  42.8% (used 785679 bytes from 1835008 bytes)
Successfully created esp32 image.
========================= [SUCCESS] Took 40.98 seconds =========================
```

No compile blocker remains. No device was attached, so flashing, serial boot,
RF lifecycle, waveform, and deep-sleep current/wake validation remain physical
test items.

### Fix-round behavioral notes

- `RTC_DATA_ATTR` retains the exact prior display frame, cadence, magic,
  version, and checksum through deep sleep. A bad magic/checksum can never
  authorize partial refresh.
- Display hibernation and ordinary/failure deinit are distinct: successful
  `0x10 0x01` hibernation leaves RESET unasserted and makes RESET/DC inputs.
- PCF8563 registers now hold UTC. The local offset is restored from NVS before
  conversion and is persisted when local time is set. The century bit is
  explicit: set means 19xx, clear means 20xx.
- Sleep is admitted only after the RTC timer result, radio stop, motor-off,
  display hibernation, RTC flag clear, motion wake setup, ext0, ext1, and pin
  inactivity are all verified. Active sources are debounced both before
  teardown and immediately after wake-source registration. Failure returns to
  `app_main`, which logs and remains awake.
- ext1 contains the four buttons and BMA423 INT1 only. INT2 remains available
  to the motion driver but is neither an ext1 source nor a motion wake decode.
- Wi-Fi marks STOPPING before disconnect, reconnects only in STA active states,
  unregisters only non-null handler instances, and propagates stop errors. BLE
  marks STOPPING, blocks new advertising, waits up to 1 second for advertising
  stop completion, then tears down host/controller while accumulating errors.
- LittleFS first mounts without formatting. Only a byte-for-byte erased
  LittleFS partition is provisioned; nonblank mount failure is marked corrupt,
  preserved, and surfaced in diagnostics.
- Uninitialized value-returning services now distinguish `INVALID_STATE` from
  null/invalid arguments. Motor-off errors propagate, radio/power diagnostics
  reflect lifecycle state, and undefined sleep wake is classified as cold only
  when ESP-IDF reports a power-on reset.

## Hardware assumptions

- BMA423 address is `0x18` (SDO low), matching `BMA4_I2C_ADDR_PRIMARY` in the
  sibling InkWatchy Watchy 2.0 checkout.
- SSD1681 BUSY is treated as active high: high means the controller is busy;
  readiness requires two consecutive low samples 10 ms apart. This follows the
  controller review finding and still needs an oscilloscope/logic-analyzer
  confirmation on the supplied panel and board revision.
- Watchy 2.0 buttons are externally biased. Internal pulls are not enabled,
  especially because GPIO35 has no normal internal pull resistor.
- BMA423 interrupt 1 is configured active high, level-triggered, push-pull so it
  remains compatible with ext1 `ANY_HIGH` deep-sleep wake.
- PCF8563 INT is active-low/open-drain and uses ext0 with a pull-up.
- The battery ADC divider is 2:1, derived from the reference firmware's
  `ADC_VOLTAGE_DIVIDER 500.0f` convention (`ADC mV / 500 = battery V`).
- Watchy 2.0 has no charge-status GPIO in the supplied pinout. The battery state
  therefore exposes `watchy_battery_charging_supported() == false`; callers must
  not interpret the placeholder `charging=false` as a measured value.
- LittleFS is formatted only when mounting fails and every byte of the target
  partition is still erased (`0xff`). A nonblank corrupt filesystem is preserved
  and reported rather than erased.
- PCF8563 stores UTC. The local offset defaults to zero only when its NVS key is
  absent, is restored during RTC initialization, and is persisted before a new
  local time is committed to the RTC.

## Self-review

- Confirmed every task pin and partition value against the brief.
- Confirmed the required display command/data sequences are present verbatim,
  BUSY high blocks progress, retained old/current RAM is synchronized only after
  successful refresh, and hibernated deinit does not assert RESET.
- Confirmed radios are not started from `app_main` and diagnostics does not call
  deep sleep.
- Confirmed motor off is attempted both during deinit and sleep preparation.
- Confirmed the RTC timer result is propagated, interrupt flags are cleared,
  BMA423 INT2 is excluded from ext1, active sources are rechecked, and any
  failed prerequisite prevents `esp_deep_sleep_start()`.
- Confirmed no Arduino component, API, macro, or source dependency is present in
  project code.
- Confirmed `sdk/include` and `components/watchy_core` have no diff, preserving
  the committed Task 1 ABI/layout.
- Component code is built with `-Wall -Wextra -Werror`; the target build reaches
  link/image generation.
- The portable tests use literal expected masks/percentages and inspect real
  framebuffer bytes, not mocks or mirrored implementation helpers.

## Concerns and follow-up items

- Hardware I2C/SPI transactions, active-high BUSY timing, display waveform and
  retained partial-update behavior, ADC accuracy, wake electrical levels,
  corrupt/blank flash provisioning, and RF stop callbacks still require a
  physical Watchy 2.0 smoke test.
- SensorLib registry 0.4.1 compiles many unused drivers because that published
  version only exposes the new/legacy I2C Kconfig choice. Link-time garbage
  collection keeps the firmware within the factory partition, but compile time
  is higher than necessary.
- BMA423's step API returns `0` both for a legitimate zero count and for an
  internal read failure. The service reports steps as supported only after
  feature initialization succeeds, but SensorLib does not expose per-read status
  for that method.
- The RTC offset is fixed-minute persisted state; daylight-saving/timezone-rule
  policy remains outside this HAL task.
- BLE diagnostics mode is intentionally minimal, non-connectable advertising;
  it does not implement a diagnostic GATT service.

## Review fix round 2

### Scope and files

This round changed only the adjacent motor sleep-pin and RTC readiness paths:

- `components/watchy_hal/include/watchy/haptics.h`
- `components/watchy_hal/include/watchy/power.h`
- `components/watchy_hal/include/watchy/rtc_calendar.h`
- `components/watchy_hal/src/haptics.c`
- `components/watchy_hal/src/power.c`
- `components/watchy_hal/src/power_policy.c`
- `components/watchy_hal/src/rtc.c`
- `components/watchy_hal/src/rtc_calendar.c`
- `tests/host/test_hal.c`
- `.superpowers/sdd/implementation-plan/task-2-report.md`

### RED/GREEN evidence

The portable tests were added before their policy implementations. Command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host \
  --target watchy_hal_tests
```

RED exit code: `2`. Relevant output:

```text
Undefined symbols for architecture arm64:
  "_watchy_power_release_pin_for_sleep"
  "_watchy_rtc_initial_clock_ready"
cc: error: linker command failed with exit code 1
```

The motor regression requires GPIO13 to be retained rather than classified as
a generic reset/released pin, while ordinary display/battery pins remain
releasable. The RTC regression accepts a valid leap-day register image and
rejects both the PCF8563 VL flag and an invalid BCD nibble.

After implementing the policies, this command:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host \
  --target watchy_hal_tests && \
./build-host/tests/host/watchy_hal_tests
```

exited `0` with:

```text
PASS 13 HAL tests
```

The complete host suite was then run:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake -S . -B build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build-host && \
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest \
  --test-dir build-host --output-on-failure
```

Exit code: `0`.

```text
4/4 Test #4: watchy_hal_tests ................. Passed
100% tests passed, 0 tests failed out of 4
```

### Clean ESP-IDF target build

Exact command:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2 -t clean && \
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Both commands exited `0`. Final output:

```text
framework-espidf @ 3.50500.0 (5.5.0)
toolchain-xtensa-esp-elf @ 14.2.0+20241119
RAM:   [==        ]  17.0% (used 55544 bytes from 327680 bytes)
Flash: [====      ]  42.9% (used 786395 bytes from 1835008 bytes)
Successfully created esp32 image.
========================= [SUCCESS] Took 41.88 seconds =========================
```

### Implementation and self-review

- GPIO13 is no longer passed to `gpio_reset_pin()` during sleep preparation.
  The portable release policy is consumed directly by the generic pin-release
  loop, so the regression tests the production classification boundary.
- Sleep preparation first propagates motor-off failure, then configures GPIO13
  output-low, enables its pad hold, and enables ESP32 digital-GPIO deep-sleep
  hold. Any error returned by the GPIO operations rejects deep sleep.
- On wake or cold initialization, the haptics service disables the global
  deep-sleep hold, configures GPIO13 output-low while the per-pad latch still
  protects the level, releases the per-pad hold, and writes low again. This
  follows ESP-IDF's documented no-glitch release ordering.
- RTC now keeps private `initialized` and `ready` states. A successful bus read
  marks the transport initialized, but `ready` remains false until BCD/VL/date
  validation and offset restoration succeed. The initialized-invalid state can
  still be repaired with `watchy_rtc_set_local()`; a successful set marks the
  clock ready. Reads, alarm/timer setup, and power preparation never claim an
  invalid initial clock is ready.
- No SDK/core, runtime, portal, package-tooling, or Arduino code changed.

### Physical uncertainty

The clean build proves the ESP32 hold APIs and sequence compile/link, but no
device was attached. GPIO13 level continuity, absence of a wake-time motor
glitch, deep-sleep current, and VL-to-set-local recovery still require physical
Watchy 2.0 validation.
