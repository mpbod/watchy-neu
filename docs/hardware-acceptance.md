# Watchy 2.0 on-device acceptance checklist

This is a release checklist, not a results report. Checkboxes remain open until
run on physical Watchy 2.0 hardware with the tested firmware revision recorded.

Record board serial/revision, firmware commit, battery age/voltage, ambient
temperature, instruments, test duration, and pass/fail evidence.

## Display and controls

- [ ] Cold full refresh produces correct 200×200 orientation and polarity.
- [ ] Repeated partial refreshes are legible; configured promotion controls
      ghosting; a forced full refresh clears retained artifacts.
- [ ] Up, Down, Menu, and Back report individually and in the intended order.
- [ ] Each button wakes from deep sleep; held buttons do not create a wake loop.
- [ ] Back+Down during reset enters safe mode and third-party ELFs do not run.

## Time, motion, battery, haptics

- [ ] PCF8563 read/set preserves valid date, weekday, and local UTC offset.
- [ ] Minute alarm wakes repeatedly and alarm flags are cleared/rearmed.
- [ ] BMA423 XYZ/shake data is plausible; configured interrupt wakes the watch.
- [ ] Battery ADC is compared against a calibrated meter across useful range.
- [ ] Vibration pulse duration/strength is bounded and GPIO is low after stop.

## Connectivity and storage

- [ ] AP portal shows unique temporary credentials, serves at 192.168.4.1,
      uploads/activates/removes, times out, and leaves Wi-Fi off.
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
- [ ] Diagnostics exercises display, buttons, RTC, BMA423, ADC, motor, Wi-Fi,
      BLE, NVS, LittleFS, and reports actionable failure state.

## Power regression

- [ ] Measure steady deep-sleep current after every wake source and operation;
      record meter setup, stabilization window, minimum/mean/maximum.
- [ ] Run repeated minute wakes for at least 24 hours; record wake success,
      full/partial refresh count, resets, battery voltage, and charge estimate.
- [ ] Confirm Wi-Fi, BLE, display power, motor, SPI/I²C, and package session are
      inactive before deep sleep.
