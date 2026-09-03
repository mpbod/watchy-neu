# Watchy RTOS Input Events Design

## Goal

Make every deliberate button press register exactly once, including presses that begin and end while the e-paper controller, package runtime, or another peripheral is blocking the main task. Preserve the existing deep-sleep, safe-mode, package, and forced-full-refresh behavior.

## Confirmed Root Cause

ESP-IDF already runs the firmware on FreeRTOS, but the application currently polls button levels from the main task every 50 ms. Display presentation is synchronous and may spend seconds transferring framebuffers and waiting for the panel. A press that begins and ends inside that interval is never observed. A Cut transition has one physical write, so its transition cancellation callback has no between-frame opportunity to sample input.

## Chosen Architecture

The main ESP-IDF task remains the only owner of shell state, package lifecycle calls, canvas rendering, transition composition, and physical display presentation. This preserves the current serialization guarantees around the global framebuffer, package mutex, and task-local watchdog scopes.

The buttons HAL gains a small FreeRTOS input producer:

1. GPIO any-edge interrupts notify a dedicated input task. The ISR performs no allocation, logging, blocking work, or rendering.
2. The input task samples all four pins and applies a 30 ms debounce state machine.
3. Only stable rising edges become semantic press events in a fixed-capacity static queue.
4. The main shell and package-app loops consume queued events instead of calculating edges from occasional level samples.
5. Display transition cancellation consumes a queued event only at an existing safe transition boundary and forwards it through the existing cancelled-button handoff. An event arriving during the last or only physical write stays queued for the main loop.

No separate display worker is introduced in this milestone. A safe display worker would require a second transition scratch framebuffer, explicit framebuffer leases, sequence-tagged completion records, package-render synchronization, new watchdog ownership, and power-down joining. That larger change adds RAM and race risk without being necessary to stop lost presses. The e-paper panel's physical refresh time remains hardware-bound; this design makes input reliable while it is busy.

## Button Policy

The host-testable policy stores stable levels, candidate levels, the candidate start timestamp, and the 30 ms debounce interval.

- Initialization uses the actual GPIO levels. A button already held during deep-sleep wake is baseline state and is not synthesized as a second press.
- A candidate level must remain unchanged for the debounce interval before it becomes stable.
- A stable falling edge updates state but emits no public event.
- A stable rising edge emits its semantic button bit once.
- Independent button bits may settle together and are emitted in one mask; existing Menu, Back, Up, Down precedence remains unchanged.
- Timestamp subtraction is unsigned so wraparound remains correct.
- A long hold never repeats. A stable release followed by another stable press emits a new event.

## HAL Interface

Existing `watchy_buttons_init()`, `watchy_buttons_sample()`, `watchy_buttons_ready()`, and `watchy_buttons_deinit()` remain available. The following event API is added:

```c
typedef struct {
    watchy_button_mask_t mask;
    uint32_t timestamp_ms;
} watchy_button_event_t;

bool watchy_buttons_take_press(watchy_button_event_t *out_event,
                               uint32_t timeout_ms);
bool watchy_buttons_press_pending(void);
bool watchy_buttons_overflowed(void);
watchy_status_t watchy_buttons_quiesce(void);
watchy_status_t watchy_buttons_resume(void);
```

The implementation uses static FreeRTOS task and queue storage. The semantic queue holds 16 events. Queue overflow is latched and reported; it is never silently treated as normal operation.

## Runtime Flow

### Boot and wake

Button GPIO is initialized from the current pin levels. The existing wake-cause routing opens the launcher after a button wake. Because the input policy begins from the held level, the wake press is consumed by routing exactly once. Safe-mode chord detection continues to sample Back+Down directly during eligible cold/reset boots.

### Interactive shell

The shell waits for an event with a bounded timeout so portal timeout and idle timeout checks continue to run. Events queued during display presentation, package reconciliation, diagnostics, or motion initialization remain available afterward. Cancelled-presentation events continue through `watchy_shell_presentation_state_t` and are not replayed from both paths.

### Package apps

Interactive ELF apps consume the same semantic queue. Back is still delivered to the package first and remains the kernel fallback exit if the package stays active.

### Display transitions

Transition cancellation checks the event queue rather than current pin levels. Multi-frame optional transitions may stop at the next safe boundary. Mandatory clear sequences remain uncancellable. A single-write refresh completes normally and leaves any captured press queued for the shell.

### Sleep

Before GPIOs are converted to RTC wake sources, the buttons HAL disables GPIO interrupts, stops the producer task, drains/deletes its static queue state, and prevents an in-flight ISR from publishing. Pin levels remain readable for wake-source inactivity checks. If sleep preparation fails before wake ownership transfers, input service must be resumed before the firmware remains awake.

Motion-disabled sleep must not require BMA423 initialization. Motion is configured only when motion wake is enabled.

## Error Handling

- GPIO ISR registration or task/queue startup failure makes button initialization fail.
- Queue overflow is latched. The owner reports an input/system error after draining already captured events rather than silently losing navigation.
- Display failures preserve the existing retained-frame invalidation and forced-full recovery behavior.
- The input task never calls package, display, motion, radio, storage, or shell APIs.
- Power preparation never tears down buses or display while a background input task can still access GPIO state.

## Resource Budgets

- One static semantic queue: 16 small events, under 256 bytes including metadata.
- One statically allocated input task stack, initially 2 KiB and verified by stack high-water measurement on hardware.
- No additional 5,000-byte framebuffer.
- No dynamic allocation after initialization.

## Verification

Host tests cover debounce acceptance/rejection, long holds, release/re-press, simultaneous independent buttons, timestamp wraparound, wake-held suppression, overflow policy, sleep admission, display cancellation handoff, and shell presentation event ownership.

The milestone gate runs all 14 host suites and the ESP32 firmware build once. Hardware UAT then verifies:

1. From a sleeping face, one short Menu tap opens the launcher once.
2. A press fully contained inside an e-paper refresh is delivered once afterward.
3. Down, Up, Menu, and Back each work with Hairline and first-party WPK faces.
4. A long hold does not auto-repeat.
5. Three deliberate queued presses are applied in order without disappearing.
6. Face selection still forces a full refresh and returns directly to the selected face.
7. Safe-mode chord, idle sleep, RTC wake, and optional motion wake still operate.

Success means repeated pressing is never required. Physical e-paper refresh duration is not an acceptance failure when the first press has been captured and subsequently applied exactly once.
