# Task 3 report: package runtime review fix round 3

## Result

The package manager now uses the real `espressif/elf_loader` 1.3.x shared-object
path, a persistent app/watchface runner, ABI 1.1 asynchronous host operations,
transaction-owned staging, explicit index wire encoding, and bounded target
storage/runtime adapters. Round 2 applies the no-PSRAM Watchy 2.0 memory
ceilings, exact loader symbol-memory accounting, exact transaction namespaces,
per-runner-task watchdog enrollment, callback deadlines, and host-tested
portable target policies. Round 3 makes the ELF package export contract exact:
one and only one defined global function named `watchy_package_entry`, with
the complete target TLSF footprint of elf_loader's persistent symbol table and
separate names charged to the runtime ceiling. The public ABI major remains 1;
minor 0 prefixes are unchanged and ABI minor is 1 as ruled in review.

## Files

- `sdk/include/watchy/sdk.h`, `sdk/include/watchy/sdk.hpp`: ABI 1.1 network/BLE
  request/cancel/status operations and system exit/refresh operations appended
  after every ABI 1.0 prefix, plus C++ wrappers.
- `components/watchy_packages/include/watchy/packages.h`: 80 KiB absolute WPK,
  64 KiB ELF, 32 KiB asset, and 80 KiB relocated-runtime limits; exact
  transaction naming/reconciliation and runner-task watchdog policy contracts;
  caller-owned install/index workspaces, typed installed records, fixed wire
  codec API, target range callbacks, and copied session descriptor storage.
- `components/watchy_packages/include/watchy/package_host.h` and
  `src/package_policy.c`: portable reconciliation, async pumping, exit/refresh
  cleanup, callback-budget, watchdog enrollment, state quota/type, target
  range, and framebuffer binding policies.
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
  bus-address-mirror section model consumed by elf_loader 1.3.3, including one
  dynamic symbol/string pair, the literal single-entry export contract, and
  target TLSF accounting for the persistent loader function table and every
  separately allocated name.
- `components/watchy_packages/src/package_state.c`: explicit fixed-width NVS
  wire encoding and validated installed/type/selection/health invariants.
- `components/watchy_packages/src/package_install.c`: absolute preflight limit,
  duplicate zero-mutation rejection, exclusive staged readback, exact hidden
  transaction directories with collision retry, no-replace install, and
  created-path ownership tracking.
- `components/watchy_packages/src/package_runtime.c`: process-global handle
  ownership, poison-on-close-failure behavior, bounded descriptor/metadata
  copies, callback range checks, canvas descriptor binding, and convergent
  cleanup.
- `components/watchy_packages/src/host_caps.c`: complete capability-gated HAL
  adapter using the portable async/range/canvas/state policies, iterative state
  traversal, retrying exclusive state temps, quota-atomic writes, and a
  cumulative 4-second callback sleep/feed deadline.
- `components/watchy_packages/src/idf_runtime.c`: NVS wire persistence,
  LittleFS transaction adapter/exact reconciliation, pre-load ELF revalidation,
  exact loader-allocation range binding, persistent event runner, current-task
  watchdog verification at every runner entry, and retained install/select
  paths.
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
4. Unsupported Xtensa relocation type 6 was accepted. It now fails closed;
   undefined and local functions remain structurally checked but do not count
   as defined package exports. Loader-consumed undefined globals are included
   in persistent allocation accounting.
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
13. The previous generous limits accepted an 81,921-byte runtime declaration,
    a 32,769-byte asset, and WPK/ELF files beyond the hardware-safe limits.
    Green enforces WPK 80 KiB, ELF 64 KiB, aggregate assets 32 KiB, runtime
    80 KiB, and manifest 16 KiB before allocation and at parse/validate/load
    boundaries. Exact-limit and one-byte-over tests cover the boundaries.
14. ELF runtime accounting returned only relocated section bytes (`4` for the
    minimal fixture) and duplicate dynamic symbol/string tables were accepted.
    Green requires exactly one `.dynsym` linked to exactly one `.dynstr` and
    rejects duplicate hostile fixtures.
15. Transaction/reconciliation tests showed that substring matching treated
    valid `1.new.0` as temporary, and the installer did not retry a colliding
    transaction directory. Green uses only `.watchy-txn-xxxxxxxx`, keeps an
    indexed `1.new.0`, removes exact stale transactions and unindexed finals,
    and retries exclusive transaction-name collisions without taking ownership
    of the pre-existing path.
16. Nonzero bytes after the first NUL in a fixed-width NVS reference decoded
    successfully. Green requires every padding byte to be zero and applies the
    same reserved-version validation used by manifests/reconciliation.
