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

- The top-level Menu has exactly three rows: **Watchface**, **Apps**, and
  **Settings**. Menu opens/selects, Back cancels or returns, and Up/Down wrap
  through the current list.
- **Menu → Watchface** opens the selector. Hairline is always position zero and
  is labeled `BUILT-IN`. Installed watchfaces show `ACTIVE`, `PENDING`,
  `QUARANTINED`, or their semantic version; quarantined rows are visible but
  cannot be activated. Selecting Hairline clears the package selection and
  returns to the trusted built-in face. Selecting a WPK renders it immediately,
  promotes it only after success, and performs a full refresh. Back leaves the
  current selection unchanged and also forces a complete refresh when returning
  to a package face.
- **Settings → Motion Full/Reduced/Off** controls display transition effects.
  This `motion_fx` preference is independent of **Motion On/Off**, which controls
  accelerometer wake. Full allows eligible requested effects, Reduced replaces
  optional multi-write effects with a two-write Flash, and Off uses a one-write
  Cut. Panel-health Clear remains a mandatory two-write full refresh in every
  mode.
- Hold **Back + Down during reset** to enter safe mode. Safe mode never loads a
  third-party ELF and offers diagnostics, individual package removal (or a full
  purge if the index is unreadable), and a normal reboot.
- A failing selected watchface is quarantined/rolled back; the built-in
  watchface and shell remain available. A failed pending activation preserves
  the previously active WPK; if it cannot render, Hairline is the fallback.

Wi-Fi and BLE stay off unless an explicit operation needs them. The normal
minute wake path reloads the selected package, renders, unloads it, shuts down
peripherals, and returns to deep sleep. Minute and other unattended wakes use
Cut. Safe mode, battery voltage below 3550 mV, or an unknown retained display
source also downgrades optional effects to Cut; the kernel can always promote a
write to full refresh for panel health.

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

The bundled gallery contains eight independent ABI 1.2 WPKs:

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

Weather, calendar, and unavailable Bluetooth data are deliberately honest
placeholders (`--°`, `NO DATA`, `NO EVENT`, `--:--`, and `BT·--`). World and
second-city clocks use declared fixed UTC offsets; v1 does not apply daylight
saving time. Orbit computes moon phase locally and performs no network request.
The faces use ABI 1.2 descriptors but do not request the System capability.
Routine renders return Partial; the kernel retains activation, transition,
ghosting, and full-refresh authority.

The committed one-bit typography is generated from vendored IBM Plex Mono
(SIL Open Font License 1.1) and TeX Gyre Heros (GUST Font License) sources in
[`assets/fonts`](assets/fonts); builds do not consult host-installed fonts.

## Gallery and factory provisioning

Build the audited reproducible first-party set and the reproducible LittleFS
factory image:

```sh
make first-party
make factory-seed
```

On the first normal boot after a factory flash, firmware validates and imports
the eight seed WPKs through the normal atomic installer, records the WFS1 v1
marker, and leaves Hairline active. Import is resumable, but once the marker is
committed it never runs again: removing a bundled face does not resurrect it on
reboot. Safe mode skips seed import and all WPK execution.

Normal developer upload is firmware-only and preserves LittleFS:

```sh
platformio run -e watchy_v2 -t upload
```

Factory flashing first validates the explicit port syntax, confirms the port
appears exactly once in read-only USB serial discovery, and validates the
pinned esptool parser before it builds any package, seed, or firmware artifact. It then builds
and hashes the firmware images, validates the exact
Watchy partition table and target identity, erases only the exact 24 KiB NVS
partition without releasing the ESP32 from its bootloader, then writes firmware
plus the audited LittleFS image without resetting. Only after that write reports
success does a separate no-stub `chip-id` identity read finish with an RTS hard
reset into the application. The handoff does not mutate flash or NVS. The
factory reinstall resets all settings, Wi-Fi credentials, the package
index/health state, and the factory seed marker before first-boot import. It is destructive to package storage and
requires an explicit discovered classic ESP32 serial device—there is no
automatic port selection:

```sh
python3 -m venv build/gallery-python
build/gallery-python/bin/python -m pip install \
  -r tools/font-requirements.txt \
  -r tools/factory-flash-requirements.txt
make factory-flash PORT=/dev/ttyUSB0 \
  PYTHON=build/gallery-python/bin/python \
  IDF_PYTHON=python3
```

The tool requires exactly esptool 5.3.1, validates the imported module and all
used parser forms with complete required arguments before running even the
factory seed or firmware build, and invokes it only as a module of the selected
`PYTHON` (`FACTORY_FLASH_PYTHON` may override it); no ambient `esptool`
executable is used. The parser preflight constructs command contexts only: it
does not invoke an esptool command or open the port. The only external
pre-build command is read-only `platformio device list --json-output`; chip
identity and every mutation remain in the post-build, fail-closed flash stage.
The success-only handoff is exactly `--before no-reset --after hard-reset
--no-stub chip-id`; unlike esptool's `run` command, it does not issue a ROM
SPI-flash attach operation before resetting the Watchy.

`PYTHON` is the provisioned test/factory-tool interpreter. `IDF_PYTHON` is the
ESP-IDF-ready interpreter used for sample, first-party, and factory-seed
builds; it defaults to `python3` and must provide the Python modules required by
the installed ESP-IDF. Keeping these roles separate avoids installing the full
ESP-IDF Python environment into the small tooling virtual environment.

Do not use `factory-flash` for routine firmware development. See the pending
[hardware acceptance checklist](docs/hardware-acceptance.md) before treating a
factory image as device-approved.

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
python3 -m venv build/gallery-python
build/gallery-python/bin/python -m pip install \
  -r tools/font-requirements.txt \
  -r tools/factory-flash-requirements.txt
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
make python-test PYTHON=build/gallery-python/bin/python
make gallery-test PYTHON=build/gallery-python/bin/python IDF_PYTHON=python3
```

The complete software gallery gate is `make gallery-test` with the provisioned
`PYTHON` and `IDF_PYTHON` shown above;
it builds and verifies the two samples, all eight reproducible first-party WPKs,
the reproducible factory seed, and the `watchy_v2` firmware. It never flashes.

Hardware acceptance remains a separate on-device activity; host and target
build success is not evidence of display, wake, radio, or current performance.
