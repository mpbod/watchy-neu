# Watchy RTOS Input Events Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture every deliberate Watchy button press exactly once even while synchronous e-paper, package, or peripheral work blocks the main task.

**Architecture:** GPIO any-edge interrupts wake a small statically allocated FreeRTOS producer task. That task debounces pin levels and publishes stable rising-edge events to a fixed static queue; the existing ESP-IDF main task stays the sole owner of shell state, package callbacks, framebuffer rendering, and display presentation.

**Tech Stack:** ESP-IDF 5.5, FreeRTOS static tasks/queues, ESP32 GPIO ISR service, C11 host policy tests, PlatformIO ESP32-PICO-D4 build.

**Spec:** `docs/superpowers/specs/2026-09-04-rtos-input-events-design.md`

## Global Constraints

- Watchy 2.0 / ESP32-PICO-D4 only.
- The main task remains the only owner of shell, package lifecycle, canvas, transition, and display operations.
- The ISR only notifies the producer task; it performs no allocation, logging, debounce, rendering, or package work.
- Use static task and queue storage; the semantic queue capacity is 16 events and no additional framebuffer is allowed.
- Debounce interval is 30 ms; stable rising edges emit once, stable falling edges emit nothing, and long holds do not repeat.
- A button held through deep-sleep wake is baseline state and must not produce a duplicate Menu action.
- Existing safe-mode chord behavior, forced full refresh after face selection, and package Back handling remain unchanged.
- Motion-disabled timer sleep must not initialize or require the BMA423.
- Run focused red/green tests per implementation task; run the full host/firmware suite only at the final milestone.

---

### Task 1: Host-Tested Button Debounce Policy

**Files:**
- Modify: `components/watchy_hal/include/watchy/buttons.h`
- Modify: `components/watchy_hal/src/buttons_policy.c`
- Modify: `tests/host/test_hal.c`

**Interfaces:**
- Consumes: existing semantic masks `WATCHY_BUTTON_MASK_MENU`, `BACK`, `DOWN`, and `UP`.
- Produces:

```c
#define WATCHY_BUTTON_COUNT 4u
#define WATCHY_BUTTON_DEBOUNCE_MS 30u

typedef struct {
    watchy_button_mask_t stable_mask;
    watchy_button_mask_t candidate_mask;
    watchy_button_mask_t pending_mask;
    uint32_t candidate_since_ms[WATCHY_BUTTON_COUNT];
    uint32_t debounce_ms;
} watchy_button_filter_t;

void watchy_buttons_filter_init(watchy_button_filter_t *filter,
                                 watchy_button_mask_t initial_mask,
                                 uint32_t debounce_ms);

watchy_button_mask_t watchy_buttons_filter_observe(
    watchy_button_filter_t *filter,
    watchy_button_mask_t sampled_mask,
    uint32_t now_ms);
```

- [ ] **Step 1: Write failing behavior tests**

Add literal, table-independent assertions to `test_hal.c` for these observable behaviors:

```c
/* Initial held Menu is baseline and emits nothing. */
watchy_buttons_filter_init(&filter, WATCHY_BUTTON_MASK_MENU, 30u);
CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 100u) == 0u);

/* A new level does not emit until it remains stable for 30 ms. */
watchy_buttons_filter_init(&filter, 0u, 30u);
CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 100u) == 0u);
CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 129u) == 0u);
CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 130u) ==
      WATCHY_BUTTON_MASK_DOWN);

/* Bounce resets the candidate interval; release emits nothing; re-press emits once. */
```

Also cover independent buttons settling at different timestamps, simultaneous stable presses, a long hold, and unsigned timestamp wraparound.

- [ ] **Step 2: Verify the focused test fails for the missing API**

Run:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host --target watchy_hal_tests -j8
```

Expected: compile or link failure naming `watchy_buttons_filter_init` or `watchy_buttons_filter_observe`.

- [ ] **Step 3: Implement the minimal per-button filter**

In `buttons_policy.c`, iterate over a fixed literal array of the four semantic bits. For each bit:

1. When sampled state differs from candidate state, update that candidate bit, store `now_ms`, and mark the bit pending.
2. When a pending candidate has remained unchanged for `debounce_ms` using unsigned subtraction, commit it to `stable_mask` and clear its pending bit.
3. Return only bits whose committed transition was inactive-to-active.
4. Treat a null filter or zero debounce interval as no emitted event without modifying memory.

- [ ] **Step 4: Verify the focused test passes**

Run the same `watchy_hal_tests` build and executable:

```sh
./build/host/tests/host/watchy_hal_tests
```

Expected: `hal policy tests passed` and exit 0.

- [ ] **Step 5: Commit**

```sh
git add components/watchy_hal/include/watchy/buttons.h \
        components/watchy_hal/src/buttons_policy.c tests/host/test_hal.c
