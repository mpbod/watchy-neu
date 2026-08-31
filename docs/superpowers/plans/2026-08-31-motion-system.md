# E-Paper Motion System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a kernel-governed e-paper transition compositor, persisted Full/Reduced/Off policy, and ABI v1.2 WPK transition requests without allowing packages to drive the panel directly.

**Architecture:** Pure transition validation, planning, composition, and execution live in `watchy_core`; the ESP-IDF display driver supplies physical-write hooks and retains panel authority. The shell selects system effects, while a prefix-compatible SDK function latches one WPK request for the next accepted render and the kernel may downgrade it.

**Tech Stack:** C11, C++17 header-only SDK wrapper, ESP-IDF 5.x, Watchy 2.0 200 x 200 one-bit framebuffer, CMake/CTest host tests.

**Spec:** `docs/superpowers/specs/2026-08-31-motion-system-design.md`

## Global Constraints

- Target Watchy 2.0 and its 200 x 200, 25-byte-stride monochrome display only.
- Count physical writes after the already-visible source; never exceed three writes for one optional transition.
- The kernel may shorten, replace, or reject every WPK request.
- No opacity, grayscale, easing, idle loops, pulses, or timer-driven animation.
- Every intermediate framebuffer must be independently legible.
- Full/Reduced/Off is distinct from accelerometer motion wake; direct ghost-clearing remains enabled in every mode.
- Battery voltage below 3,550 mV downgrades optional motion.
- Existing ABI v1.1 packages remain loadable; v1.2 adds only prefix-compatible fields.
- Reuse the display driver's existing RTC-retained previous framebuffer; do not add a second RTC framebuffer.
- Use test-first development and keep all existing host, Python, firmware-build, and hardware acceptance checks passing.
- Before host CMake commands in this workspace, run `export PATH="/Users/maxb/.platformio/packages/tool-cmake/bin:/Users/maxb/.platformio/packages/tool-ninja:$PATH"`.

---

## File Structure

- `sdk/include/watchy/sdk.h`: ABI v1.2 transition enums, request struct, Busy status, and appended system function.
- `sdk/include/watchy/sdk.hpp`: typed C++ `System::request_transition` wrapper.
- `components/watchy_core/include/watchy/transition.h`: trusted transition policy, plan, compositor, and executor interfaces.
- `components/watchy_core/src/transition_policy.c`: request validation and Full/Reduced/Off downgrade matrix.
- `components/watchy_core/src/transition_compositor.c`: pure one-bit Cut/Flash/Wipe/Push/Dither/Grow/Odometer/Split/Fill/Shutter frame generation.
- `components/watchy_core/src/transition_executor.c`: bounded write loop, cancellation boundaries, watchdog hook, and failure result.
- `components/watchy_hal/src/display.c`: physical presentation integration and existing-retained-buffer reuse.
- `components/watchy_packages/include/watchy/package_host.h`: one-request latch in each loaded package context.
- `components/watchy_packages/src/host_caps.c`: ABI v1.2 host callback and latch consumption.
- `components/watchy_packages/src/idf_runtime.c`: consume a package request and present through the trusted display path.
- `components/watchy_shell/include/watchy/settings.h`: persisted display-motion level.
- `components/watchy_shell/src/settings_policy.c`: defaults, validation, and sanitization.
- `components/watchy_shell/src/settings_idf.c`: NVS key `motion_fx`.
- `components/watchy_shell/include/watchy/shell.h`: transition selection result and updated Settings item count.
- `components/watchy_shell/src/shell_policy.c`: shell route-to-effect mapping and Motion setting action.
- `components/watchy_shell/src/shell_render.c`: Motion row copy.
- `main/main.c`: configure presentation context and route shell refreshes through the compositor.
- `tests/host/test_core.c`, `test_packages.c`, `test_shell.c`, `test_sdk_c.c`, `test_sdk_cpp.cpp`, `test_hal.c`: behavior and compatibility coverage.

### Task 1: Define the ABI v1.2 transition request

**Files:**
- Modify: `sdk/include/watchy/sdk.h`
- Modify: `sdk/include/watchy/sdk.hpp`
- Modify: `tests/host/test_sdk_c.c`
- Modify: `tests/host/test_sdk_cpp.cpp`

