# Watchy 2.0 Extensible Firmware Implementation Plan

Build a pure ESP-IDF firmware for Watchy 2.0 with a trusted hardware and
system kernel plus dynamically loaded native Xtensa ELF watchfaces and apps.

## Milestones

1. Establish the ESP-IDF project, public SDK ABI, deterministic WPK format,
   lifecycle state machine, refresh policy, and portable host tests.
2. Implement Watchy 2.0 display, buttons, RTC, accelerometer, battery,
   vibration, Wi-Fi, BLE, storage, diagnostics, and deep-sleep services.
3. Integrate Espressif's ELF loader, package validation, single-package
   lifecycle, quarantine, package-private storage, and host capabilities.
4. Implement the built-in watchface, launcher, settings, diagnostics,
   safe-mode recovery, and AP/client Wi-Fi package portal.
5. Provide deterministic package tooling, sample watchface and app packages,
   documentation, host tests, target builds, and hardware acceptance guidance.

## Fixed decisions

- Target Watchy 2.0 only, using pure ESP-IDF and ESP32-PICO-D4.
- Installed packages are trusted native code, not sandboxed processes.
- Expose one C-compatible `watchy_package_entry` symbol and a versioned host
  function table; provide a header-only C++ convenience API.
- Unload packages before deep sleep and reload them on wake.
- Use a 1.75 MiB factory partition and 2.1875 MiB LittleFS partition.
- Firmware OTA is outside v1; packages are managed through a Wi-Fi portal.
- Built-ins always provide a watchface, launcher, settings, diagnostics, and
  safe mode even when no third-party package can load.
