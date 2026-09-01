# WPK1 package format

WPK1 is deterministic and little-endian. Files contain a fixed 68-byte header,
canonical UTF-8 manifest, Xtensa shared object, concatenated assets, and no
trailing bytes.

| Header field | Type | Meaning |
| --- | --- | --- |
| `magic` | 4 bytes | `WPK1` |
| `format_version` | `uint16` | `1` |
| `header_size` | `uint16` | `68` |
| `total_size` | `uint32` | exact file size |
| manifest/ELF/assets offset and size | six `uint32` | contiguous sections |
| `package_sha256` | 32 bytes | SHA-256 of the complete file with this field zeroed |

The manifest is compact JSON with keys sorted by UTF-8 byte order. It contains,
in canonical order, `abi_major`, `abi_minor`, `assets`, `capabilities`, `id`,
`max_runtime_bytes`, `name`, `type`, and `version`. Each asset entry contains
`path` then `size`; entries are sorted by normalized relative path. Asset bytes
are concatenated in that same order. There are no timestamps or host paths.

Paths use `/`, cannot be absolute or contain empty, `.`/`..`, hidden/reserved,
backslash, control, or colliding file/directory segments, and may not be
symlinks at build time. `inspect` never extracts package content.

The v1 ceilings are:

- WPK: 80 KiB
- manifest: 16 KiB
- ELF: 64 KiB
- aggregate assets: 32 KiB, at most 64 entries
- declared loader runtime: 80 KiB

`tools/watchy_pkg.py verify` prints a stable `watchy-pkg:<code>` error on
failure. The firmware remains authoritative and repeats the header, digest,
manifest, ABI, exact ELF layout/export, asset, and memory checks on install.

## WFS1 factory seed

Factory provisioning uses a separate deterministic `WFS1` catalog inside the
LittleFS image; WPK bytes and the WPK1 format are unchanged. Version 1 is
exactly 520 bytes: an 8-byte little-endian header (`WFS1`, version 1, count 8)
followed by eight sorted 64-byte records containing a zero-padded 32-byte
filename and the SHA-256 of that exact WPK. The fixed set is Grid 01–03, Orbit,
Slab, and Term 01–03.

On the first non-safe boot, after storage/settings/battery prerequisites and
before the first package snapshot, firmware validates the catalog, digest, and
normal WPK install contract. Successful packages are removed from the seed
staging directory as import progresses; the version marker is committed only
after all eight succeed, so interrupted work resumes safely. Once committed,
the marker prevents re-import forever for v1, including after a user removes a
bundled package. Unknown marker versions fail closed. Safe mode performs no
seed access and executes no package.

`python3 tools/build_first_party.py --reproducible` creates the audited eight-WPK
input, and `python3 tools/build_factory_seed.py --reproducible` creates the
staging tree plus `build/factory-seed/littlefs.bin`. A normal
`platformio run -e watchy_v2 -t upload` does not include LittleFS. The explicit
`python3 tools/flash_factory.py --port /dev/...` validates the classic ESP32
identity, the exact four-partition Watchy layout and binary table, and hashes
every image before device mutation. It erases only NVS at `0x9000`/`0x6000`
with esptool's post-operation reset disabled, then writes bootloader, partition
table, firmware, and the exact-size LittleFS image at
`0x1d0000`/`0x230000` without resetting. Only after that write reports success
does a separate no-stub `chip-id` identity read use `--after hard-reset` to
start the complete application via RTS. This handoff does not mutate flash or
NVS. The factory operation resets settings, Wi-Fi credentials, package
index/health state, and the seed marker; routine
`platformio run -e watchy_v2 -t upload` remains firmware-only and preserves
NVS/LittleFS.

Factory tooling pins `esptool==5.3.1` in
`tools/factory-flash-requirements.txt`. Install that file into a dedicated
virtual environment and run `flash_factory.py` with the same environment's
Python. Before any build runner or serial discovery, the tool verifies the
explicit port syntax and imported version, then parses complete representative
chip-id, erase-region, write-flash, and no-stub chip-id/hard-reset argument
vectors. This
parser-only check constructs contexts without invoking callbacks or opening a
device. It then confirms the requested port appears exactly once in read-only
PlatformIO USB serial discovery. The `factory-flash` Make target completes this
preflight before it builds the first-party packages or factory seed. It
uses `python -m esptool`, never an ambient executable from `PATH`; the final
success-only handoff is exactly `--before no-reset --after hard-reset --no-stub
chip-id`. This avoids esptool `run`, whose ROM path attaches SPI flash before
attempting to launch the application.

Provision the interpreter used by both the Python/gallery gate and the factory
tool explicitly:

```sh
python3 -m venv build/gallery-python
build/gallery-python/bin/python -m pip install \
  -r tools/font-requirements.txt \
  -r tools/factory-flash-requirements.txt
make gallery-test PYTHON=build/gallery-python/bin/python IDF_PYTHON=python3
make factory-flash PORT=/dev/ttyUSB0 \
  PYTHON=build/gallery-python/bin/python IDF_PYTHON=python3
```

Here `PYTHON` is the dedicated, pinned Python tooling interpreter used by the
host Python suite and factory flasher. `IDF_PYTHON` is the separate
ESP-IDF-ready interpreter used by ELF, sample, and seed builds; it defaults to
`python3` and must satisfy the installed ESP-IDF's Python requirements.