**Interfaces:**
- Consumes: existing prefix-compatible `watchy_system_api_v1_t`.
- Produces: `watchy_transition_effect_t`, `watchy_transition_direction_t`, `watchy_transition_rect_t`, `watchy_transition_request_v1_t`, `WATCHY_STATUS_BUSY`, and `System::request_transition(const watchy_transition_request_v1_t *)`.

- [ ] **Step 1: Add compile-time C and C++ tests for ABI values and the wrapper**

```c
_Static_assert(WATCHY_ABI_V1_MINOR == 2u, "ABI minor must be 1.2");
_Static_assert(WATCHY_TRANSITION_CUT == 0, "stable effect value");
_Static_assert(sizeof(((watchy_system_api_v1_t *)0)->request_transition) == sizeof(void *),
               "system API exposes transition request");
```

```cpp
watchy_transition_request_v1_t request{};
request.size = sizeof(request);
request.effect = WATCHY_TRANSITION_WIPE;
CHECK(watchy::System(&api).request_transition(&request) == WATCHY_STATUS_OK);
CHECK(captured.effect == WATCHY_TRANSITION_WIPE);
```

- [ ] **Step 2: Run the SDK tests and verify the new symbols are absent**

Run: `cmake -S . -B build/host -G Ninja && cmake --build build/host --target watchy_sdk_c_tests watchy_sdk_cpp_tests`

Expected: compilation fails on `WATCHY_ABI_V1_MINOR == 2u` and undefined transition types.

- [ ] **Step 3: Add exact ABI v1.2 definitions and append the function pointer**

```c
#define WATCHY_ABI_V1_MINOR 2u

typedef enum {
    WATCHY_TRANSITION_CUT = 0,
    WATCHY_TRANSITION_FLASH,
    WATCHY_TRANSITION_WIPE,
    WATCHY_TRANSITION_PUSH,
    WATCHY_TRANSITION_DITHER,
    WATCHY_TRANSITION_GROW,
    WATCHY_TRANSITION_ODOMETER,
    WATCHY_TRANSITION_SPLIT,
    WATCHY_TRANSITION_FILL,
    WATCHY_TRANSITION_SHUTTER,
} watchy_transition_effect_t;

typedef enum {
    WATCHY_TRANSITION_DIRECTION_NONE = 0,
    WATCHY_TRANSITION_DIRECTION_LEFT,
    WATCHY_TRANSITION_DIRECTION_RIGHT,
    WATCHY_TRANSITION_DIRECTION_UP,
    WATCHY_TRANSITION_DIRECTION_DOWN,
} watchy_transition_direction_t;

typedef struct { int16_t x, y, width, height; } watchy_transition_rect_t;

#define WATCHY_TRANSITION_HAS_RECT UINT32_C(1)
#define WATCHY_TRANSITION_PREFER_FULL UINT32_C(2)

typedef struct {
    uint32_t size;
    watchy_transition_effect_t effect;
    watchy_transition_direction_t direction;
    watchy_transition_rect_t rect;
    uint32_t flags;
    uint32_t reserved[2];
} watchy_transition_request_v1_t;
```

Append `request_transition` after `request_refresh` and add the wrapper without reordering any ABI v1.1 member. Assign `WATCHY_STATUS_BUSY = -5` without changing existing status values.

- [ ] **Step 4: Run both SDK tests**

Run: `cmake --build build/host --target watchy_sdk_c_tests watchy_sdk_cpp_tests && ./build/host/tests/host/watchy_sdk_c_tests && ./build/host/tests/host/watchy_sdk_cpp_tests`

Expected: both executables print PASS and return zero.

- [ ] **Step 5: Commit the ABI seam**

```bash
git add sdk/include/watchy/sdk.h sdk/include/watchy/sdk.hpp tests/host/test_sdk_c.c tests/host/test_sdk_cpp.cpp
git commit -m "feat: add ABI 1.2 transition requests"
```

### Task 2: Implement request validation and downgrade policy

**Files:**
- Create: `components/watchy_core/include/watchy/transition.h`
- Create: `components/watchy_core/src/transition_policy.c`
- Modify: `components/watchy_core/CMakeLists.txt`
- Modify: `tests/host/CMakeLists.txt`
- Modify: `tests/host/test_core.c`

**Interfaces:**
- Consumes: Task 1 SDK request types.
- Produces: `watchy_transition_level_t`, `watchy_transition_policy_context_t`, `watchy_transition_plan_t`, `watchy_transition_validate`, and `watchy_transition_plan`.

