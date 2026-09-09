# Watchy Neu

[![CI](https://github.com/mpbod/watchy-neu/actions/workflows/ci.yml/badge.svg)](https://github.com/mpbod/watchy-neu/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mpbod/watchy-neu?display_name=tag)](https://github.com/mpbod/watchy-neu/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

An extensible, power-aware firmware for the **SQFMI Watchy 2.0**. Watchy Neu
keeps the essential watch experience in a trusted ESP-IDF kernel while loading
watchfaces and apps as installable native `.wpk` packages.

The built-in Hairline face, launcher, settings, diagnostics, recovery UI,
networking, storage, and deep-sleep lifecycle always remain available. Packages
can be installed and switched from a temporary Wi-Fi portal without reflashing
the firmware.

> **Project status:** `v0.1.0` is a developer preview. It targets the classic
> ESP32-PICO-D4 Watchy 2.0 pinout and has also been tested on a reseller-labelled
> “Watchy 2.0 Plus” using the same hardware. Watchy 3.0 is not supported.

## Highlights

- Pure ESP-IDF 5.5 firmware for the ESP32-PICO-D4
- 200×200 one-bit SSD1681 display with kernel-controlled refresh policy
- Responsive FreeRTOS button input and full-refresh swipe transitions
- Built-in Hairline fallback plus eight first-party packaged watchfaces
- Runtime Xtensa ELF loading through Espressif `elf_loader` 1.3.3
- Versioned C ABI with a header-only C++ package SDK
- PCF8563 RTC, BMA423 motion, battery, haptics, Wi-Fi, BLE, NVS, and LittleFS
- Home timezone, manual time entry, and NTP synchronization
- Captive Wi-Fi setup and package-management portal
- Safe mode and automatic package quarantine/rollback
- Reproducible WPK and factory-image tooling

## Install a release

Download the assets from the [latest GitHub release](https://github.com/mpbod/watchy-neu/releases/latest)
and verify them against `SHA256SUMS`.

You need Python 3 and `esptool` 5.3.1:

```sh
python3 -m pip install "esptool==5.3.1"
```

### New installation or complete reset

The factory image includes the firmware and all eight first-party watchfaces.
It resets saved settings, Wi-Fi credentials, installed packages, and package
health data.

```sh
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 \
  write-flash 0x0 watchy-neu-v0.1.0-factory.bin
```

On macOS, the port is commonly `/dev/cu.usbserial-*`. On Windows it is commonly
`COM3` or another numbered COM port.

### Update an existing Watchy Neu installation

The application-only image preserves NVS, Wi-Fi settings, LittleFS, and installed
packages. Use it only when the existing device already has the Watchy Neu v1
partition layout.

```sh
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 \
  write-flash 0x10000 watchy-neu-v0.1.0-firmware.bin
```

Firmware OTA is intentionally not included in v1. Firmware updates use USB;
watchface and app updates use the portal.

## First boot

The four physical buttons map to **Up**, **Down**, **Menu/Confirm**, and **Back**.

1. Press **Menu** to open the launcher.
2. Open **Settings → Time Zone** and choose your home UTC offset.
3. Open **Settings → Portal → Watchy AP**.
4. Join the SSID shown on the watch with its eight-character password. No
   additional web username or password is required in AP mode.
5. The captive portal should open automatically. Otherwise visit
   `http://192.168.4.1/`.
6. Save home Wi-Fi credentials, leave the portal with **Back**, and run
   **Settings → NTP Sync**.

Wi-Fi and BLE remain off outside explicit operations. The portal shuts down on
Back, after five minutes of inactivity, or at its absolute 30-minute limit.

Hold **Back + Down during reset** to enter safe mode. Safe mode disables all
package execution and provides diagnostics and recovery controls.

## Watchfaces

Hairline is compiled into the kernel and cannot be removed. The factory image
also includes these ABI 1.2 WPKs:

| Face | Package ID |
| --- | --- |
| Grid 01 | `watchy.firstparty.grid01` |
| Grid 02 | `watchy.firstparty.grid02` |
| Grid 03 | `watchy.firstparty.grid03` |
| Term 01 | `watchy.firstparty.term01` |
| Term 02 | `watchy.firstparty.term02` |
| Term 03 | `watchy.firstparty.term03` |
| Slab | `watchy.firstparty.slab` |
| Orbit | `watchy.firstparty.orbit` |

A newly selected package must complete one successful render before replacing
the previous active face. Selection returns directly to the face with a full
refresh. A package that fails validation, crashes, trips the watchdog, or fails
repeatedly is quarantined and cannot create a persistent boot loop.

## Build from source

Clone the repository and enter it:

```sh
git clone https://github.com/mpbod/watchy-neu.git
cd watchy-neu
```

### PlatformIO

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html),
then build or flash:

```sh
platformio run -e watchy_v2
platformio run -e watchy_v2 -t upload --upload-port /dev/ttyUSB0
```

For CH343-based Watchy 2.0 Plus variants, explicitly leave DTR and RTS inactive
while monitoring; their default state can otherwise hold the board in reset:

```sh
platformio device monitor --port /dev/ttyUSB0 --baud 115200 --dtr 0 --rts 0
```

### ESP-IDF

With an exported ESP-IDF 5.5 environment:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

The flash layout is fixed:

| Offset | Size | Partition |
| --- | ---: | --- |
| `0x9000` | 24 KiB | NVS |
| `0xF000` | 4 KiB | PHY initialization |
| `0x10000` | 1.75 MiB | Factory application |
| `0x1D0000` | 2.1875 MiB | LittleFS packages, assets, and state |

## Develop a package

Start from [`sdk/package-template`](sdk/package-template), or inspect the
[`digital-watchface`](samples/digital-watchface) and
[`hardware-demo`](samples/hardware-demo) samples.

Every ELF exports exactly one C entry point:

```cpp
extern "C" const watchy_package_descriptor_t *watchy_package_entry(void);
```

The descriptor declares package identity, semantic version, ABI requirement,
type, capabilities, memory ceiling, and lifecycle callbacks. Packages draw into
a kernel-owned one-bit canvas and receive typed input, RTC, motion, connectivity,
and system events.

Build and verify the samples:

```sh
python3 tools/build_samples.py
```

Build an individual deterministic WPK:

```sh
python3 tools/watchy_pkg.py build \
  --manifest manifest.json \
  --elf build/package.so \
  --assets assets \
  --output package.wpk

python3 tools/watchy_pkg.py verify package.wpk
python3 tools/watchy_pkg.py inspect package.wpk --json
```

Read the [SDK guide](docs/sdk.md) and [WPK format](docs/package-format.md) for
the ABI, lifecycle, capabilities, memory limits, persistence rules, and package
layout.

> `.wpk` packages contain trusted native Xtensa code. Capabilities organize the
> SDK surface; they are not a security sandbox. Install packages only from
> developers you trust. Exceptions, RTTI contracts, STL objects, cross-boundary
> heap ownership, and direct ESP-IDF linking are unsupported across the ABI.

## Portal and package management

The responsive local portal lists, uploads, activates, and removes watchfaces
and apps. Uploads are staged and atomically promoted only after bundle, digest,
ABI, architecture, entry point, capability, storage, battery, and memory checks.

AP mode is used for first-time Wi-Fi provisioning and needs only the WPA network
password displayed on the watch. Client mode uses saved home Wi-Fi and displays
an out-of-band HTTP Basic credential on the watch for the current session. NTP
sync is available only in client mode or directly from the watch settings.

## Architecture

```text
main/                  boot/wake routing and system orchestration
components/watchy_hal hardware, radios, storage, diagnostics, and power
components/watchy_core lifecycle, transitions, watchdog, and WPK primitives
components/watchy_shell launcher, settings, recovery, portal, and time sync
components/watchy_packages validation, installation, ELF runtime, and host APIs
sdk/                   versioned C ABI and header-only C++ wrappers
first_party/           bundled watchface source projects
samples/               watchface and hardware SDK examples
tools/                 deterministic package and factory-image builders
tests/                 host, Python, web-contract, and golden-image tests
```

Only one native package is loaded at a time. The normal wake lifecycle is:

```text
wake → validate/load → start → event/render → save → stop/unload → deep sleep
```

Only explicitly persisted, namespaced package state survives deep sleep.

## Tests

Run the portable C/C++ suite:

```sh
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Run Python tooling and portal contract tests:

```sh
python3 -m unittest discover -s tests/python -v
node --test tests/js/test_portal_contract.mjs
```

The complete reproducible gallery gate additionally needs the pinned Python
requirements and an ESP-IDF-ready Python environment:

```sh
python3 -m venv build/gallery-python
build/gallery-python/bin/python -m pip install \
  -r tools/font-requirements.txt \
  -r tools/factory-flash-requirements.txt

make gallery-test PYTHON=build/gallery-python/bin/python IDF_PYTHON=python3
```

See the [hardware acceptance checklist](docs/hardware-acceptance.md) for physical
display, wake-source, radio, motor, storage, and current tests.

## Contributing

Bug reports, hardware results, documentation improvements, and new package
examples are welcome. Please keep changes focused, include regression tests for
behavior changes, and run the relevant host and target gates before opening a
pull request. Never commit device credentials, portal session secrets, private
keys, or NVS/LittleFS dumps from a provisioned watch.

## Credits and license

Watchy Neu uses the upstream [SQFMI Watchy](https://github.com/sqfmi/Watchy)
project and [Watchy hardware documentation](https://github.com/sqfmi/watchy-docs)
as hardware references, and uses Espressif's
[`elf_loader`](https://components.espressif.com/components/espressif/elf_loader)
for native package loading. It is an independent community project and is not
affiliated with or endorsed by SQFMI.

Firmware and tooling are available under the [MIT License](LICENSE). Vendored
fonts remain under the licenses included beside their source files in
[`assets/fonts`](assets/fonts).