17. Portable policy tests initially failed to link for reconciliation, async
    pumping, exit/refresh cleanup, watchdog enrollment/deadlines, state
    quota/no-follow classification/temp reservation, pointer ranges, and canvas
    binding. Green implements those seams and uses them from the target
    adapters. Repeated 2-second sleeps exhaust the one 4-second callback budget;
    further sleeps and watchdog feeds are rejected.
18. Target review showed watchdog enrollment occurred only at global runtime
    initialization. Green verifies/adds and then re-verifies the current task
    at start/event/render/stop entry, resets the deadline for every callback,
    and fails package startup closed if enrollment cannot be proven.
19. Export validation accepted a missing export, the wrong export name,
    duplicate exports, and thousands of defined global-function exports. The
    new assertion-level fixtures failed against that behavior. Green permits
    exactly one defined global `STT_FUNC`, requires its name to be the literal
    `watchy_package_entry`, and rejects zero, two, or 3,000 defined exports.
20. Symbol accounting charged raw requests but omitted TLSF block overhead and
    its 12-byte minimum payload. The minimal fixture now reports exactly 48
    bytes: 4 text + a 16-byte TLSF table block + a 28-byte TLSF name block, and
    fails a 47-byte declaration. A structurally valid fixture with one defined
    entry plus 2,999 undefined loader-consumed functions reports 72,020 bytes
    and fails a 72,019-byte declaration.

Final host evidence:

- Normal AppleClang build: CTest 5/5 passed (core 15, SDK C, SDK C++, HAL 13,
  package 31); the separate real-Xtensa fixture smoke passed with
  `runtime_bytes=304`.
- Fresh round-3 UBSan build (`-fsanitize=undefined -fno-omit-frame-pointer`):
  CTest 5/5 passed and the fixture smoke passed with `runtime_bytes=304`; no
  UBSan diagnostics.
- ASan remains unclaimed: AppleClang ASan startup hangs in this runner even for
  the unchanged core executable. UBSan is the clean sanitizer result requested
  by the ruling.

## Format and policy decisions

- WPK1 bytes and digest definition remain unchanged. Hardware truth sets the
  v1 ceilings to 80 KiB total WPK, 64 KiB ELF, 32 KiB aggregate assets, 80 KiB
  relocated/dynamic-symbol runtime, and 16 KiB manifest. Every individual and
  aggregate limit is checked before staging/allocation and again at the
  relevant parser/validator/load boundary.
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
  Xtensa relocation types, dynamic/hash relationships, and all arithmetic. It
  also permits one `.dynsym`/`.dynstr` pair only and exactly one defined global
  function export named `watchy_package_entry`. Undefined/local functions are
  not package exports. For every global function elf_loader consumes, runtime
  accounting models ESP-IDF's 32-bit TLSF allocation as a 4-byte-aligned
  payload of at least 12 bytes plus its 4-byte used-block header. This is
  applied once to the 8-byte-per-entry `esp_symtab_t` table and separately to
  every copied symbol name, with checked arithmetic.
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
- Package version directories and transaction directories are disjoint by
  grammar. Versions may legitimately contain `.new`; installer-owned temporary
  directories are exactly `.watchy-txn-` plus eight lowercase hex digits.
  All `.watchy-*` path segments, including `.watchy-state-*`, are package
  reserved. Reconciliation keeps only indexed valid finals and removes exact
  stale transactions, invalid names, and unindexed finals.
- Radio requests are recorded during package callbacks and executed by the host
  pump afterward. Status/cancel are permission-gated. System exit causes clean
  runner shutdown; requested refresh is performed after the callback, and a
  refresh failure still stops/unloads/closes.
- Package callbacks have one cumulative 4000 ms budget against the 5-second
  task watchdog. Host sleep reserves from that budget, slices accepted sleeps
  into 100 ms delays, and feeds only while the deadline is still valid;
  repeated calls after exhaustion are no-ops and cannot feed indefinitely.
  Every runner entry verifies enrollment for its current task, and startup
  aborts before package code if enrollment cannot be proven.
- Peak transient memory is bounded by phase. Installation is rejected while a
  live or poisoned loader handle exists and needs at most the caller's 80 KiB
  WPK plus one 80 KiB immutable-stage readback (160 KiB, excluding small static
  workspaces). Pre-load validation needs at most 64 KiB and frees it before
  `dlopen`; loader peak is then at most 64 KiB input plus 80 KiB relocated and
  persistent dynamic-symbol/name storage (144 KiB). This leaves operational
  headroom on the 320 KiB no-PSRAM Watchy 2.0 instead of relying on the brief's
  obsolete generous limits.