- [ ] **Step 1: Write table-driven failures and policy matrix tests**

```c
watchy_transition_request_v1_t request = {
    .size = sizeof(request), .effect = WATCHY_TRANSITION_WIPE,
    .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
};
watchy_transition_policy_context_t context = {
    .level = WATCHY_TRANSITION_LEVEL_FULL, .attended = true,
    .battery_mv = 3900u, .source_valid = true,
};
watchy_transition_plan_t plan;
CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
CHECK(plan.effect == WATCHY_TRANSITION_WIPE);
CHECK(plan.write_count == 3u);
context.level = WATCHY_TRANSITION_LEVEL_REDUCED;
CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
CHECK(plan.effect == WATCHY_TRANSITION_FLASH && plan.write_count == 2u);
context.level = WATCHY_TRANSITION_LEVEL_OFF;
CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);
```

Cover struct-size mismatch, nonzero reserved words, invalid flags/direction/effect, zero or overflowing rectangles, 3,549 mV, unattended wake, safe mode, unknown source, and mandatory Clear.

- [ ] **Step 2: Run the core test and observe the missing interface**

Run: `cmake --build build/host --target watchy_core_tests`

Expected: compilation fails because `watchy/transition.h` does not exist.

- [ ] **Step 3: Implement the public policy types and pure planner**

```c
typedef enum {
    WATCHY_TRANSITION_LEVEL_FULL = 0,
    WATCHY_TRANSITION_LEVEL_REDUCED = 1,
    WATCHY_TRANSITION_LEVEL_OFF = 2,
} watchy_transition_level_t;

typedef struct {
    watchy_transition_level_t level;
    bool attended;
    bool safe_mode;
    bool source_valid;
    bool clear_required;
    uint16_t battery_mv;
} watchy_transition_policy_context_t;

typedef struct {
    watchy_transition_effect_t effect;
    watchy_transition_direction_t direction;
    watchy_transition_rect_t rect;
    uint8_t write_count;
    bool target_full;
    bool mandatory_clear;
} watchy_transition_plan_t;
```

`watchy_transition_plan` must normalize an omitted rectangle to `{0,0,200,200}`, convert Reduced to Flash, convert Off/unsafe/unattended/unknown-source to Cut, and produce direct two-write Clear when `clear_required` is true.

- [ ] **Step 4: Run core tests**

Run: `cmake --build build/host --target watchy_core_tests && ./build/host/tests/host/watchy_core_tests`

Expected: policy matrix and all pre-existing core tests pass.

- [ ] **Step 5: Commit policy planning**

```bash
git add components/watchy_core tests/host/test_core.c tests/host/CMakeLists.txt
git commit -m "feat: plan bounded e-paper transitions"
```

### Task 3: Generate all ten one-bit effects

**Files:**
- Create: `components/watchy_core/src/transition_compositor.c`
- Modify: `components/watchy_core/include/watchy/transition.h`
- Modify: `components/watchy_core/CMakeLists.txt`
- Create: `tests/host/transition_golden.h`
- Modify: `tests/host/test_core.c`

**Interfaces:**
- Consumes: `watchy_transition_plan_t` from Task 2 and 5,000-byte source/target buffers.
- Produces: `watchy_transition_compose_frame(const watchy_transition_plan_t *, uint8_t, const uint8_t *, const uint8_t *, uint8_t *, size_t)`.

- [ ] **Step 1: Add fixed-pattern golden tests for every frame**

```c
uint8_t source[5000], target[5000], scratch[5000];
make_transition_fixture(source, target);
for (uint8_t frame = 0; frame < plan.write_count; ++frame) {
    CHECK(watchy_transition_compose_frame(&plan, frame, source, target,
                                          scratch, sizeof(scratch)) == WATCHY_STATUS_OK);
    CHECK(fnv1a(scratch, sizeof(scratch)) == expected_hash[effect][frame]);
}
CHECK(memcmp(scratch, target, sizeof(target)) == 0);
```

Use independent expected pixel assertions for leading edges and checkerboard parity before freezing FNV-1a hashes in `transition_golden.h`; do not derive expected frames with production helpers.

- [ ] **Step 2: Run the core test and confirm the compositor is missing**

Run: `cmake --build build/host --target watchy_core_tests`

Expected: link fails on `watchy_transition_compose_frame`.