git commit -m "feat: add deterministic button debounce policy"
```

---

### Task 2: Interrupt-Driven FreeRTOS Button Service

**Files:**
- Modify: `components/watchy_hal/include/watchy/buttons.h`
- Modify: `components/watchy_hal/src/buttons.c`

**Interfaces:**
- Consumes: `watchy_button_filter_t`, `watchy_buttons_filter_init(...)`, and `watchy_buttons_filter_observe(...)` from Task 1.
- Produces:

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

- [ ] **Step 1: Add a failing compile contract before implementation**

Declare the public interfaces in `buttons.h`. In `buttons.c`, make `watchy_buttons_init()` return the result of a forward-declared internal `start_event_service()` after GPIO configuration, but do not define that internal function yet. This is the real initialization seam the completed producer must supply; do not add a test-only production hook.

- [ ] **Step 2: Verify the firmware build fails for missing event service symbols**

Run:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: link failure naming `start_event_service`.

- [ ] **Step 3: Implement the static producer service**

In `buttons.c`:

- Configure all four button GPIOs as inputs with `GPIO_INTR_ANYEDGE`.
- Allocate a 16-element semantic event queue with `xQueueCreateStatic`.
- Allocate the producer task control block and a 2 KiB stack statically; never place framebuffer-sized data on this stack.
- Install one IRAM-safe handler per GPIO. Each handler performs only `vTaskNotifyGiveFromISR` and the required ISR yield.
- Initialize the filter from `watchy_buttons_sample()` before enabling handlers.
- In the producer task, sample immediately after a notification and every 5 ms while any candidate bit remains pending. Feed samples and `esp_timer_get_time()/1000` into the Task 1 filter.
- Enqueue each nonzero stable rising-edge mask with its timestamp. If the queue is full, latch overflow without blocking.
- `watchy_buttons_take_press` validates its output pointer and converts `timeout_ms` to FreeRTOS ticks, with zero remaining nonblocking.
- `watchy_buttons_quiesce` disables/removes handlers, stops and joins the producer, drains the queue, and prevents an in-flight ISR from publishing.
- `watchy_buttons_resume` restores the filter baseline from current levels and reinstalls the service without synthesizing a press.
- `watchy_buttons_deinit` quiesces and marks the HAL unready.
- Treat `ESP_ERR_INVALID_STATE` from `gpio_install_isr_service` as an already-installed shared service; remove only this component's handlers and do not uninstall the global ISR service.

- [ ] **Step 4: Verify focused policy and ESP32 compilation**

Run:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host --target watchy_hal_tests -j8
./build/host/tests/host/watchy_hal_tests
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: focused host test passes and firmware links without warnings or stack-frame errors.

- [ ] **Step 5: Commit**

```sh
git add components/watchy_hal/include/watchy/buttons.h \
        components/watchy_hal/src/buttons.c
git commit -m "feat: capture buttons with a FreeRTOS event service"
```

---

### Task 3: Queue Consumers, Display Cancellation, and Sleep Lifecycle

**Files:**
- Modify: `main/main.c`
- Modify: `components/watchy_hal/src/display.c`
- Modify: `components/watchy_hal/src/power.c`
- Modify: `components/watchy_hal/src/power_policy.c`
- Modify: `components/watchy_hal/include/watchy/power.h`
- Modify: `tests/host/test_hal.c`

**Interfaces:**
- Consumes: the Task 2 event and lifecycle API.
- Produces: no new public interface beyond any sleep-requirement field needed to verify quiescence.

- [ ] **Step 1: Write failing integration-policy tests**

Add this literal helper and tests that fail if disabled motion is still required or sleep admission ignores a live button producer:

```c
static watchy_sleep_requirements_t valid_timer_sleep_requirements(void) {
    return (watchy_sleep_requirements_t){
        .timer_configured = true,
        .radios_stopped = true,
        .motor_off = true,
        .display_hibernated = true,
        .rtc_source_cleared = true,
        .motion_wake_enabled = true,
        .motion_source_configured = true,
        .buttons_quiesced = true,
        .ext0_configured = true,
        .ext1_configured = true,
        .sources_inactive = true,
    };
}

