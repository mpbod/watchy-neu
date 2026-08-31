# Watchy package SDK (ABI 1.2)

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
ABI 1.2 is prefix-compatible with ABI 1.1: existing 1.1 packages continue to
render with Cut presentation, while a package that declares 1.1 may use the
appended transition request only after checking that the host minor is at least
2. It must not read the appended system callback from a 1.1 host.

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
Transition submission may additionally return `BUSY` when the package already
has an unconsumed request.
Unhandled callback errors, watchdog overruns, invalid pointers/descriptors, and
repeated incomplete attempts can quarantine a package.

Callbacks are synchronous and must not retain kernel canvas/event pointers.
The package task has a cumulative 4000 ms callback budget against the system
watchdog. Avoid blocking; asynchronous Wi-Fi/BLE requests remain pending until
the underlying radio reaches a terminal state and fail on the bounded host
deadline. Cancellation rolls back a started operation, and session teardown
stops package-owned radios. Packages poll request IDs through `Network::status`
or `Bluetooth::status`; ABI 1.1 does not deliver radio completion events.

## Events

`watchy_event_t` is tagged as tick, button, motion, battery, network,
Bluetooth, or system. Inspect only the union member selected by `type`. Button
events identify Up, Down, Confirm, or Back and press/release state. The host may
request app exit; apps can request graceful exit through `System::request_exit`.
Network and Bluetooth remain reserved event tags; the current host does not
emit them, so polling the matching request ID is authoritative.

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

`Clock::set_alarm` exposes the PCF8563's available next-match alarm: the RTC
matches the supplied minute, hour, day-of-month, and weekday fields. Year,
month, seconds, and UTC-offset fields must still form a valid `watchy_time_t`
but are otherwise ignored. This is not an absolute timestamp alarm; callers
must choose four fields that describe an intentional future match.

## Canvas and refresh

The kernel owns a 200×200 monochrome canvas. `pixels` has `height * stride`
bytes; `(x,y)` uses byte `y*stride+x/8` and mask `0x80>>(x&7)`. A cleared bit is
black and a set bit is white. Honor the supplied dimensions, stride, rotation,
and format instead of assuming them.

`on_render` proposes `WATCHY_REFRESH_PARTIAL` or `FULL`. A package may also
request refresh through Canvas/System. The kernel has final authority and can
promote partial to full for cold start, ghosting limits, or policy. There is no
v1 dirty-rectangle wire API; partial applies to the complete framebuffer.

## ABI 1.2 transition requests

`watchy_system_api_v1_t::request_transition` is appended in ABI 1.2. It accepts
a `watchy_transition_request_v1_t` during an active event or render callback and
requests presentation of the next accepted complete render. It does not expose
the panel, submit an intermediate framebuffer, choose physical refresh modes,
or override kernel policy. Treat the service as optional: check the negotiated
ABI and callback, and continue rendering when it is absent or returns
`WATCHY_STATUS_UNSUPPORTED`.

Initialize the entire request to zero, set `size = sizeof(request)`, then set an
effect and direction. `WATCHY_TRANSITION_HAS_RECT` declares a signed rectangle;
the compositor clips a partially visible rectangle to the 200×200 canvas.
Zero-area and wholly off-canvas rectangles are invalid, and Odometer always
requires a rectangle. Omitting the flag selects the full canvas for effects that
allow it. `WATCHY_TRANSITION_PREFER_FULL` asks for a full final target but does
not prevent the kernel from promoting other writes. Unknown flags, invalid enum
values, wrong size, or nonzero reserved words return `INVALID_ARGUMENT`.

The host copies one valid request into trusted storage. A second request before
consumption returns `BUSY` without replacing the first. The request is consumed
exactly once when the next render is accepted for presentation, whether it was
submitted by the preceding event or by that render callback. A rejected render,
stop, unload, or teardown discards it. If optional request presentation is
rejected as invalid at the final boundary, the accepted target is retried as
Cut; a real display failure is not masked.

Physical-write budgets count every display transfer after the source already
visible on the panel:

| Effect | Maximum physical writes |
| --- | ---: |
| Cut | 1 |
| Flash, Dither | 2 |
| Push, Grow, Odometer, Split | 2 |
| Wipe | 3 |
| Fill, Shutter | 3 |
| Mandatory direct Clear | 2 full writes |

Every intermediate write advances the refresh/ghosting counters and may be
promoted to full refresh. Between completed optional writes the kernel services
the watchdog and may cancel on a newly pressed button; the last completed frame
then becomes the truthful retained source. Direct Clear cannot be cancelled
between its clearing and target writes.

The display policy applies after validation. Full permits the requested effect
within the limits above. Reduced keeps Cut but replaces any other optional
effect with a two-write Flash. Off, safe mode, unattended wakes, retained-source
loss, and battery voltage below 3550 mV downgrade optional effects to Cut.
Reaching the partial-refresh ghosting limit takes precedence over those modes:
the kernel performs a direct two-full-write Clear and ends on the complete
target. Invalid source state or an intermediate-write failure forces the next
successful target to a complete full refresh.

## First-party gallery contract

The factory gallery is not a privileged runtime. Grid 01–03, Term 01–03, Slab,
and Orbit are ordinary independent ABI 1.2 watchface packages with IDs
`watchy.firstparty.grid01`, `watchy.firstparty.grid02`,
`watchy.firstparty.grid03`, `watchy.firstparty.term01`,
`watchy.firstparty.term02`, `watchy.firstparty.term03`,
`watchy.firstparty.slab`, and `watchy.firstparty.orbit`. They use the same
validation, lifecycle, capability, render-promotion, rollback, watchdog, and
80 KiB WPK ceiling as developer packages.

Weather and calendar services are not part of v1. First-party renderers must
show their documented placeholders (`--°`, `NO DATA`, `NO EVENT`, `--:--`, or
`BT·--`) rather than fixture data or implied connectivity. World and
second-city clocks use explicit fixed UTC offsets and are not DST-aware. Orbit
uses local integer moon-phase calculation and makes no network request.

The shared UI data is generated deterministically from vendored IBM Plex Mono
under SIL OFL 1.1 and TeX Gyre Heros under the GUST Font License. The source
fonts, licenses, generator configuration, and committed one-bit strikes live
under `assets/fonts`, `tools/font_strikes.json`, and `sdk/ui/generated`.

Selector activation first records the requested WPK as pending, runs the normal
fresh-session lifecycle immediately, and promotes it only after a successful
render. Failure retains the prior active reference; the shell falls back to
Hairline when no package can render. Selecting Hairline clears active/pending
package state and persists an empty `active_watchface`. Both activation and a
Back return to a package face invalidate the retained display source so the
next accepted face uses a full refresh.

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
sole exported entry point because the pinned `project_so` 1.3.3 emits shared objects with
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