- [ ] **Step 3: Implement clipped pixel/region primitives and effect dispatch**

```c
watchy_status_t watchy_transition_compose_frame(
    const watchy_transition_plan_t *plan, uint8_t frame_index,
    const uint8_t *source, const uint8_t *target,
    uint8_t *out, size_t size) {
    if (!valid_buffers(plan, frame_index, source, target, out, size))
        return WATCHY_STATUS_INVALID_ARGUMENT;
    memcpy(out, source, WATCHY_TRANSITION_FRAME_BYTES);
    switch (plan->effect) {
    case WATCHY_TRANSITION_CUT: copy_rect(out, target, plan->rect); break;
    case WATCHY_TRANSITION_FLASH: compose_flash(plan, frame_index, target, out); break;
    case WATCHY_TRANSITION_WIPE: compose_wipe(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_PUSH: compose_push(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_DITHER: compose_dither(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_GROW: compose_grow(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_ODOMETER: compose_odometer(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_SPLIT: compose_split(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_FILL: compose_fill(plan, frame_index, source, target, out); break;
    case WATCHY_TRANSITION_SHUTTER: compose_shutter(plan, frame_index, source, target, out); break;
    }
    return WATCHY_STATUS_OK;
}
```

Use integer halves/thirds/quarters, a deterministic `(x + y) & 1` Dither, one- or two-pixel leading rules, and a final exact target frame. Keep all loops bounded to 200 x 200.

- [ ] **Step 4: Run core tests and freeze reviewed hashes**

Run: `cmake --build build/host --target watchy_core_tests && ./build/host/tests/host/watchy_core_tests`

Expected: all ten effect sequences match independent pixel assertions and frozen hashes.

- [ ] **Step 5: Commit the pure compositor**

```bash
git add components/watchy_core tests/host/test_core.c tests/host/transition_golden.h
git commit -m "feat: compose one-bit transition frames"
```

### Task 4: Execute bounded physical-write sequences

**Files:**
- Create: `components/watchy_core/src/transition_executor.c`
- Modify: `components/watchy_core/include/watchy/transition.h`
- Modify: `components/watchy_core/CMakeLists.txt`
- Modify: `tests/host/test_core.c`

**Interfaces:**
- Consumes: Task 3 compositor.
- Produces: callback types `watchy_transition_write_fn`, `watchy_transition_cancel_fn`, `watchy_transition_feed_fn`, result struct `watchy_transition_result_t`, and `watchy_transition_execute`.

- [ ] **Step 1: Add injected-hook tests for success, cancellation, Clear, and each failing write**

```c
fake_writer_t writer = {.fail_at = SIZE_MAX};
watchy_transition_result_t result;
CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch),
        fake_write, fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
CHECK(writer.write_count == plan.write_count);
CHECK(writer.feed_count == plan.write_count);
CHECK(result.completed && result.source_valid);
```

Assert optional effects cancel after the current write, mandatory Clear ignores cancellation between its two full writes, a failure clears `source_valid`, and no callback occurs past `write_count`.

- [ ] **Step 2: Run the core test and observe the missing executor**

Run: `cmake --build build/host --target watchy_core_tests`

Expected: link fails on `watchy_transition_execute`.

- [ ] **Step 3: Implement the callback-driven executor**

```c
for (uint8_t frame = 0; frame < plan->write_count; ++frame) {
    status = watchy_transition_compose_frame(plan, frame, source, target, scratch, size);
    if (status != WATCHY_STATUS_OK || write(context, scratch, frame_mode(plan, frame)) != WATCHY_STATUS_OK) {
        result->source_valid = false;
        return WATCHY_STATUS_INVALID_STATE;
    }
    result->writes_completed++;
    if (feed != NULL) feed(context);
    if (!plan->mandatory_clear && frame + 1u < plan->write_count &&
        cancel != NULL && cancel(context)) {
        result->cancelled = true;
        result->last_frame_is_target = false;
        return WATCHY_STATUS_OK;
    }
}
```

Store no framebuffer inside the result; instead expose `last_frame_is_target` and let the caller copy the already-written scratch buffer. This keeps the result bounded and avoids accidental 5 KiB stack frames.

- [ ] **Step 4: Run core tests with stack warnings enabled**

Run: `cmake --build build/host --target watchy_core_tests && ./build/host/tests/host/watchy_core_tests`

