# Task 3 report: package runtime review fix round 1

## Result

The package manager now uses the real `espressif/elf_loader` 1.3.x shared-object
path, a persistent app/watchface runner, ABI 1.1 asynchronous host operations,
transaction-owned staging, explicit index wire encoding, and bounded target
storage/runtime adapters. The public ABI major remains 1; minor 0 prefixes are
unchanged and ABI minor is now 1 as ruled in review.

## Files

- `sdk/include/watchy/sdk.h`, `sdk/include/watchy/sdk.hpp`: ABI 1.1 network/BLE
  request/cancel/status operations and system exit/refresh operations appended
  after every ABI 1.0 prefix, plus C++ wrappers.
- `components/watchy_packages/include/watchy/packages.h`: absolute WPK limit,
  caller-owned install/index workspaces, typed installed records, fixed wire
  codec API, target range callbacks, and copied session descriptor storage.
- `components/watchy_packages/include/watchy/package_host.h`: state traversal,
  async operation, framebuffer binding, mutex, and 4 second callback-budget
  state.
- `components/watchy_packages/include/watchy/package_runtime.h`: persistent
  runner start/event/render/stop API and link-retention smoke entry point.
- `components/watchy_packages/include/watchy/package_crypto.h` and
  `src/sha256.c`: small region-based SHA-256 implementation used by target and
  checked against a known vector on the host.
- `components/watchy_packages/src/package_format.c` and
  `src/package_validate.c`: direct-to-caller parsing/validation without nested
  multi-kilobyte automatic objects, reserved asset namespace, and the narrow
  canonical escape profile.
- `components/watchy_packages/src/elf_validate.c`: validation of the exact
  bus-address-mirror section model consumed by elf_loader 1.3.3.
- `components/watchy_packages/src/package_state.c`: explicit fixed-width NVS
  wire encoding and validated installed/type/selection/health invariants.
- `components/watchy_packages/src/package_install.c`: absolute preflight limit,
  duplicate zero-mutation rejection, exclusive staged readback, no-replace
  install, and created-path ownership tracking.
- `components/watchy_packages/src/package_runtime.c`: process-global handle
  ownership, poison-on-close-failure behavior, bounded descriptor/metadata
  copies, callback range checks, canvas descriptor binding, and convergent
  cleanup.
- `components/watchy_packages/src/host_caps.c`: complete capability-gated HAL
  adapter, asynchronous radios, exit/refresh requests, range validation,
  iterative state traversal, quota-atomic writes, and watchdog-safe sleep.
- `components/watchy_packages/src/idf_runtime.c`: NVS wire persistence,
  LittleFS transaction adapter/reconciliation, exact loader-allocation range
  binding, persistent event runner, target watchdog enrollment, and retained
  install/select paths.
- `components/watchy_packages/CMakeLists.txt`: real Xtensa ET_DYN fixture target,
  `-Wframe-larger-than=2048`, and `.su` stack reports.
- `tests/fixtures/package_smoke.c`, `tests/fixtures/package_smoke.ld`, and
  `tests/host/validate_elf_fixture.c`: target-toolchain shared-object fixture and
  host validation smoke.
- `tests/host/test_packages.c`, `test_sdk_c.c`, `test_sdk_cpp.cpp`, and
  `CMakeLists.txt`: regression suite, ABI-prefix compile checks, hostile ELF and
  transaction fixtures, and working CTest discovery.
- `main/app_hooks.c`: target reference which keeps install/select/event runner
  paths linked and reachable.
- `sdkconfig.defaults`: 6144-byte main task stack and optional Wi-Fi IRAM
  optimizations disabled to fit the real Wi-Fi/BLE/ELF runtime on ESP32.

## Red/green record

Portable behavior was exercised test-first. The material red cases were:

1. ABI 1.1 tests could not compile before request/cancel/status and
   exit/refresh members existed. Green includes C and C++ static assertions for
   the network, Bluetooth, and system ABI 1.0 prefixes.
2. Reserved package/internal asset names and non-profile JSON escapes were
   accepted. Green rejects `package.so`, `manifest.json`, `state`, `.new`,
   hidden/internal and `watchy-` roots, and escapes other than `\"` and `\\`.
3. Hostile section-name/table/link/info/entry/overflow/overlap mutations passed
   the earlier ELF check. Green validates all loader-consumed tables and exact
   named allocations.
4. Unsupported Xtensa relocation type 6 and an undefined global function in
   `.dynsym` were accepted. Both hostile fixtures failed before the final
   loader-consumed checks and now fail closed; the real Xtensa fixture remains
   green.
