# Task 3 report: ELF loader, package manager, and host capabilities

## Files

- `components/watchy_packages/CMakeLists.txt` and `idf_component.yml`: focused ESP-IDF
  component and `espressif/elf_loader >=1.3.0,<1.4.0` dependency.
- `components/watchy_packages/include/watchy/packages.h`: portable validation,
  index, transaction, loader-boundary, and session contracts.
- `components/watchy_packages/include/watchy/package_host.h`: committed-v1 host
  capability adapter context.
- `components/watchy_packages/include/watchy/package_runtime.h`: target runtime,
  install, and watchface-selection entry points.
- `components/watchy_packages/src/package_format.c`: package-ID/path validation,
  UTF-8/canonical JSON manifest parser, digest normalization, and constant-time
  digest comparison.
- `components/watchy_packages/src/elf_validate.c`: bounded little-endian ELF32
  `ET_DYN`/`EM_XTENSA` program and section validation.
- `components/watchy_packages/src/package_validate.c`: composed WPK, digest,
  ABI, ELF, and asset-byte validation.
- `components/watchy_packages/src/package_state.c`: transactional persisted
  installed/active/pending/prior/quarantine state.
- `components/watchy_packages/src/package_install.c`: stage, validate, unpack,
  read-only files, sync, atomic rename, NVS-last install transaction.
- `components/watchy_packages/src/package_runtime.c`: one-handle lifecycle,
  descriptor checking, callback watchdog hooks, and failure cleanup.
- `components/watchy_packages/src/host_caps.c`: capability-gated HAL tables,
  private state/asset paths, and 16 KiB state quota.
- `components/watchy_packages/src/idf_runtime.c`: mbedTLS, NVS, LittleFS/POSIX,
  task-watchdog, and real `dlopen`/`dlsym`/`dlclose` adapters plus boot flow.
- `tests/host/test_packages.c` and `tests/host/CMakeLists.txt`: 14 portable
  package/runtime/transaction tests.
- `main/app_hooks.[ch]`, `main/main.c`, and `main/CMakeLists.txt`: safe-mode
  bypass and package-watchface boot/render fallback integration.
- `sdkconfig.defaults`: dynamic shared-object loading under `/data`, with
  firmware libc/ESP-IDF/customer import symbol tables disabled.

The committed public SDK headers were not changed.

## TDD red/green record

Each portable behavior began with a failing host test:

1. Package-ID grammar: missing parser header (red), bounded lowercase ASCII
   implementation (green).
2. WPK digest normalization: missing crypto/digest API (red), three-region
   complete-WPK hash with the digest field replaced by 32 zero bytes and a
   constant-time comparison (green).
3. Canonical manifest/schema: missing parser API (red), then a real valid
   manifest exposed a path-terminator bug (`ERR_PATH`, red); root cause fixed
   and canonical/schema/UTF-8/capability/collision/limit tests passed (green).
4. Xtensa ELF32 validation: missing ELF API (red), bounded table/load/section
   validator (green).
5. Composed WPK validation: missing composed API (red), digest + manifest +
   ABI + ELF + asset accounting with trailing-data rejection (green).
6. Selection/quarantine state: missing state API (red), copy-then-persist
   transactions, promotion/rollback, boot attempts, and three-strike
   quarantine (green).
7. Loader lifecycle: missing session API (red), cleanup after load/start/event/
   render failures, safe/quarantine bypass, and one-handle enforcement (green).
8. Install transaction: missing filesystem/install API (red), staging,
   read-only unpack, atomic rename, NVS-last commit, and rollback (green).
9. Version traversal: `../1` was accepted (behavioral red); version strings are
   now single safe path segments (green).
10. Corrupt NVS index: excessive installed count was accepted (behavioral red);
    magic/version/count/NUL/reference/duplicate validation now fails closed
    (green).

Fresh host verification after implementation:

- Normal: 5/5 CTest executables passed, 0 failures. The suite contains 44
  assertions-as-tests at executable level: 15 core, 1 SDK C, 1 SDK C++, 13
  HAL, and 14 package tests.
- UBSan: 5/5 CTest executables passed, 0 failures.
- AddressSanitizer: AppleClang-built test binaries hang during sanitizer
  startup in this runner even for the unchanged core executable. No ASan result
  is claimed.

## Package-format decisions