Expected: execution tests pass and no test function allocates a transition plan or 5 KiB buffer on constrained firmware stacks.

- [ ] **Step 5: Commit the executor**

```bash
git add components/watchy_core tests/host/test_core.c
git commit -m "feat: execute bounded transition sequences"
```

### Task 5: Integrate the compositor with the display driver

**Files:**
- Modify: `components/watchy_hal/include/watchy/display.h`
- Modify: `components/watchy_hal/src/display.c`
- Modify: `components/watchy_hal/CMakeLists.txt`
- Modify: `components/watchy_hal/include/watchy/display_policy.h`
- Modify: `components/watchy_hal/src/display_policy.c`
- Modify: `tests/host/test_hal.c`

**Interfaces:**
- Consumes: Task 4 executor and existing RTC-retained previous frame.
- Produces: `watchy_display_set_transition_policy(...)`, `watchy_display_present(...)`, and `watchy_display_invalidate_previous()`; preserves `watchy_display_refresh` as a Cut-compatible wrapper.

- [ ] **Step 1: Add HAL policy tests for retained-source reuse and per-write counters**

```c
watchy_display_retained_state_t retained = valid_retained_state(18u);
CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20u) ==
      WATCHY_REFRESH_PARTIAL);
watchy_display_commit_refresh(&retained, WATCHY_REFRESH_PARTIAL, frame, sizeof(frame));
CHECK(retained.partial_count == 19u);
CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20u) ==
      WATCHY_REFRESH_FULL);
watchy_display_commit_refresh(&retained, WATCHY_REFRESH_FULL, frame, sizeof(frame));
CHECK(retained.partial_count == 0u);
```

Extend test helpers so a simulated multi-write transition calls prepare/commit once per physical write and failure invalidates the retained magic.

- [ ] **Step 2: Run HAL tests before the display API exists**

Run: `cmake --build build/host --target watchy_hal_tests`

Expected: compilation fails on the new retained invalidation helper.

- [ ] **Step 3: Refactor one physical write and add trusted presentation**

```c
static watchy_status_t refresh_current(watchy_refresh_mode_t requested) {
    /* Existing write_ram/update-control/wait/commit body, exactly once. */
}

watchy_status_t watchy_display_present(
    watchy_refresh_mode_t requested,
    const watchy_transition_request_v1_t *request) {
    watchy_transition_policy_context_t context = current_policy_context();
    watchy_transition_plan_t plan;
    if (watchy_transition_plan(request, &context, &plan) != WATCHY_STATUS_OK)
        return WATCHY_STATUS_INVALID_ARGUMENT;
    return execute_with_static_source_target_scratch(&plan, requested);
}

watchy_status_t watchy_display_refresh(watchy_refresh_mode_t requested) {
    return watchy_display_present(requested, NULL);
}
```

Copy the already-rendered `s_framebuffer` into a static target buffer before using `s_framebuffer` for intermediate writes. Read source from valid `s_retained.previous_frame`; otherwise policy downgrades to Cut. Reset the task watchdog and sample a baseline-relative button mask between writes. Invalidate retained state on any physical-write failure.

- [ ] **Step 4: Run host tests and build the ESP-IDF component**

Run: `cmake --build build/host --target watchy_hal_tests && ./build/host/tests/host/watchy_hal_tests`

Run: `platformio run -e watchy`

Expected: HAL tests pass; firmware compiles with `-Wframe-larger-than=2048` and no new large automatic arrays.

- [ ] **Step 5: Commit trusted display presentation**

```bash
git add components/watchy_hal tests/host/test_hal.c
git commit -m "feat: present transitions through display policy"
```

### Task 6: Latch one WPK transition request and consume it on render

**Files:**
- Modify: `components/watchy_packages/include/watchy/package_host.h`
- Modify: `components/watchy_packages/src/package_policy.c`
- Modify: `components/watchy_packages/src/host_caps.c`
- Modify: `components/watchy_packages/src/idf_runtime.c`
- Modify: `tests/host/test_packages.c`

**Interfaces:**
- Consumes: Task 1 request and Task 5 display presentation.
- Produces: `watchy_package_transition_latch`, `watchy_package_transition_take`, and the host callback behind `System::request_transition`.

- [ ] **Step 1: Write pure latch validation and consume-once tests**