watchy_sleep_requirements_t requirements = valid_timer_sleep_requirements();
requirements.motion_wake_enabled = false;
requirements.motion_source_configured = false;
CHECK(watchy_power_sleep_allowed(&requirements));

requirements = valid_timer_sleep_requirements();
requirements.buttons_quiesced = false;
CHECK(!watchy_power_sleep_allowed(&requirements));
```

Refactor the existing sleep-admission test to use the helper while retaining all current fail-closed assertions. Existing transition tests remain the coverage for cancelled presentation ownership.

- [ ] **Step 2: Verify the focused tests fail for the missing semantics**

Run:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host --target watchy_hal_tests watchy_shell_tests -j8
```

Expected: failure because disabled motion still blocks admission and/or `buttons_quiesced` does not exist.

- [ ] **Step 3: Replace polling consumers and level cancellation**

In `main.c`:

- Remove `previous`, `current`, and `current & ~previous` edge detection from `run_shell` and `run_package_app`.
- First consume any existing presentation-cancelled mask; otherwise call `watchy_buttons_take_press(&event, WATCHY_BUTTON_POLL_MS)` and use `event.mask`.
- Preserve one-event-at-a-time ordering and the existing semantic input precedence inside a multi-bit event.
- Continue checking portal and idle deadlines on each bounded wait timeout.
- If `watchy_buttons_overflowed()` becomes true, report a bounded input/system failure instead of silently accepting lost navigation.

In `display.c`, remove baseline level comparison from the hardware transition context. At existing optional transition boundaries, consume one queued press event and place its mask in `s_cancelled_buttons`. A Cut/single-write presentation does not consume the queue, so a press captured during that write reaches the shell afterward.

- [ ] **Step 4: Make sleep lifecycle own input quiescence and disabled-motion semantics**

Add `bool buttons_quiesced` and `bool motion_wake_enabled` to `watchy_sleep_requirements_t`. In `watchy_power_sleep_allowed`, require `buttons_quiesced`, and require `motion_source_configured` only when `motion_wake_enabled` is true.

In both sleep-preparation paths:

1. Keep the input producer active while checking that wake pins have become inactive.
2. Quiesce it before converting button GPIOs to RTC mode.
3. Set `buttons_quiesced` only after successful quiescence.
4. Resume it if preparation returns before RTC wake ownership has safely transferred.

In `watchy_power_prepare_deep_sleep_with_motion`, initialize/configure the BMA423 only when motion wake is enabled. In `main.c`, remove the unconditional `timer_configured && !watchy_motion_ready()` initialization and initialize motion before sleep only when `settings.motion_wake` is true.

- [ ] **Step 5: Verify focused tests and firmware compilation**

Run:

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host --target watchy_hal_tests watchy_shell_tests -j8
./build/host/tests/host/watchy_hal_tests
./build/host/tests/host/watchy_shell_tests
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: focused suites pass and the firmware links successfully.

- [ ] **Step 6: Commit**

```sh
git add main/main.c components/watchy_hal/src/display.c \
        components/watchy_hal/src/power.c components/watchy_hal/src/power_policy.c \
        components/watchy_hal/include/watchy/power.h tests/host/test_hal.c
git commit -m "fix: preserve button events through blocking work"
```

---

### Task 4: Final Review, Milestone Gate, and Hardware Flash

**Files:**
- Modify: `docs/hardware-acceptance.md` only with evidence actually observed during this milestone.

**Interfaces:**
- Consumes: Tasks 1–3 and the hardware UAT list in the spec.
- Produces: a reviewed, reproducible firmware image flashed without erasing NVS or LittleFS.

- [ ] **Step 1: Run a broad delegated code review**

Review the entire diff from the pre-plan base through Task 3 for ISR safety, queue ownership, shutdown races, watchdog compatibility, package/display serialization, and exact-once event delivery. Resolve all Critical and Important findings before proceeding.

- [ ] **Step 2: Run the single full milestone gate**

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host -j8
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir build/host --output-on-failure
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
git diff --check
```

Expected: 14/14 host suites pass, firmware builds within RAM/flash budgets, and the worktree has no whitespace errors.

- [ ] **Step 3: Flash the verified application build**

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2 -t upload \
  --upload-port /dev/cu.usbserial-5B0B0425311
```

Do not erase NVS or LittleFS. Confirm the flashed ELF embeds the committed short SHA.

- [ ] **Step 4: Hand off physical UAT**

Ask the user to perform the seven hardware checks in the spec, beginning with one short Menu tap from a sleeping first-party WPK face. Record only observed outcomes in `docs/hardware-acceptance.md`.
