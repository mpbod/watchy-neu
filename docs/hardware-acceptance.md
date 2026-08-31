# Watchy 2.0 on-device acceptance checklist

This is a release checklist, not a results report. Checkboxes remain open until
run on physical Watchy 2.0 hardware with the tested firmware revision recorded.

Record board serial/revision, firmware commit, battery age/voltage, ambient
temperature, instruments, test duration, and pass/fail evidence.

No motion-system device run was performed during its firmware integration.
Builds and host simulations are not physical acceptance evidence. The motion
checks, measurements, and firmware/device metadata below remain pending for the
integrated motion-plus-gallery controller run.

## Display and controls

- [ ] Cold full refresh produces correct 200×200 orientation and polarity.
- [ ] Repeated partial refreshes are legible; configured promotion controls
      ghosting; a forced full refresh clears retained artifacts.
- [ ] Up, Down, Menu, and Back report individually and in the intended order.
- [ ] Each button wakes from deep sleep; held buttons do not create a wake loop.
- [ ] Back+Down during reset enters safe mode and third-party ELFs do not run.

## Time, motion, battery, haptics

- [ ] PCF8563 read/set preserves valid date, weekday, and local UTC offset.
- [ ] PCF8563 minute/hour/day/weekday next-match alarm wakes at the intended
      match and alarm flags are cleared/rearmed.
- [ ] BMA423 XYZ/shake data is plausible; configured interrupt wakes the watch.
- [ ] Battery ADC is compared against a calibrated meter across useful range.
- [ ] Vibration pulse duration/strength is bounded and GPIO is low after stop.

## Connectivity and storage

- [ ] AP portal shows unique AP and out-of-band HTTP Basic credentials, serves
      at 192.168.4.1, provisions saved STA credentials, uploads/activates/removes,
      enforces idle/absolute expiry, and leaves Wi-Fi off.
- [ ] Saved STA connects, portal advertises the assigned address, NTP sets RTC,
      failure/timeout leaves Wi-Fi off, and credentials are not logged.
- [ ] BLE start/status/stop works and radio is disabled after the operation.
- [ ] NVS settings survive reset; LittleFS packages/state survive reset without
      formatting; low-space and low-battery installs are rejected.

## Package and recovery

- [ ] Digital watchface and hardware-demo WPKs install without firmware flash.
- [ ] Watchface replacement becomes active only after one successful render.
- [ ] Corrupt/incompatible/oversize WPKs are rejected without installed debris.
- [ ] A crashing/overrunning package is quarantined without a boot loop.
- [ ] Pending watchface failure rolls back; built-in watchface remains usable.
- [ ] Safe mode removes one package; corrupt-index fallback purge recovers UI.
- [ ] Passive built-in diagnostic `READY` rows accurately report initialization
      and bounded reads without being mistaken for active hardware acceptance.
- [ ] Separately exercise display, buttons, RTC, BMA423, ADC, motor, Wi-Fi, BLE,
      NVS, and LittleFS on-device and record actionable pass/fail evidence.

## E-paper motion system — pending live run

Test metadata: board/revision **pending**; firmware commit **pending**; battery
voltage/age **pending**; ambient temperature **pending**; current meter and
display timing method **pending**.

- [ ] From a visible watchface, Menu produces the left-to-right Wipe and ends on
      the complete Menu target.
- [ ] Menu into a child screen produces forward/right Push; Back produces the
      reverse/left Push and ends on the complete parent target.
- [ ] Saving a setting produces the intended confirmation Flash over the changed
      UI, with no stale pixels outside the affected presentation.
- [ ] Back from the watchface completes Split before user-requested deep sleep;
      idle timeout and RTC minute wake use direct Cut.
- [ ] Full preserves eligible requested effects; Reduced converts optional
      multi-write effects to Flash; Off converts them to Cut. The separate
      accelerometer Motion On/Off row continues to control wake only.
- [ ] An injected policy build just below 3550 mV downgrades optional effects to
      Cut without preventing a complete target render.
- [ ] Wipe, Push, Fill, Shutter, Odometer, Grow, Flash, Dither, and Split are
      cancelled with a newly pressed button at every available post-write safe
      boundary; the next render starts from the last completed truthful frame.
- [ ] A forced ghost-budget promotion performs direct Clear as two full physical
      writes even in Reduced, Off, safe mode, and low-battery policy cases; the
      clearing-to-target pair cannot be cancelled between writes.
- [ ] An injected failure at each possible intermediate write invalidates the
      retained source; the next successful display is the complete target with a
      full refresh and the shell's bounded display-error recovery remains usable.
- [ ] Every intermediate frame is readable at native 200×200 resolution and the
      sequence never exceeds its budget: Cut 1, Flash/Dither 2,
      Push/Grow/Odometer/Split 2, Wipe 3, Fill/Shutter 3, direct Clear 2 full.

Live evidence still to record:

| Measurement | Result |
| --- | --- |
| Per-effect completion timing and physical-write count | Pending |
| Longest-sequence minimum/mean/maximum current | Pending |
| Direct-Clear timing/current and post-clear ghosting | Pending |
| Cancellation timing at each safe boundary | Pending |
| Intermediate-failure recovery timing/current | Pending |
| Photographs or captured display evidence | Pending |

## Power regression

- [ ] Measure steady deep-sleep current after every wake source and operation;
      record meter setup, stabilization window, minimum/mean/maximum.
- [ ] Run repeated minute wakes for at least 24 hours; record wake success,
      full/partial refresh count, resets, battery voltage, and charge estimate.
- [ ] Confirm Wi-Fi, BLE, display power, motor, SPI/I²C, and package session are
      inactive before deep sleep.