```c
watchy_package_transition_latch_t latch = {0};
CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_BUSY);
watchy_transition_request_v1_t taken;
CHECK(watchy_package_transition_take(&latch, &taken));
CHECK(taken.effect == valid.effect);
CHECK(!watchy_package_transition_take(&latch, &taken));
```

Also prove invalid requests do not occupy the latch and teardown clears an unconsumed request.

- [ ] **Step 2: Run package tests and confirm missing latch symbols**

Run: `cmake --build build/host --target watchy_packages_tests`

Expected: compilation fails on `watchy_package_transition_latch_t`.

- [ ] **Step 3: Implement the latch and host callback**

```c
static watchy_status_t host_request_transition(
    void *opaque, const watchy_transition_request_v1_t *request) {
    watchy_package_host_context_t *context = opaque;
    if (context == NULL || !context->callback_budget.active)
        return WATCHY_STATUS_INVALID_STATE;
    return watchy_package_transition_latch(&context->transition, request);
}
```

Append the function pointer when initializing `context->system`. After a successful render, `idf_runtime.c` takes the request and calls `watchy_display_present`; callback failure or an invalid render still discards the request during host cleanup.

- [ ] **Step 4: Run package and SDK tests**

Run: `cmake --build build/host --target watchy_packages_tests watchy_sdk_c_tests watchy_sdk_cpp_tests && ctest --test-dir build/host --output-on-failure`

Expected: all host tests pass, including ABI v1.1 package fixtures.

- [ ] **Step 5: Commit WPK presentation requests**

```bash
git add components/watchy_packages tests/host/test_packages.c
git commit -m "feat: honor bounded WPK transition requests"
```

### Task 7: Persist motion level and map shell routes

**Files:**
- Modify: `components/watchy_shell/include/watchy/settings.h`
- Modify: `components/watchy_shell/src/settings_policy.c`
- Modify: `components/watchy_shell/src/settings_idf.c`
- Modify: `components/watchy_shell/include/watchy/shell.h`
- Modify: `components/watchy_shell/src/shell_policy.c`
- Modify: `components/watchy_shell/src/shell_render.c`
- Modify: `tests/host/test_shell.c`

**Interfaces:**
- Consumes: Task 2 `watchy_transition_level_t` and Task 1 request type.
- Produces: `settings.transition_level`, NVS key `motion_fx`, `watchy_shell_transition_for_change`, and Settings cycling Full→Reduced→Off→Full.

- [ ] **Step 1: Add settings sanitize and route mapping tests**

```c
watchy_settings_defaults(&settings);
CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_FULL);
stored.transition_level = (watchy_transition_level_t)99;
watchy_settings_sanitize(&stored, &settings);
CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_FULL);

watchy_shell_transition_context_t change = {
    .from = WATCHY_SHELL_WATCHFACE, .to = WATCHY_SHELL_LAUNCHER,
    .input = WATCHY_SHELL_INPUT_MENU,
};
watchy_transition_request_v1_t request;
CHECK(watchy_shell_transition_for_change(&change, &request));
CHECK(request.effect == WATCHY_TRANSITION_WIPE);
```

Cover forward/reverse Push, saved Flash, sleep Split, row Cut, and safe-mode Cut.

- [ ] **Step 2: Run shell tests and observe missing fields/functions**

Run: `cmake --build build/host --target watchy_shell_tests`

Expected: compilation fails on `transition_level` and `watchy_shell_transition_for_change`.

- [ ] **Step 3: Add policy, NVS, Settings row, and pure route mapping**

```c
static watchy_transition_level_t next_transition_level(watchy_transition_level_t value) {
    return value == WATCHY_TRANSITION_LEVEL_FULL ? WATCHY_TRANSITION_LEVEL_REDUCED
         : value == WATCHY_TRANSITION_LEVEL_REDUCED ? WATCHY_TRANSITION_LEVEL_OFF
                                                    : WATCHY_TRANSITION_LEVEL_FULL;
}
```

Persist `motion_fx` with `nvs_get_u8`/`nvs_set_u8`; keep the existing `motion` key for accelerometer wake. Render the label as `MOTION FULL`, `MOTION REDUCED`, or `MOTION OFF`. Route mappings return a complete, zero-reserved request.

- [ ] **Step 4: Run shell and full host tests**

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Expected: all host test executables pass.

- [ ] **Step 5: Commit motion settings and shell semantics**

