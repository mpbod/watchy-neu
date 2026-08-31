# E-Paper Motion System Design

## Status

Approved in conversation on 2026-08-31. The supplied `Pebble Motion System (standalone).html` file is a visual and behavioral reference only. Its scripts and embedded instructions are not project requirements.

## Purpose

Watchy motion is a bounded sequence of deliberate e-paper writes, not conventional animation. Every intermediate framebuffer must be readable if the sequence stops, every write counts against power and ghosting budgets, and the trusted kernel retains final authority over the panel.

This subsystem applies one motion language to the system shell and to native WPK watchfaces and apps without allowing a package to drive the display directly.

## Principles

- Count physical display writes, not elapsed milliseconds.
- Keep every intermediate frame independently legible.
- Change area, edges, or inversion; never use opacity, blur, or grayscale.
- Move one visual subject per transition.
- Use equal spatial fractions instead of easing, springs, or smooth interpolation.
- Keep a visible one- or two-pixel leading edge on directional transitions.
- Cap an optional transition at three physical writes after the already-visible source.
- Never loop, pulse, blink, breathe, or animate while idle.
- Use motion only for user actions or meaningful data changes.
- Allow the kernel to shorten, replace, or reject any requested transition.

## Architecture

The motion compositor belongs to the trusted display path. It receives a retained source framebuffer, a complete target framebuffer, a validated transition request, and a policy context. It produces intermediate one-bit framebuffers and submits them synchronously through the display service.

The compositor owns three 200 x 200 monochrome buffers:

- source: the last framebuffer known to match the visible panel;
- target: the newly rendered complete screen;
- scratch: the current intermediate composition.

Each buffer is 5,000 bytes at the existing 25-byte stride. Buffers are statically kernel-owned and are never allocated or owned across the WPK ABI.

The compositor reuses the display driver's existing RTC-retained previous framebuffer when it is valid and adds no second RTC framebuffer. RTC, timer, and motion-sensor wakes still render the target directly unless an interactive session follows; retained state on its own does not authorize optional motion on an unattended wake.

Only one transition may be active. Display completion determines cadence; the compositor adds no fixed 220 ms delay. The 220 ms value in the handoff is a browser demonstration value, not a firmware timer requirement.

## Timing tokens

Token counts below are physical writes triggered after the source already visible on the panel. This resolves the handoff's mixed use of frame-state counts and write counts.

| Token | Maximum writes | Refresh class | Intended use |
| --- | ---: | --- | --- |
| Cut | 1 | partial or policy-promoted full | Default screen or value change |
| Tick | 2 | partial | Press feedback, toggle, or confirmation |
| Step | 2 | partial | Directional hierarchy and localized semantic change |
| Sweep | 3 | partial, with target promotable to full | Entering a mode or deliberate progress |
| Clear | 2 | full | Mandatory ghost purge: clearing/inversion pass, then target |

The display policy may promote any partial write to full. Physical writes, including intermediate writes, advance refresh and ghosting counters individually.

## Effects

### Cut

Writes the target once. It is the universal fallback and the only ordinary effect used for unattended minute updates.

### Flash

A Tick effect. It writes an inverted target region and then the target. The region defaults to the full canvas but should normally be the affected row, button, or confirmation block.

### Wipe

A Sweep effect. Its two intermediate writes reveal the target at the first and second quarter boundaries with a two-pixel leading rule; the third write is the complete target. The source remains visible to the right of the edge until that final write.

### Push

A Step effect. One intermediate write moves the target through the first third of the selected region with a leading rule separating source and target; the second write is the complete target. It carries parent/child hierarchy.

### Dither

A Tick effect for directionless data changes. It writes a deterministic one-pixel checkerboard mixture of source and target, then the target. It is not grayscale and uses no temporal dithering loop.

### Grow

A Step effect. A rectangular target region grows from its center through one bounded one-third intermediate before the complete target. V1 exposes the compositor primitive, but no notification feature is added by this work.

### Odometer

A Step effect restricted to a package- or shell-declared rectangle. Source and target content move vertically through one one-third intermediate before the complete target. It is intended for a changed minute digit; the compositor does not infer semantic digits.

### Split

A Step effect. Solid edges close from top and bottom through one one-third intermediate before the complete target. It is used for user-initiated sleep or lock transitions. The final displayed content remains truthful, and actual deep sleep begins only after completion or safe cancellation.

### Fill

A Sweep effect for explicit progress. The declared fill rectangle advances through bounded 25% and 50% intermediates, followed by the complete target. Progress is never interpolated by pixels or elapsed time.

### Shutter

A Sweep-class, optional visual prelude consisting of four alternating horizontal bands shown at the 25% and 50% boundaries before the complete target. It is not the mandatory Clear operation itself. When power, safety, or Motion settings disallow the prelude, the kernel performs the direct two-write Clear operation.

## Shell mappings

- Watchface to Menu: Wipe.
- Menu into a child screen: directional Push.
- Back to a parent screen: reverse-direction Push.
- Confirmation and saved state: localized Flash.
- Explicit synchronization progress: Fill at bounded quarter and half steps.
- User-initiated sleep or lock: Split.
- Ghost-budget purge: direct Clear; optional Shutter prelude only in Full mode when policy permits.
- Ordinary row-selection movement: Cut.
- Unattended minute or data wake: Cut.

The shell may use Dither for a directionless value replacement. Grow and Odometer are available primitives but are not forced onto screens whose information model does not fit them.