5. Raw index structure persistence accepted inconsistent flags, selections,
   types, and reserved bytes. Green uses a 2978-byte little-endian wire format
   and rejects every tested corruption.
6. A second session object could race the live handle, reinitialization could
   overwrite a live session, and close failure released ownership. Green uses
   one global owner; a failed close remains occupied/poisoned. A fresh session
   filled with nonzero bytes is safely initialized without reading caller
   storage first.
7. A render callback could replace the bound framebuffer pointer. The new test
   failed against that behavior; green restores the host-owned descriptor,
   aborts the callback path, and unloads the package.
8. Duplicate/unindexed-final install cases could delete a pre-existing final
   directory. Green rejects indexed duplicates before filesystem mutation and
   never removes a destination not created by the transaction.
9. An `O_EXCL` staging collision was incorrectly cleaned up as transaction
   owned. The regression failed with the pre-existing stage removed; green
   retries unique names and leaves every colliding stage untouched.
10. Caller-buffer validation could differ from staged bytes. Green mutates the
    fake stage after the exclusive write and proves digest/parse/unpack all use
    the readback.
11. The absolute WPK limit formerly permitted staging work first. Green returns
    `ERR_LIMIT` with zero filesystem calls.
12. SHA-256 target fallback was added behind the known `abc` vector before it
    was used for package digests.

Final host evidence:

- Normal AppleClang build: CTest 5/5 passed (core 15, SDK C, SDK C++, HAL 13,
  package 21); the separate real-Xtensa fixture smoke passed with
  `runtime_bytes=260`.
- Fresh UBSan build (`-fsanitize=undefined -fno-omit-frame-pointer`): CTest 5/5
  passed and the fixture smoke passed; no UBSan diagnostics.
- ASan remains unclaimed: AppleClang ASan startup hangs in this runner even for
  the unchanged core executable. UBSan is the clean sanitizer result requested
  by the ruling.

## Format and policy decisions

- WPK1 bytes and digest definition remain unchanged. The absolute upper bound
  is header + 16 KiB manifest + 384 KiB ELF + 512 KiB assets, checked before
  staging or allocation.
- ABI version is 1.1. New function pointers are appended to the ABI 1.0 network,
  Bluetooth, and system table prefixes. The host advertises its outer table
  `size`; packages continue to negotiate major/minor with the existing
  size/version rules.
- Canonical strings deliberately use a narrow profile: UTF-8 may appear
  directly, while only quote and reverse-solidus escapes are accepted. Control,
  slash, `\b`, `\f`, `\n`, `\r`, `\t`, and `\u` spellings are rejected.
- The ELF validator models elf_loader's bus mirror precisely: `.text` is
  4-byte allocation-aligned; `.data`, `.rodata`, `.data.rel.ro`, and `.bss`
  form the data allocation. It validates the section-name table, NUL-terminated
  names, section/segment containment, entry execution, allocation and file
  overlaps, NOBITS, symbols/strings, RELA links/info/entry sizes and supported
  Xtensa relocation types, dynamic/hash relationships, and all arithmetic.
- The target adapter additionally obtains the active `dlmod`/`esp_elf_t`
  allocation boundaries. A readable/executable ESP address is accepted only
  when the complete range also lies in the current package's relocated text or
  data allocation.
- Index persistence is an explicit 2978-byte fixed-width little-endian record;
  no C layout, enum width, `size_t`, padding, or raw boolean is persisted.
- LittleFS has no useful chmod semantics. Package ELF/manifest/assets are
  therefore semantically read-only: packages receive only the `assets/` read
  route, writes resolve exclusively below their private `state/` root, and no
  package-visible capability exposes the installer or package filesystem.
- Every state write holds the state mutex across tree sizing, quota decision,
  exclusive hidden-temp write/fsync, and rename. Traversal is iterative and
  bounded; the LittleFS backend cannot represent links, and type checks fail
  closed if that guarantee changes. Package removal is also iterative and does
  not follow links.
- Radio requests are recorded during package callbacks and executed by the host
  pump afterward. Status/cancel are permission-gated. System exit causes clean
  runner shutdown; requested refresh is performed after the callback, and a
  refresh failure still stops/unloads/closes.
- Package callbacks have a documented maximum 4000 ms budget against the
  5-second task watchdog. Host sleep rejects longer requests and slices accepted
  sleeps into 100 ms delays with watchdog feeds. Failure to enroll the startup
  task aborts package runtime initialization.

## Target build and integration evidence

Commands:

