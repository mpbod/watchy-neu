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
- `components/watchy_hal/include/watchy/haptics.h`
- `components/watchy_hal/include/watchy/motion.h`
- `components/watchy_hal/include/watchy/power.h`
- `components/watchy_hal/include/watchy/radios.h`
- `components/watchy_hal/include/watchy/rtc.h`
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
- `components/watchy_hal/src/framebuffer.c`
- `components/watchy_hal/src/haptics.c`
- `components/watchy_hal/src/motion.cpp`
- `components/watchy_hal/src/power.c`
- `components/watchy_hal/src/power_policy.c`
- `components/watchy_hal/src/radios.c`
- `components/watchy_hal/src/rtc.c`
- `components/watchy_hal/src/storage.c`

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
- The display owns a 5000-byte current framebuffer and a private 5000-byte
  previous-RAM shadow. It clips framebuffer writes, synchronizes SSD1681 old and
  new RAM, feeds requests through `watchy_refresh_policy_t`, uses the required
  full/partial/power/deep-sleep command values, and times out BUSY waits after
  10 seconds.
- BMA423 optional temperature and step fields carry explicit supported,
  unsupported, or unavailable statuses. Values are never presented without the
  corresponding status.
- Battery ADC uses 16 samples, ESP32 line-fitting calibration, the Watchy 2.0
  2:1 divider, a conservative piecewise LiPo percentage curve, and an exact
  install gate of 3550 mV.
- Wi-Fi and BLE remain stopped at normal boot. Wi-Fi has stored STA and AP
  boundaries; BLE has separate controller/host lifecycle and non-connectable
  diagnostics advertising.
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

## Hardware assumptions

- BMA423 address is `0x18` (SDO low), matching `BMA4_I2C_ADDR_PRIMARY` in the
  sibling InkWatchy Watchy 2.0 checkout.
- SSD1681 BUSY is active low, matching GDEH0154D67 convention; BUSY high means
  ready.
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
- LittleFS is formatted only when mounting fails. This makes a blank factory
  partition usable but means an unrecoverable corrupt filesystem is erased.
- RTC values are local broken-down time. The UTC offset defaults to zero after a
  reset and is updated when local time is set; persistent timezone policy belongs
  to a later settings/runtime task.

## Self-review

- Confirmed every task pin and partition value against the brief.
- Confirmed the required display command/data sequences are present verbatim and
  old/current RAM are synchronized after successful refresh.
- Confirmed radios are not started from `app_main` and diagnostics does not call
  deep sleep.
- Confirmed motor off is attempted both during deinit and sleep preparation.
- Confirmed RTC interrupt flags are cleared before enabling ext0/ext1 sleep.
- Confirmed no Arduino component, API, macro, or source dependency is present in
  project code.
- Confirmed `sdk/include` and `components/watchy_core` have no diff, preserving
  the committed Task 1 ABI/layout.
- Component code is built with `-Wall -Wextra -Werror`; the target build reaches
  link/image generation.
- The portable tests use literal expected masks/percentages and inspect real
  framebuffer bytes, not mocks or mirrored implementation helpers.

## Concerns and follow-up items

- Hardware I2C/SPI transactions, BUSY polarity, display waveform behavior, ADC
  accuracy, wake electrical levels, and RF start/stop still require a physical
  Watchy 2.0 smoke test.
- SensorLib registry 0.4.1 compiles many unused drivers because that published
  version only exposes the new/legacy I2C Kconfig choice. Link-time garbage
  collection keeps the firmware within the factory partition, but compile time
  is higher than necessary.
- BMA423's step API returns `0` both for a legitimate zero count and for an
  internal read failure. The service reports steps as supported only after
  feature initialization succeeds, but SensorLib does not expose per-read status
  for that method.
- RTC UTC offset is not persisted yet, by design; Unix conversion uses the
  offset most recently supplied through `watchy_rtc_set_local()`.
- BLE diagnostics mode is intentionally minimal, non-connectable advertising;
  it does not implement a diagnostic GATT service.