## Target build and integration evidence

Commands:

```text
pio run -e watchy_v2 -t clean
pio run -e watchy_v2
ninja -C .pio/build/watchy_v2 esp-idf/watchy_packages/watchy_package_xtensa_smoke
build-host/tests/host/watchy_elf_fixture_validator \
  .pio/build/watchy_v2/esp-idf/watchy_packages/watchy_package_smoke.so
```

- Clean firmware build: success in 45.39 seconds on ESP32 / ESP-IDF 5.5.0.
- Resolved package: `espressif/elf_loader` 1.3.3, dynamic shared-object support
  enabled, bus-address-mirror mode enabled, filesystem base `/data`.
- Final size: 97,768 / 327,680 RAM (29.8%) and 1,254,811 / 1,835,008 flash
  (68.3%).
- Final firmware symbols include `dlopen`, `dlsym`, `dlclose`,
  `watchy_package_install`, `watchy_package_select_watchface`,
  `watchy_package_host_pump`, `watchy_package_watchdog_ensure_current`, target
  install/select wrappers, link smoke, and runner start/event/render/stop.
- The fixture is an actual ELF32 little-endian `ET_DYN`, `EM_XTENSA` shared
  object with entry `0x1004` and ten `R_XTENSA_RELATIVE` relocations. It is
  compiled and linked with the installed Xtensa 14.2.0 target compiler and
  exports only `watchy_package_entry` and passes the production validator
  (`runtime_bytes=304`, including the target TLSF table/name allocations).
- Stack guard: every package source is compiled with
  `-Wframe-larger-than=2048`. No warning occurred. `.su` reports show the
  largest frame is `watchy_package_install` at 1600 bytes; runner start is 1008,
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
  under the single package mutex, transaction `O_EXCL` collisions retry without
  deleting the colliding path, and boot reconciliation removes staging, exact
  `.watchy-txn-xxxxxxxx` directories, invalid names, and unindexed finals while
  preserving an indexed valid `1.new.0`.
- Confirmed manifests, validated package objects, index scratch, NVS wire,
  install workspace, state traversal, and removal traversal are caller-owned,
  heap, or serialized static storage rather than multi-kilobyte automatic
  objects.
- Confirmed selection accepts only installed watchfaces; applications cannot be
  selected as watchfaces. Active/pending/prior references, quarantine flags,
  attempt counts, installed membership/types, booleans, and reserved bytes all
  fail closed on decode.
- Confirmed fixed-width reference bytes after the first NUL must all be zero,
  and manifest, NVS, reconciliation, asset, and state path validation share the
  reserved internal namespace decisions.
- Confirmed all `watchy_time_t` fields are checked through
  `watchy_calendar_valid`, canvas identity/capacity comes from the acquired HAL
  framebuffer, package pointers are range checked, and descriptor strings are
  copied to bounded host-owned storage before use.
- Confirmed safe mode/quarantine bypass `dlopen`, at most one global handle can
  exist, close failure poisons and retains ownership, and load/start/event/
  render/refresh/exit paths attempt stop/unload/close consistently.
- Confirmed all current-task runner entries verify TWDT enrollment, each
  callback starts a fresh deadline, and a package cannot extend watchdog feeds
  with repeated sleeps after the 4-second cumulative budget.
- Confirmed no install allocation occurs while an active or poisoned package
  handle remains resident, and the installed ELF is size/runtime revalidated
  from storage immediately before the real loader is called.
- Confirmed install/select/event roots are referenced from target code and are
  present in the final firmware ELF after garbage collection.
- Confirmed the target-generated fixture has exactly one dynamic global
  function, literally `watchy_package_entry`; local callbacks remain local.
  Rechecked the target TLSF allocator's 4-byte alignment, 12-byte minimum
  payload, and 4-byte block overhead against the pinned ESP-IDF source.

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
4. The blob install API still requires the bounded 80 KiB caller WPK plus one
   bounded 80 KiB immutable staged readback. Loader residency is excluded from
   that phase. A future portal can add a staged-file entry to avoid retaining
   the upload copy, but must validate/unpack only bytes read back from the
   immutable stage.
5. LittleFS exposes no portable directory-fsync operation. Every file is
   fsynced and closed before the atomic directory rename; metadata durability
   then relies on LittleFS rename semantics plus the NVS-last commit and boot
   reconciliation.
6. No package was executed on physical Watchy hardware in this task, so no
   device execution or power-cycle durability result is claimed.