```bash
git add components/watchy_shell tests/host/test_shell.c
git commit -m "feat: add display motion settings"
```

### Task 8: Wire boot policy, shell presentation, recovery, and documentation

**Files:**
- Modify: `main/main.c`
- Modify: `README.md`
- Modify: `docs/sdk.md`
- Modify: `docs/hardware-acceptance.md`
- Modify: `samples/digital-watchface/main/digital_watchface.cpp`
- Modify: `tests/host/test_digital_sample.cpp`

**Interfaces:**
- Consumes: Tasks 1–7.
- Produces: complete on-device transition flow and one ABI v1.2 sample request.

- [ ] **Step 1: Add a sample-package test proving request use remains optional**

```cpp
watchy_transition_request_v1_t request{};
request.size = sizeof(request);
request.effect = WATCHY_TRANSITION_ODOMETER;
request.direction = WATCHY_TRANSITION_DIRECTION_UP;
request.rect = {146, 55, 30, 54};
request.flags = WATCHY_TRANSITION_HAS_RECT;
CHECK(sample_requests_transition_after_minute_change(request_capture));
```

The sample must still render successfully when the host reports ABI v1.1 or returns `WATCHY_STATUS_UNSUPPORTED`.

- [ ] **Step 2: Run the sample test before integration**

Run: `cmake --build build/host --target watchy_digital_sample_tests`

Expected: the new request-capture assertion fails.

- [ ] **Step 3: Configure policy before package rendering and pass shell transitions**

```c
watchy_display_set_transition_policy(settings.transition_level,
    wake_cause == WATCHY_WAKE_BUTTON, safe_mode, battery.millivolts);
```

Capture shell state before each input/action, call `watchy_shell_transition_for_change`, and pass the result into `watchy_display_present` from `refresh_shell`. On `WATCHY_STATUS_INVALID_STATE`, call `watchy_display_invalidate_previous`, enter the existing display error screen, and force the next successful target to full refresh. Update the digital sample to request Odometer only for an attended button-driven render, leaving minute wakes as Cut.

- [ ] **Step 4: Document exact policy and run all automated checks**

Document Full/Reduced/Off, the `motion_fx`/accelerometer distinction, ABI v1.2 consume-once rules, physical-write budgets, downgrade conditions, and Clear behavior.

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Run: `python3 -m unittest discover -s tests/python -v`

Run: `python3 tools/build_samples.py`

Run: `platformio run -e watchy`

Expected: all host and Python tests pass, both sample WPKs build/audit/verify, and firmware links within the 1.75 MiB factory partition.

- [ ] **Step 5: Perform device UAT and record evidence**

Flash using the existing non-destructive firmware flow. Verify Wipe face→Menu, forward/reverse Push, localized Flash, Split before user sleep, Cut on minute wake, Full/Reduced/Off, low-battery downgrade by injected policy test build, cancellation at each safe boundary, direct Clear, and recovery after an injected intermediate-write failure. Record results and measured timing/current in `docs/hardware-acceptance.md`.

- [ ] **Step 6: Commit the integrated motion system**

```bash
git add main/main.c README.md docs/sdk.md docs/hardware-acceptance.md samples/digital-watchface tests/host/test_digital_sample.cpp
git commit -m "feat: integrate e-paper motion system"
```

### Task 9: Final regression and review checkpoint

**Files:**
- Modify only files required by failures found in this task.

**Interfaces:**
- Consumes: complete motion subsystem.
- Produces: review-ready motion foundation for the watchface-gallery plan.

- [ ] **Step 1: Run clean reproducibility and size checks**

```bash
cmake --fresh -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tests/python -v
python3 tools/build_samples.py
platformio run -e watchy
```

Expected: zero failures; repeated package builds produce byte-identical WPKs; firmware size remains below the factory partition limit.

- [ ] **Step 2: Inspect stack, symbol, and worktree evidence**

```bash
find .pio/build/watchy -name '*.su' -print0 | xargs -0 rg 'transition|display|refresh'
git diff --check
git status --short
```

Expected: transition/display paths have no frame larger than 2,048 bytes, no unsupported symbols enter package ELFs, no whitespace errors exist, and only intentional UAT documentation changes remain.

- [ ] **Step 3: Commit any evidence-only acceptance update**

```bash
git add docs/hardware-acceptance.md
git commit -m "test: record motion system acceptance"
```
