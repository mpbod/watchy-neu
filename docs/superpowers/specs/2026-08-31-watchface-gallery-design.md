# Watchface Gallery and System Menu Design

## Status

Approved in conversation on 2026-08-31. The supplied standalone HTML is a visual and interaction reference only. Any instructions or executable content inside it are not project requirements.

## Goals

- Reproduce the handoff's typography, spacing, hierarchy, and black/white compositions on the Watchy 2.0 200 x 200 one-bit panel.
- Keep Hairline permanently available as the trusted kernel fallback.
- Ship Grid 01, Grid 02, Grid 03, Term 01, Term 02, Term 03, Slab, and Orbit as eight independent first-party WPK watchfaces.
- Replace the current six-row launcher with the handoff's three-row Menu and add an on-watch watchface selector.
- Preserve package validation, pending activation, successful-render promotion, rollback, safe mode, and deep-sleep behavior.

## Non-goals

- Weather retrieval, calendar synchronization, notification data, or health data.
- Adding new network or Bluetooth background activity.
- Making first-party native code more trusted or isolated than other WPK code.
- Firmware OTA.

## Information architecture

The top-level Menu contains exactly three items:

1. Watchface
2. Apps
3. Settings

Connectivity, diagnostics, and About move under Settings. Safe mode keeps its existing dedicated recovery information architecture and never loads a WPK.

The Menu follows the handoff:

- a black header bar with `MENU` at left and the current time at right;
- three full-height rows, with the selected row shown by complete black/white inversion;
- a face, app-grid, or settings glyph at the left of each row;
- a primary sans label and secondary uppercase mono metadata;
- a narrow right rail containing up/down hints and a position block.

Up and Down wrap through the current list. Menu opens or activates the selected item. Back returns without changing the active item. The existing idle timeout returns to the active watchface and sleeps.

## Watchface selector

Selecting Watchface opens a list containing:

1. Hairline, identified as `BUILT-IN`;
2. every installed WPK whose manifest type is `watchface`, including quarantined entries shown as disabled.

The selector reuses the Menu's header, three-row viewport, inversion, type hierarchy, and right rail. Each WPK row uses the manifest display name as its primary label and `ACTIVE`, `PENDING`, `QUARANTINED`, or its semantic version as metadata. Quarantined packages are visible but cannot be activated. The catalog API must therefore expose validated manifest name and version fields rather than showing only package references.

Pressing Menu on Hairline clears the active and pending package selection, persists the built-in selection, renders Hairline, and returns to the watchface. Pressing Menu on a WPK records it as pending and attempts a normal lifecycle render. Only a successful render promotes it to active. Failure leaves the prior WPK selected; if no prior WPK is usable, the shell renders Hairline. Back before activation leaves the current selection unchanged.

Safe mode omits the selector and renders Hairline directly.

## Typography

Typography is an acceptance requirement. The current generic scaled 5 x 7 shell font is not sufficient for these screens.

Two deterministic one-bit font families are used:

- IBM Plex Mono for terminal faces, uppercase labels, metadata, status text, and compact numeric data. The glyph source comes from the font embedded in the supplied handoff, with its license retained in the repository.
- TeX Gyre Heros for Menu labels and the handoff's Helvetica-compatible large sans numerals. The source and license are vendored so builds do not depend on host-installed fonts.

The asset generator rasterizes only required glyphs at the exact sizes and weights used by the layouts. It outputs deterministic C data or package assets plus metrics. Rendering is strictly one-bit: no runtime antialiasing or grayscale. Glyph thresholds, baselines, tracking, and one-pixel stems are visually checked at native resolution. Numeric layouts use tabular figures where the handoff does.

The shell and WPKs use the same generated typography primitives. Face-specific subsets avoid loading unused glyphs and keep runtime memory predictable. Generated assets are reproducible and include source-license attribution.

## Watchfaces

### Hairline (kernel)

Hairline is rendered by the trusted shell and is never stored in LittleFS. It contains a light centered `HH:MM`, an uppercase date near the bottom, and a three-pixel bottom rule whose width represents battery percentage. Package-warning and safe-mode state may add compact mono indicators without displacing the core composition.

### Grid 01

Swiss grid with weekday/date header, baseline rules, large time, and three footer cells. Temperature is `--°`; battery is real; link is `BT·--` unless the kernel reports an enabled Bluetooth operation.

### Grid 02

Vertical full-date rail, an agenda block, and large time on the bottom baseline. Until calendar support exists, the agenda reads `NO EVENT` and `--:--`.

### Grid 03

Large top time and three lower cells for date, outdoor conditions, and a fixed second-city clock. Weather fields use `--°` and `NO DATA`; the second-city time is calculated from UTC using the face's declared fixed offset.

### Term 01