- The existing packed WPK1 header/layout remains unchanged.
- Digest: SHA-256 over every WPK byte, substituting 32 zero bytes for
  `wpk_header_t.package_sha256`; comparison accumulates all 32 XOR bytes.
- Manifest: closed canonical UTF-8 JSON schema with root keys exactly in this
  lexical order: `abi_major`, `abi_minor`, `assets`, `capabilities`, `id`,
  `max_runtime_bytes`, `name`, `type`, `version`. Whitespace, duplicate/unknown
  keys, non-minimal integer spellings, trailing data, and noncanonical escapes
  are rejected.
- Assets are canonical objects `{"path":"...","size":N}`. Their bytes are
  concatenated in manifest array order in the WPK asset region. Exact and
  file/directory-prefix collisions are rejected.
- Limits: 16 KiB manifest, 64 assets, 512 KiB aggregate assets, 384 KiB ELF,
  and 160 KiB declared/actual ELF load memory.
- Capability bits map in committed host-table order from bit 0 through bit 9:
  canvas, clock, input, motion, battery, haptics, storage, network, Bluetooth,
  and system.
- Installed files live at `/data/packages/<id>/<version>`; the shared object is
  `package.so`, assets retain manifest paths, and all unpacked files use mode
  `0400`. State is under `/data/state/<id>` and SDK paths are explicitly
  `assets/<relative>` (read-only) or `state/<relative>` (read/write, 16 KiB).
- Index records use magic `WPKI`, version 1, bounded fixed records, and an NVS
  blob committed only after filesystem rename.

## Target build

Command: `/Users/maxb/.platformio/penv/bin/pio run -t clean` followed by
`/Users/maxb/.platformio/penv/bin/pio run`.

- Result: success from a clean target build.
- Target/framework: ESP32, ESP-IDF 5.5.0.
- Resolved loader: `espressif/elf_loader 1.3.3` (allowed range is 1.3.x).
- Configuration evidence: `CONFIG_ELF_LOADER=1`,
  `CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1`, base path `/data`.
- Link evidence: firmware ELF contains `dlopen`, `dlsym`, `dlclose`, and
  `watchy_packages_run_watchface`; the loader's `src/dlso/dlfcn.c` and
  `src/dlso/dlmod.c` were compiled.
- Size: 66,344 bytes RAM (20.2%) and 802,231 bytes flash (43.7%).

## Self-review

- Verified no public SDK ABI diff.
- Rechecked overflow/bounds behavior for WPK regions, UTF-8, JSON integers,
  ELF tables/segments/sections, asset sums, fixed persisted records, and all
  constructed target paths.
- Added safe version-segment validation discovered during traversal review.
- Added fail-closed NVS record validation before any persisted string use.
- Fixed short-circuit file-close/fsync paths in target storage adapters.
- Moved the approximately 7 KiB runtime manifest workspace out of the small
  ESP app-task stack.
- Confirmed safe mode and quarantine return before the loader boundary.
- Confirmed every callback failure clears the handle and runtime, normal stop
  transitions through the committed runtime state machine, and only the
  literal `watchy_package_entry` symbol is resolved.
- Confirmed package imports do not receive the loader's libc, ESP-IDF, or
  customer firmware symbol tables; host functionality is supplied through the
  committed table passed to `on_load`.

## Concerns and explicit limitations

1. The committed v1 ABI exposes only network `connected()` and Bluetooth
   `enabled()` queries; it has no asynchronous request operations. Its system
   table has logging/time/sleep but no exit request, and refresh is available
   only through the canvas table. Those brief items cannot be added without a
   public ABI change, which this task explicitly forbids. All currently
   committed tables are implemented and permission-gated.
2. `elf_loader 1.3.3` accepts the `RTLD_NOW` API argument, and this runtime
   always passes it, but the dependency's current `dlopen` implementation
   explicitly ignores the mode internally. This is an upstream implementation
   limitation, not replaced with a placeholder.
3. Verification compiled and linked the real ESP32 loader but did not execute
   a produced Xtensa package on physical Watchy hardware; no sample/package
   builder belongs to Task 3.
4. The blob install entry point receives a complete WPK in memory before it is
   copied to `/data/staging`. A future portal may stream into staging, but no
   HTTP route/tooling was added in this task.
5. Every file is fsynced and closed before rename. ESP-IDF LittleFS does not
   offer a portable directory-fsync contract through this adapter, so directory
   durability relies on LittleFS metadata behavior plus atomic rename and the
   NVS-last transaction.
