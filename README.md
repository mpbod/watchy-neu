# Watchy extensible firmware

Pure ESP-IDF firmware for the **SQFMI Watchy 2.0** (ESP32-PICO-D4, 200×200
SSD1681 e-paper). The kernel owns hardware, power, storage, recovery, networking,
and the built-in UI. One installable Xtensa ELF watchface or app may run at a
time through the exactly pinned Espressif `elf_loader` 1.3.3 component.

> **Native-code trust boundary:** WPK packages contain trusted native Xtensa
> code. Capabilities organize access to kernel APIs; they are not a security
> sandbox. Install packages only from developers you trust.

## Prerequisites

- Watchy 2.0 (other Watchy revisions are not supported)
- ESP-IDF 5.5 with the ESP32 toolchain, or PlatformIO Core with `espressif32`
- Python 3.11+, CMake, Ninja, and a USB serial connection

## Build, flash, and monitor

With PlatformIO:

```sh
platformio run -e watchy_v2
platformio run -e watchy_v2 -t upload
platformio device monitor -b 115200
```

With an exported ESP-IDF 5.5 environment:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The partition table reserves 24 KiB NVS, 4 KiB PHY data, 1.75 MiB factory
firmware, and 2.1875 MiB LittleFS. Firmware OTA is intentionally absent in v1;
flash firmware over serial. Packages update independently through the portal.

## Controls and recovery

- Menu opens/selects, Back returns, and Up/Down navigate.
- Hold **Back + Down during reset** to enter safe mode. Safe mode never loads a
  third-party ELF and offers diagnostics, individual package removal (or a full
  purge if the index is unreadable), and a normal reboot.
- A failing selected watchface is quarantined/rolled back; the built-in
  watchface and shell remain available.

Wi-Fi and BLE stay off unless an explicit operation needs them. The normal
minute wake path reloads the selected package, renders, unloads it, shuts down
peripherals, and returns to deep sleep.

## Package portal

On the watch, open **Menu → Settings → Portal**, then choose saved client Wi-Fi
or the temporary Watchy access point. The screen shows the URL, an out-of-band
32-character session credential, and temporary AP credentials. AP mode uses
`http://192.168.4.1/`; client mode shows its assigned address. Authenticate every
route with HTTP Basic username `watchy` and the displayed session credential as
the password. The credential is never embedded in the served page. The session
ends on Back, after 10 minutes without authenticated activity, or at its absolute
30-minute lifetime, and turns Wi-Fi off.

The portal lists, uploads, activates, and removes packages. AP mode also accepts
and persists initial station Wi-Fi credentials, so an erased watch can be
provisioned without reflashing. Uploads are staged and atomically promoted after
full validation. Installation is rejected for unsafe battery/storage/heap
conditions. A new watchface does not replace the previous one until it completes
a successful render.

Built-in diagnostic `READY` rows are passive initialization/read checks, not
physical acceptance results. Only the still-open on-device checklist below can
establish display, input, radio, storage, motor, wake, and current behavior.

## Develop packages

Start from [`sdk/package-template`](sdk/package-template), or inspect the
[`digital-watchface`](samples/digital-watchface) and
[`hardware-demo`](samples/hardware-demo) samples. Build and verify both samples:

```sh
python3 tools/build_samples.py
```

Build a WPK directly:

```sh
python3 tools/watchy_pkg.py build \
  --manifest manifest.json --elf build/package.so --assets assets --output package.wpk
python3 tools/watchy_pkg.py inspect package.wpk --json
python3 tools/watchy_pkg.py verify package.wpk
```

See [Package SDK](docs/sdk.md), [WPK format](docs/package-format.md), and the
[hardware acceptance checklist](docs/hardware-acceptance.md).

## Tests

```sh
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tests/python -v
```

Hardware acceptance remains a separate on-device activity; host and target
build success is not evidence of display, wake, radio, or current performance.
