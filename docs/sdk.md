# Watchy package SDK (ABI 1.1)

## Compatibility and entry point

Packages target `esp32`: ELF32, little-endian, `ET_DYN`, machine `EM_XTENSA`.
Exactly one defined global function is exported:

```cpp
extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry() noexcept;
```

The returned descriptor and all callback pointers/strings must remain valid for
the loaded session. Metadata identity, version, and ABI must exactly match the
manifest. ABI major must equal the host major; a package minor may not exceed
the host minor. WPK format changes and ABI changes are independent version
axes. Installing `id@new-version` creates a new immutable version; activation
is explicit and prior watchface promotion is transactional.

The binary boundary is the versioned C structs in `sdk.h`. `sdk.hpp` is a
header-only C++17 convenience layer for `Canvas`, `Clock`, `Input`, `Motion`,
`Battery`, `Haptics`, `Storage`, `Network`, `Bluetooth`, and `System`.

## Lifecycle

Each wake uses a fresh session:

1. firmware validates WPK/ELF and calls `dlopen`/`dlsym`;
2. `watchy_package_entry` returns the descriptor;
3. `on_load(host, &state)` negotiates services and package state;
4. `on_start(state)` begins the wake session;
5. tagged events and `on_render(state, canvas, &mode)` run as needed;
6. `on_stop(state)`, then `on_unload(state)`, then `dlclose`;
7. the kernel shuts down peripherals and enters deep sleep.

Only data written through `Storage` persists. Static/global RAM and pointers do
not survive deep sleep. Callbacks return `WATCHY_STATUS_OK`,
`INVALID_ARGUMENT`, `INVALID_STATE`, `INCOMPATIBLE_ABI`, or `UNSUPPORTED`.
Unhandled callback errors, watchdog overruns, invalid pointers/descriptors, and
repeated incomplete attempts can quarantine a package.

Callbacks are synchronous and must not retain kernel canvas/event pointers.
The package task has a cumulative 4000 ms callback budget against the system
watchdog. Avoid blocking; asynchronous Wi-Fi/BLE requests return request IDs
whose status packages must poll through `Network::status` or
`Bluetooth::status`. The v1 firmware does not deliver radio completion events.

## Events

`watchy_event_t` is tagged as tick, button, motion, battery, network,
Bluetooth, or system. Inspect only the union member selected by `type`. Button
events identify Up, Down, Confirm, or Back and press/release state. The host may
request app exit; apps can request graceful exit through `System::request_exit`.
Network and Bluetooth remain reserved event tags in ABI 1.1; the current host
does not emit them, so polling the matching request ID is authoritative.

## Capabilities

The manifest bitmask requests only the services a package uses:

| Bit | Capability | API |
| ---: | --- | --- |
| 0 | Canvas | 1-bit drawing and refresh request |
| 1 | Clock | current local time and RTC alarm |
| 2 | Input | button state |
| 3 | Motion | BMA423 sample |
| 4 | Battery | millivolts/percent/charging |
| 5 | Haptics | bounded motor pulse |
| 6 | Storage | namespaced state and read-only assets |
| 7 | Network | kernel-owned asynchronous Wi-Fi operation |
| 8 | Bluetooth | kernel-owned asynchronous BLE operation |
| 9 | System | time/log/sleep/exit/refresh requests |

A capability can still return `UNSUPPORTED` if unavailable in the current
hardware/session state. Capabilities are API organization, not native-code
isolation. Packages must never call ESP-IDF symbols directly.

## Canvas and refresh

The kernel owns a 200×200 monochrome canvas. `pixels` has `height * stride`
bytes; `(x,y)` uses byte `y*stride+x/8` and mask `0x80>>(x&7)`. A cleared bit is
black and a set bit is white. Honor the supplied dimensions, stride, rotation,
and format instead of assuming them.

`on_render` proposes `WATCHY_REFRESH_PARTIAL` or `FULL`. A package may also
request refresh through Canvas/System. The kernel has final authority and can
promote partial to full for cold start, ghosting limits, or policy. There is no
v1 dirty-rectangle wire API; partial applies to the complete framebuffer.

## Persistence and assets

Storage paths are relative to the package namespace. Mutable state is
quota-limited and atomically handled by the firmware. `assets/<manifest-path>`
is read-only and maps into the installed version. Do not write package assets,
cross package IDs, use absolute/traversal paths, or assume filesystem paths.

## C++ restrictions

Use fixed/static storage and explicit POD ownership. Across the ABI boundary:

- no exceptions, RTTI, STL objects, virtual interfaces, or C++ name mangling;
- no allocator ownership transfer and no function-local thread-safe statics;
- no direct ESP-IDF/libc dependency; the template supplies hidden package-local
  memory primitives for compiler-generated POD initialization and copies;
- no callback throwing, longjmp, retained host pointers after unload, or direct
  framebuffer refresh hardware access.

The template compiles with hidden visibility, `-fno-exceptions`, `-fno-rtti`,
`-fno-builtin`, section garbage collection, a loader-compatible linker script,
and Espressif `project_so`. Its post-build finalizer sets ELF `e_entry` to the
sole exported entry point because `project_so` 1.3.x emits shared objects with
entry zero. The sample build also audits generated ELFs for unresolved symbols
before packaging.

## Build and package

Copy `sdk/package-template`, adjust its CMake include path (or set
`WATCHY_PACKAGE_TOOL`), implement `main/package.cpp`, edit `manifest.json`, and
create an `assets` directory (it may be empty).
With an exported ESP-IDF 5.5 environment:

```sh
idf.py set-target esp32
idf.py build so
python3 /path/to/watchy-fw/tools/watchy_pkg.py build \
  --manifest manifest.json --elf build/watchy_package.so \
  --assets assets --output watchy_package.wpk
python3 /path/to/watchy-fw/tools/watchy_pkg.py verify watchy_package.wpk
```

Limits enforced by both tool and firmware are 80 KiB WPK, 64 KiB ELF, 32 KiB
aggregate assets, 16 KiB canonical manifest, and 80 KiB declared loader
runtime. Choose a smaller honest `max_runtime_bytes`; production ELF validation
rejects a relocated image that exceeds it.
