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
does a separate non-mutating esptool `run` start the complete application. This
factory operation resets settings,
Wi-Fi credentials, package index/health state, and the seed marker; routine
`platformio run -e watchy_v2 -t upload` remains firmware-only and preserves
NVS/LittleFS.