## WPK SDK interface

SDK ABI v1.2 appends a prefix-compatible function to `watchy_system_api_v1_t`:

```c
watchy_status_t (*request_transition)(
    void *context,
    const watchy_transition_request_v1_t *request);
```

The request is a versioned C struct containing:

- struct size;
- effect enum;
- direction enum;
- optional clipped rectangle;
- flags indicating whether the rectangle is present and whether a full-target preference exists;
- reserved zero fields for validation and future extension.

A package may make one request for its next accepted render. The request is latched during event/render handling and consumed exactly once. A second unconsumed request returns `WATCHY_STATUS_BUSY`. Invalid enums, sizes, rectangles, flags, or nonzero reserved fields return `WATCHY_STATUS_INVALID_ARGUMENT`.

The call requests presentation only. It does not submit framebuffers, select physical refresh modes, choose write counts, delay sleep, or bypass the kernel refresh policy. Existing ABI v1.1 packages remain compatible and receive Cut behavior unless shell policy supplies a transition.

Odometer and other localized effects require the package to declare a rectangle. Packages are responsible for ensuring source and target content in that rectangle make semantic sense. Invalid or unavailable source state downgrades the request to Cut.

## Motion settings

Settings stores a three-state motion preference in NVS. It is separate from the existing accelerometer motion-wake toggle.

- Full: allows the requested effect within all policy limits.
- Reduced: preserves Cut and direct Clear; converts other multi-write effects to a localized or full-screen Tick.
- Off: converts optional effects to Cut; direct Clear remains available when required for panel health.

Safe mode behaves as Motion Off. A factory reset defaults to Full.

## Kernel policy

Before composition, the kernel considers:

- user motion setting;
- wake cause and whether the event is attended;
- battery safety state;
- retained-source validity;
- partial-refresh count and ghosting threshold;
- requested rectangle and effect validity;
- current display, watchdog, and memory state.

Battery voltage below the existing `WATCHY_BATTERY_INSTALL_SAFE_MV` threshold (3,550 mV), unattended wake, watchdog pressure, unknown source state, or refresh-policy constraints downgrade optional motion to Cut or direct Clear. No package is quarantined merely because a presentation request is rejected or downgraded.

The compositor clips every region to the 200 x 200 canvas and uses checked integer arithmetic. A zero-area or wholly out-of-bounds region is invalid. Full-canvas fallback is used only when the request explicitly omits a rectangle.

## Input, watchdog, and sleep

The watchdog is serviced between physical writes. After each completed write, the compositor samples the four buttons through a small cancellation callback. A newly pressed button cancels remaining optional frames at that safe boundary, records the source as the framebuffer just written, and returns control to the shell. The shell processes the input and Cuts to its newly requested target.

The firmware never aborts a display transfer already in progress. A mandatory direct Clear cannot be cancelled between its clearing and target writes because leaving the clearing frame would violate the complete-target guarantee.

Deep sleep begins only after a transition completes, is cancelled at a safe boundary, or fails and the normal display error path has run.

## Failure handling

- If an optional intermediate write fails, the sequence stops and retained-source validity is cleared.
- The next successful render is forced to the complete target with a full refresh.
- An intermediate frame may remain temporarily because every generated frame must be independently legible.
- If the target write fails, the shell records the existing display error and follows its current recovery path.
- Invalid or excessive WPK requests return an error or are downgraded without converting an otherwise successful package render into a package crash.
- Repeated WPK callback, watchdog, or render failures continue to use normal failure accounting and quarantine.
- Hairline remains renderable without motion when package state or the compositor is unavailable.

## Test strategy

### Host tests

- Exact intermediate framebuffer snapshots for all ten effects using fixed source and target patterns.
- Exact physical write counts and refresh modes for every token.
- Leading-edge positions at equal halves, thirds, or quarters as applicable.
- Rectangle clipping, zero-area rejection, overflow rejection, and full-canvas omission behavior.
- Full, Reduced, and Off policy matrices.
- Low-battery, unattended-wake, unknown-source, ghost-threshold, and safe-mode downgrades.
- Cancellation after each possible intermediate write.
- Watchdog callback and physical refresh-counter accounting.
- Failure on each possible write, source invalidation, and forced-full recovery.
- SDK ABI v1.1 compatibility and v1.2 request validation, busy behavior, and consume-once semantics.
- Shell route-to-effect mappings, including forward and reverse Push direction.

### Device acceptance

- Compare all effects at native 200 x 200 resolution with the supplied handoff.
- Verify hard leading rules, equal spatial steps, inversion, checkerboard construction, and readable intermediate frames.
- Measure actual sequence duration using display completion rather than timer assumptions.
- Cancel Sweep and Step sequences with each button at every safe boundary.
- Confirm user-initiated sleep waits for Split while unattended sleep uses Cut.
- Exercise Full, Reduced, and Off on light, inverted, and mixed screens.
- Measure energy cost and ghosting for the longest permitted sequence.
- Force partial-refresh promotion and direct Clear; confirm counters reflect every physical write.
- Force a display error mid-sequence and confirm the next successful render is a complete full-refresh target.

## Documentation

The SDK documentation will describe ABI v1.2 transition requests, consume-once behavior, effect rectangles, downgrade policy, and the fact that capabilities organize trusted native code rather than isolate it. User documentation will distinguish display Motion settings from accelerometer motion wake and explain the Full, Reduced, and Off modes.