```text
pio run -e watchy_v2 -t clean
pio run -e watchy_v2
ninja -C .pio/build/watchy_v2 esp-idf/watchy_packages/watchy_package_xtensa_smoke
build/host-final/watchy_elf_fixture_validator \
  .pio/build/watchy_v2/esp-idf/watchy_packages/watchy_package_smoke.so
```

- Clean firmware build: success in 43.55 seconds on ESP32 / ESP-IDF 5.5.0.
- Resolved package: `espressif/elf_loader` 1.3.3, dynamic shared-object support
  enabled, bus-address-mirror mode enabled, filesystem base `/data`.
- Final size: 97,736 / 327,680 RAM (29.8%) and 1,252,275 / 1,835,008 flash
  (68.2%).
- Final firmware symbols include `dlopen`, `dlsym`, `dlclose`,
  `watchy_package_install`, `watchy_package_select_watchface`, target
  install/select wrappers, link smoke, and runner start/event/render/stop.
- The fixture is an actual ELF32 little-endian `ET_DYN`, `EM_XTENSA` shared
  object with entry `0x1004` and ten `R_XTENSA_RELATIVE` relocations. It is
  compiled and linked with the installed Xtensa 14.2.0 target compiler and
  passes the production validator (`runtime_bytes=260`).
- Stack guard: every package source is compiled with
  `-Wframe-larger-than=2048`. No warning occurred. `.su` reports show the
  largest frame is `watchy_package_install` at 1600 bytes; runner start is 736,
  state storage write 608, ELF validation 128, manifest parsing 96, and session
  load 64. The ESP main task stack is explicitly 6144 bytes, and recursive
  package tree walks were removed.
- Optional Wi-Fi IRAM and RX-IRAM optimization are disabled. Retaining the real
  asynchronous Wi-Fi and BLE operations otherwise overflowed ESP32 IRAM by
  under 1 KiB; this trades radio speed optimization for a linkable complete
  runtime without removing functionality.

No physical Watchy execution is claimed.

## Self-review

- Rechecked all review findings against the final diff and the concrete
  elf_loader 1.3.3 source used by this build.
- Confirmed there is no `fchmod`/`chmod` dependency and no package write route
  for package ELF/assets.
- Confirmed duplicate rejection occurs before filesystem mutation, every
  cleanup path is conditional on transaction ownership, rename is no-replace
  under the single package mutex, and boot reconciliation removes staging,
  `.new`, and unindexed final directories.
- Confirmed manifests, validated package objects, index scratch, NVS wire,
  install workspace, state traversal, and removal traversal are caller-owned,
  heap, or serialized static storage rather than multi-kilobyte automatic
  objects.
- Confirmed selection accepts only installed watchfaces; applications cannot be
  selected as watchfaces. Active/pending/prior references, quarantine flags,
  attempt counts, installed membership/types, booleans, and reserved bytes all
  fail closed on decode.
- Confirmed all `watchy_time_t` fields are checked through
  `watchy_calendar_valid`, canvas identity/capacity comes from the acquired HAL
  framebuffer, package pointers are range checked, and descriptor strings are
  copied to bounded host-owned storage before use.
- Confirmed safe mode/quarantine bypass `dlopen`, at most one global handle can
  exist, close failure poisons and retains ownership, and load/start/event/
  render/refresh/exit paths attempt stop/unload/close consistently.
- Confirmed install/select/event roots are referenced from target code and are
  present in the final firmware ELF after garbage collection.

## Concerns and limitations

1. `elf_loader` 1.3.3's `dlopen` accepts `RTLD_NOW` but currently ignores the
   mode value internally. This is an upstream behavior; the real loader is used
   and no placeholder substitutes for it.
2. PlatformIO's SCons bridge does not execute arbitrary CMake `ALL` custom
   targets. The fixture remains a dependency in the native CMake graph and is
   explicitly built with the generated Ninja target in the evidence command
   above. The firmware itself is built by the normal clean PlatformIO target.
3. The fixture linker emits one RWX PT_LOAD warning. The production validator
   checks each named section's required segment permissions, and elf_loader
   relocates text into executable memory and data into a separate writable
   allocation; the input segment is never executed in place.
4. A complete in-memory WPK and same-sized staged-readback allocation are
   temporarily required during installation. Oversize and allocation failure
   are bounded/fail-closed; a future portal may stream directly to staging.
5. LittleFS exposes no portable directory-fsync operation. Every file is
   fsynced and closed before the atomic directory rename; metadata durability
   then relies on LittleFS rename semantics plus the NVS-last commit and boot
   reconciliation.
6. No package was executed on physical Watchy hardware in this task, so no
   device execution or power-cycle durability result is claimed.
