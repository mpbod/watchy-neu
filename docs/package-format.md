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