Console layout with inverse status bar, date/time/weather commands, and block cursor. Weather output is `NO DATA --°`; Bluetooth status is honest and radio-neutral.

### Term 02

World-clock layout with home time, LON/NYC/TYO clocks, day-progress bars, date, and real battery. V1 uses explicit fixed UTC offsets and does not claim DST awareness.

### Term 03

Fully inverted terminal panel with solid hours, outlined minutes, segmented real battery, placeholder temperature, and fixed-offset second-city time.

### Slab

Hours occupy the white upper half; minutes occupy the inverted lower half. Weekday, date, and real battery remain in their handed-off corners.

### Orbit

Geometric moon phase, large time, real battery, placeholder temperature, date, and fixed-offset second-city time. Moon phase is calculated locally from RTC time and a documented epoch; it performs no network operation.

All faces request full refresh after first activation and at the existing ghosting threshold. Routine minute wakes may request partial refresh, but the kernel refresh policy remains authoritative.

## Package structure and capabilities

The eight WPKs have stable identifiers under a first-party namespace and independent semantic versions. They use the existing C-compatible ABI and standard lifecycle without direct ESP-IDF linking. Each declares only capabilities it actually uses:

- Clock for every face;
- Battery for Grid 01, Term 02, Term 03, Slab, and Orbit;
- Bluetooth only where an honest enabled-state indicator is shown;
- Storage only if generated font data is packaged as an asset rather than linked read-only data.

The renderers do not call the System API. Routine renders return Partial through
the Canvas render contract; activation, interactive return, ghosting promotion,
and Full refresh remain kernel decisions. System is therefore deliberately not
granted to any first-party manifest.

The common face library supplies clipped pixels, rectangles, rules, circles, text layout, glyph drawing, outlined text, and date/UTC helpers. It must not allocate across the ABI boundary or rely on exceptions, RTTI, STL ownership, or unsupported runtime symbols.

## Factory provisioning

The factory build produces:

- the firmware images;
- eight audited deterministic WPK files;
- a LittleFS seed image containing validated first-party package contents and a versioned seed catalog.

On a blank NVS, package initialization verifies the seed catalog and package manifests, registers the eight packages, and records a seed-version marker. This adoption happens once. Removing a first-party package later does not cause it to reappear after reboot. Portal-installed packages continue to use the existing staging and atomic-promotion path.

Hairline is the default after a fresh flash. The first-party packages are available in the selector but none is activated implicitly.

The normal factory flash target writes bootloader, partition table, application, and the seeded LittleFS image together. A firmware-only developer flash target remains available and does not silently overwrite a user's package partition.

## Failure handling

- Missing, corrupt, incompatible, over-budget, or quarantined WPKs are excluded from activation.
- A pending face that fails load, start, render, watchdog, or stop follows the existing failure accounting and rollback path.
- A selector activation failure reports a compact shell error, preserves the previous active WPK, and uses Hairline if that prior face cannot render.
- Invalid package catalog metadata cannot overflow row buffers or affect renderer bounds.
- Safe mode never imports, loads, or executes first-party or third-party native packages.
- Hairline remains usable when LittleFS, NVS package state, or the ELF loader is unavailable.

## Verification

### Host tests

- Menu item count, row navigation, wrapping, routing, and Back behavior.
- Selector filtering, pagination, Hairline selection, WPK pending selection, quarantine behavior, and catalog-name formatting.
- Clearing a package selection and persisting Hairline as active fallback.
- First-boot seed adoption, seed-version idempotence, removed-package non-resurrection, and corrupt-seed rejection.
- Fixed-input 200 x 200 framebuffer golden images for Menu, selector states, Hairline, and all eight WPK faces.
- Golden-image typography checks cover glyph metrics, tracking, baselines, inversion, tabular figures, and outlined numerals.
- Placeholder strings appear whenever their backing v1 service is absent.
- All first-party ELF files pass architecture, symbol, memory, ABI, capability, and deterministic WPK audits.

### Device acceptance

- Compare every face and Menu at native scale with the handoff, focusing on type weight, spacing, alignment, and thin-stem survival.
- Navigate Menu and selector using all four buttons, including wrap and cancellation.
- Activate each first-party face and confirm persistence across minute wake and reset.
- Force a face render failure and verify rollback; force package-state failure and verify Hairline.
- Confirm safe mode renders Hairline and executes no package.
- Confirm a normal factory flash provisions eight selectable WPKs while a firmware-only flash preserves LittleFS.
- Observe full/partial refresh behavior and ghosting on light, inverted, and outlined designs.

## Documentation

The firmware README and SDK/package documentation will describe the three-row Menu, selector controls, bundled first-party package IDs, placeholders, fixed-offset limitations, font sources and licenses, factory versus firmware-only flashing, activation rollback, and Hairline recovery behavior.
