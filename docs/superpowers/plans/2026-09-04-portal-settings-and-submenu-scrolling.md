# Portal Settings and Submenu Scrolling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a captive AP portal matching the monochrome handoff, expose all real Watchy settings including Wi-Fi/NTP/home timezone/date-time, and make every long on-watch submenu scroll one row at a time.

**Architecture:** Keep `watchy_settings_t` and its NVS writer as the single source of truth. Add pure-C policy seams for timezone conversion, portal mutation, scrolling, NTP ownership, and DNS packet construction; keep ESP-IDF networking, SNTP, RTC I/O, JSON parsing, and HTTP serving in IDF adapters. Embed one dependency-free HTML/CSS/JS file in the firmware and preserve the existing package promotion and portal-exit reconciliation flows.

**Tech Stack:** ESP-IDF 5.5, C11, FreeRTOS/lwIP, `esp_http_server`, ESP-IDF cJSON, PCF8563 RTC HAL, existing Watchy transition/UI primitives, CMake/CTest host tests, Python `unittest` static web checks, PlatformIO ESP32 target build.

**Spec:** `docs/superpowers/specs/2026-09-04-portal-settings-and-submenu-scrolling-design.md`

## Global Constraints

- Target only Watchy 2.0 / ESP32-PICO-D4 with the existing 4 MiB partition layout.
- Firmware OTA remains excluded; no Updates portal route or simulated firmware controls.
- Portal assets must be embedded and work without Internet, CDN, remote fonts, or a JavaScript framework.
- The AP password is exactly 8 unambiguous random characters; the independent HTTP Basic session credential remains 32 hex characters.
- Idle timeout is exactly 5 minutes and absolute timeout remains 30 minutes.
- The session credential and saved Wi-Fi password must never appear in HTML, JavaScript, URLs, logs, or JSON responses.
- AP captive discovery runs only in AP mode; HTTPS interception is not implemented.
- Wi-Fi and radios remain enabled only for explicit portal or NTP operations.
- Keep package activation, pending promotion, rollback, quarantine, safe mode, and full-refresh return behavior unchanged.
- Use focused tests during development. Run the complete host and ESP32 milestone gate only after integration review.
- Normal flash must preserve NVS and LittleFS.

---

### Task 1: Short AP Credential and Five-Minute Idle Lifetime

**Files:**
- Modify: `components/watchy_shell/include/watchy/portal.h`
- Modify: `components/watchy_shell/src/portal_policy.c`
- Modify: `components/watchy_shell/src/portal_idf.c`
- Modify: `tests/host/test_portal.c`
- Modify: `tests/python/test_reproducibility.py`

**Interfaces:**
- Consumes: `watchy_portal_entropy_api_t`, `watchy_portal_session_accept`.
- Produces: `WATCHY_PORTAL_AP_PASSWORD_SIZE == 8`, `WATCHY_PORTAL_IDLE_TIMEOUT_MS == 300000`, unchanged `WATCHY_PORTAL_TOKEN_HEX_SIZE == 32` / absolute timeout, and `watchy_portal_password_from_digest(const uint8_t digest[32], char *out, size_t size)` used by the real AP derivation path.

- [ ] **Step 1: Write failing credential and timeout tests**

```c
static int test_ap_password_is_eight_unambiguous_characters(void) {
    entropy_fixture_t fixture = deterministic_entropy();
    watchy_portal_entropy_api_t entropy = fixture_api(&fixture);
    char password[WATCHY_PORTAL_AP_PASSWORD_SIZE + 1u];
    CHECK(WATCHY_PORTAL_AP_PASSWORD_SIZE == 8u);
    CHECK(watchy_portal_generate_ap_password(&entropy, password, sizeof(password)));
    CHECK(strlen(password) == 8u);
    CHECK(strpbrk(password, "01OIl") == NULL);
    CHECK(!watchy_portal_generate_ap_password(&entropy, password, sizeof(password) - 1u));
    uint8_t digest[32] = {0};
    CHECK(watchy_portal_password_from_digest(digest, password, sizeof(password)));
    CHECK(strlen(password) == WATCHY_PORTAL_AP_PASSWORD_SIZE);
    return 0;
}

static int test_idle_lifetime_is_five_minutes_with_thirty_minute_cap(void) {
    uint64_t last = 1000u;
    CHECK(!watchy_portal_idle_expired(last, last + 299999u));
    CHECK(watchy_portal_idle_expired(last, last + 300000u));
    CHECK(WATCHY_PORTAL_ABSOLUTE_TIMEOUT_MS == 1800000u);
    CHECK(watchy_portal_session_accept(1000u, last, last + 299999u, true, &last));
    CHECK(!watchy_portal_session_accept(1000u, last, 1000u + 1800000u, true, &last));
    return 0;
}
```

- [ ] **Step 2: Run the focused portal test and verify RED**

Run `cmake --build build/host --target watchy_portal_tests && ./build/host/watchy_portal_tests`.

Expected: compile or assertion failure because the public AP password size is absent and the old generator emits 16 characters / timeout remains ten minutes.

- [ ] **Step 3: Implement the shared policy and IDF derivation change**

```c
#define WATCHY_PORTAL_AP_PASSWORD_SIZE 8u
#define WATCHY_PORTAL_IDLE_TIMEOUT_MS (5u * 60u * 1000u)
#define WATCHY_PORTAL_ABSOLUTE_TIMEOUT_MS (30u * 60u * 1000u)

bool watchy_portal_generate_ap_password(const watchy_portal_entropy_api_t *entropy,
                                        char *out_password,
                                        size_t out_size) {
    uint8_t bytes[32];
    bool mapped;
    if (out_password == NULL || out_size < WATCHY_PORTAL_AP_PASSWORD_SIZE + 1u) return false;
    if (!watchy_portal_fill_guaranteed_entropy(entropy, bytes, sizeof(bytes))) return false;
    mapped = watchy_portal_password_from_digest(bytes, out_password, out_size);
    memset(bytes, 0, sizeof(bytes));
    return mapped;
}
```

Move digest-to-alphabet mapping into `watchy_portal_password_from_digest` and make both the direct entropy generator and IDF `derive_ap_password` use it. Keep the seed/counter SHA-256 and zeroization unchanged. Add a reproducibility topology assertion that `derive_ap_password` calls the shared mapper, `start_network` derives into `config.password`, and the same config is passed to `watchy_wifi_start_ap(&config)`. Also assert the IDF adapter contains no `out_size < 17u`, `index < 16u`, or `out_password[16]` credential literals.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the Task 1 command again, then `python3 -m unittest tests.python.test_reproducibility`. Expected: portal policy and IDF credential-derivation assertions pass.

- [ ] **Step 5: Commit Task 1**

```sh
git add components/watchy_shell/include/watchy/portal.h components/watchy_shell/src/portal_policy.c components/watchy_shell/src/portal_idf.c tests/host/test_portal.c tests/python/test_reproducibility.py
git commit -m "feat: simplify portal AP credentials"
```

---

### Task 2: Home-Timezone and Calendar Policy

**Files:**
- Modify: `components/watchy_shell/include/watchy/settings.h`
- Modify: `components/watchy_shell/src/settings_policy.c`
- Modify: `tests/host/CMakeLists.txt`
- Modify: `tests/host/test_shell.c`

**Interfaces:**
- Consumes: existing `watchy_settings_t.timezone`, `watchy_calendar_valid`, and settings validation.
- Produces:

```c
size_t watchy_settings_timezone_count(void);
bool watchy_settings_timezone_offset_at(size_t index, int16_t *out_minutes);
bool watchy_settings_timezone_index(const watchy_settings_t *settings,
                                    size_t *out_index);
bool watchy_settings_timezone_offset(const watchy_settings_t *settings,
                                     int16_t *out_minutes);
bool watchy_settings_set_timezone_offset(watchy_settings_t *settings,
                                         int16_t minutes);
bool watchy_settings_format_timezone(const watchy_settings_t *settings,
                                     char *out, size_t size);
bool watchy_settings_local_time_valid(const watchy_time_t *time);
```

- [ ] **Step 1: Write failing timezone round-trip tests**

```c
static int test_home_timezone_offsets_round_trip_without_changing_legacy_storage(void) {
    watchy_settings_t settings;
    char label[16];
    size_t index = 0u;
    watchy_settings_defaults(&settings);
    CHECK(watchy_settings_timezone_count() == 38u);
    CHECK(watchy_settings_set_timezone_offset(&settings, 420));
    CHECK(strcmp(settings.timezone, "UTC-7") == 0);
    CHECK(watchy_settings_format_timezone(&settings, label, sizeof(label)));
    CHECK(strcmp(label, "UTC+07:00") == 0);
    CHECK(watchy_settings_timezone_index(&settings, &index));
    CHECK(watchy_settings_set_timezone_offset(&settings, -210));
    CHECK(strcmp(settings.timezone, "UTC3:30") == 0);
    CHECK(watchy_settings_format_timezone(&settings, label, sizeof(label)));
    CHECK(strcmp(label, "UTC-03:30") == 0);
    CHECK(!watchy_settings_set_timezone_offset(&settings, 75));
    strcpy(settings.timezone, "EST5EDT,M3.2.0,M11.1.0");
    CHECK(!watchy_settings_timezone_index(&settings, &index));
    CHECK(watchy_settings_format_timezone(&settings, label, sizeof(label)));
    CHECK(strcmp(label, "CUSTOM") == 0);
    return 0;
}
```

- [ ] **Step 2: Write failing date/time boundary tests**

```c
static int test_portal_local_time_accepts_only_valid_2000_to_2099_values(void) {
    watchy_time_t time = {.year = 2028, .month = 2, .day = 29,
                          .hour = 23, .minute = 59, .second = 0};
    CHECK(watchy_settings_local_time_valid(&time));
    time.year = 1999;
    CHECK(!watchy_settings_local_time_valid(&time));
    time.year = 2100;
    CHECK(!watchy_settings_local_time_valid(&time));
    time.year = 2027;
    CHECK(!watchy_settings_local_time_valid(&time));
    time.year = 2028;
    time.hour = 24;
    CHECK(!watchy_settings_local_time_valid(&time));
    return 0;
}
```

- [ ] **Step 3: Run the focused shell test and verify RED**

Run `cmake --build build/host --target watchy_shell_tests && ./build/host/watchy_shell_tests`.

Expected: link failure for the new timezone/calendar policy functions.

- [ ] **Step 4: Implement the fixed-offset table and converters**

```c
static const int16_t timezone_offsets[] = {
    -720, -660, -600, -570, -540, -480, -420, -360, -300, -240,
    -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300,
    330, 345, 360, 390, 420, 480, 525, 540, 570, 600, 630, 660,
    720, 765, 780, 840,
};
```

The setter builds a candidate, writes `UTC0`, `UTC-7`, or `UTC3:30`, and copies it back only when `watchy_settings_valid(&candidate)` succeeds. `watchy_settings_timezone_offset` parses those fixed forms and rejects custom legacy POSIX rules. The formatter recognizes normalized fixed forms; other valid POSIX values produce `CUSTOM`. Date validation calls `watchy_calendar_valid` and additionally enforces years 2000 through 2099 and seconds 0 through 59. Add `components/watchy_hal/src/rtc_calendar.c` to both `watchy_shell_tests` and `watchy_shell_render_tests` so every settings-policy host target links the production calendar validator.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run the Task 2 focused command. Expected: all shell policy tests pass.

- [ ] **Step 6: Commit Task 2**

```sh
git add components/watchy_shell/include/watchy/settings.h components/watchy_shell/src/settings_policy.c tests/host/CMakeLists.txt tests/host/test_shell.c
git commit -m "feat: add home timezone policy"
```

---

### Task 3: Continuous List Viewports and On-Watch Timezone Screen

**Files:**
- Modify: `components/watchy_shell/include/watchy/shell.h`
- Create: `components/watchy_shell/include/watchy/timezone_action.h`
- Modify: `components/watchy_shell/src/shell_policy.c`
- Modify: `components/watchy_shell/src/shell_render.c`
- Create: `components/watchy_shell/src/timezone_action_policy.c`
- Modify: `components/watchy_hal/include/watchy/rtc.h`
- Modify: `components/watchy_hal/src/rtc.c`
- Modify: `components/watchy_shell/CMakeLists.txt`
- Modify: `main/main.c`
- Modify: `tests/host/CMakeLists.txt`
- Modify: `tests/host/test_shell.c`
- Modify: `tests/host/test_shell_render.c`

**Interfaces:**
- Consumes: Task 2 timezone table/converters and current shell input/action contracts.
- Produces `WATCHY_SHELL_TIMEZONE`, `watchy_shell_t.view_start`, a bounded four-frame navigation history, `watchy_shell_visible_start`, `watchy_shell_set_home_timezone_index`, `WATCHY_SHELL_ACTION_SAVE_TIMEZONE` with `numeric_value` offset minutes, and `watchy_rtc_set_utc_offset` for changing the persisted display offset without rewriting UTC registers.

```c
typedef struct {
    watchy_status_t (*set_rtc_offset)(void *context, int16_t minutes);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    watchy_status_t (*read_local)(void *context, watchy_time_t *out_time);
    void *context;
} watchy_timezone_action_ops_t;

watchy_status_t watchy_timezone_action_apply(
    watchy_settings_t *settings,
    const watchy_settings_t *candidate,
    watchy_time_t *time,
    const watchy_timezone_action_ops_t *ops);
```

- [ ] **Step 1: Write failing continuous-scroll policy tests**

```c
static int test_long_submenus_scroll_one_row_and_wrap_the_viewport(void) {
    watchy_shell_t shell = {.screen = WATCHY_SHELL_SETTINGS};
    for (unsigned press = 0u; press < 3u; ++press) {
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    }
    CHECK(shell.selection == 3u);
    CHECK(watchy_shell_visible_start(&shell) == 1u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(shell.selection == 2u);
    CHECK(watchy_shell_visible_start(&shell) == 1u);
    shell.selection = 0u;
    shell.view_start = 0u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(shell.selection == 10u);
    CHECK(watchy_shell_visible_start(&shell) == 8u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    CHECK(shell.selection == 0u);
    CHECK(watchy_shell_visible_start(&shell) == 0u);
    return 0;
}
```

Repeat the boundary assertions for a two-row Portal screen, timezone count, watchfaces, apps, and diagnostics after their counts are populated.

- [ ] **Step 2: Write failing navigation restoration and timezone-action tests**

```c
static int test_timezone_confirm_saves_offset_and_back_restores_settings_cursor(void) {
    watchy_shell_t shell;
    watchy_shell_action_request_t request;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    shell.selection = 2u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    shell.selection = 1u;
    watchy_shell_set_home_timezone_index(&shell, 26u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_TIMEZONE);
    CHECK(shell.selection == 26u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_SETTINGS);
    CHECK(shell.selection == 1u);
    CHECK(watchy_shell_take_action_request(&shell, &request));
    CHECK(request.action == WATCHY_SHELL_ACTION_SAVE_TIMEZONE);
    CHECK(request.numeric_value == 420);
    return 0;
}
```

Add a cancel case with no action and a nested Back sequence proving Settings returns to Launcher row 2 and Launcher returns to Watchface.

- [ ] **Step 3: Run focused shell targets and verify RED**

Run `cmake --build build/host --target watchy_shell_tests watchy_shell_render_tests`, then both binaries. Expected: compile/link failure for viewport/timezone interfaces and old settings order assertions.

- [ ] **Step 4: Implement navigation history and viewport policy**

Add a four-frame stack containing screen, selection, and viewport. Push the exact current state on drill-in and pop on Back. Seed Watchface as the parent of a launcher entered by button wake. Clear history on direct Watchface selection or idle return. Keep `return_screen` synchronized to the stack top for compatibility.

```c
static void reveal_selection(watchy_shell_t *shell, uint8_t count) {
    if (count <= WATCHY_SHELL_VISIBLE_ROWS || shell->selection == 0u) {
        shell->view_start = 0u;
    } else if (shell->selection == count - 1u && shell->view_start == 0u) {
        shell->view_start = (uint8_t)(count - WATCHY_SHELL_VISIBLE_ROWS);
    } else if (shell->selection < shell->view_start) {
        shell->view_start = shell->selection;
    } else if (shell->selection >= shell->view_start + WATCHY_SHELL_VISIBLE_ROWS) {
        shell->view_start =
            (uint8_t)(shell->selection - WATCHY_SHELL_VISIBLE_ROWS + 1u);
    }
}
```

- [ ] **Step 5: Add Home Zone and render from the continuous viewport**

```c
static const char *const settings_labels[] = {
    "Clock", "Home Zone", "Motion Wake", "Display Motion", "Set Time",
    "NTP Sync", "Wi-Fi", "Portal", "Refresh", "Diagnostics", "About",
};
```

Render every long list from `shell->view_start`. Render timezone rows with Task 2 labels and the proportional rail. Keep same-screen Up/Down as single-write Cut presentations.

- [ ] **Step 6: Wire timezone persistence and shifted indexes in `main.c`**

```c
case WATCHY_SHELL_ACTION_SAVE_TIMEZONE:
    if (!watchy_settings_set_timezone_offset(settings,
                                             (int16_t)action_request.numeric_value) ||
        watchy_settings_save(settings) != WATCHY_STATUS_OK) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
    } else {
        saved = true;
    }
    break;
```

Set the shell home-zone index after settings load and portal-exit reload. Shift direct settings mutations to Clock 0, Motion Wake 2, Display Motion 3, and Refresh 8. Include the zone label in the 192-byte manual-time detail buffer.

Implement `watchy_rtc_set_utc_offset(int16_t minutes)` by validating `-1439..1439`, persisting the offset, and updating the in-memory RTC offset only after NVS succeeds. `watchy_timezone_action_apply` compares current and candidate fixed offsets. When changed, it preserves the prior settings and `time->utc_offset_minutes`, writes the candidate RTC offset, saves the complete candidate settings, and rolls the RTC offset back if settings save fails. When unchanged, it saves the candidate without an RTC write. On success, copy the candidate into the live settings and call `read_local` so the UI changes zones while PCF8563 UTC registers preserve the instant. On portal exit, compare reloaded settings offset with `time->utc_offset_minutes`, apply the RTC offset, and reread local time before returning to the face.

Test the action helper with injected RTC-offset/save/read operations. Prove new offset success, RTC failure with no settings write, settings failure with RTC rollback, unchanged-offset candidate save without RTC mutation, and successful zone change preserving a fixed UTC epoch while changing the local hour. Manual-time save must set `time->utc_offset_minutes` from the current fixed home zone immediately before calling `watchy_rtc_set_local`.

- [ ] **Step 7: Update render expectations and verify GREEN**

Assert the 11 labels, adjacent one-row viewport frames, timezone labels, and proportional rail. Run the Task 3 focused binaries until green.

- [ ] **Step 8: Commit Task 3**

```sh
git add components/watchy_shell/include/watchy/shell.h components/watchy_shell/include/watchy/timezone_action.h components/watchy_shell/src/shell_policy.c components/watchy_shell/src/shell_render.c components/watchy_shell/src/timezone_action_policy.c components/watchy_hal/include/watchy/rtc.h components/watchy_hal/src/rtc.c components/watchy_shell/CMakeLists.txt main/main.c tests/host/CMakeLists.txt tests/host/test_shell.c tests/host/test_shell_render.c
git commit -m "feat: scroll watch submenus continuously"
```

---

### Task 4: Shared NTP Synchronization Service

**Files:**
- Create: `components/watchy_shell/include/watchy/time_sync.h`
- Create: `components/watchy_shell/src/time_sync_policy.c`
- Create: `components/watchy_shell/src/time_sync_idf.c`
- Modify: `components/watchy_shell/CMakeLists.txt`
- Modify: `main/main.c`
- Modify: `tests/host/CMakeLists.txt`
- Create: `tests/host/test_time_sync.c`

**Interfaces:**
- Consumes: saved Wi-Fi/NTP/timezone values, `watchy_wifi_*`, SNTP, calendar conversion, and RTC HAL.
- Produces:

```c
typedef enum {
    WATCHY_TIME_SYNC_START_SAVED_WIFI = 0,
    WATCHY_TIME_SYNC_BORROW_CONNECTED_WIFI,
    WATCHY_TIME_SYNC_REJECT_CLIENT_MODE_REQUIRED,
    WATCHY_TIME_SYNC_REJECT_NO_WIFI,
} watchy_time_sync_decision_t;

watchy_time_sync_decision_t watchy_time_sync_decide(
    bool wifi_configured,
    bool portal_active,
    watchy_portal_network_mode_t portal_mode);
watchy_status_t watchy_time_sync_connected(const watchy_settings_t *settings);
watchy_status_t watchy_time_sync_saved_wifi(const watchy_settings_t *settings);
watchy_status_t watchy_time_sync_local_from_epoch(
    const watchy_settings_t *settings,
    int64_t utc_epoch,
    watchy_time_t *out_local);
```

- [ ] **Step 1: Write failing ownership-policy tests**

```c
static int test_ntp_ownership_distinguishes_watch_client_portal_and_ap(void) {
    CHECK(watchy_time_sync_decide(true, false, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_START_SAVED_WIFI);
    CHECK(watchy_time_sync_decide(true, true, WATCHY_PORTAL_NETWORK_CLIENT) ==
          WATCHY_TIME_SYNC_BORROW_CONNECTED_WIFI);
    CHECK(watchy_time_sync_decide(true, true, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_REJECT_CLIENT_MODE_REQUIRED);
    CHECK(watchy_time_sync_decide(false, false, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_REJECT_NO_WIFI);
    return 0;
}
```

Add a fixed-epoch case that first sets `settings` to UTC+07:00, calls `watchy_time_sync_local_from_epoch`, and asserts the returned calendar has `utc_offset_minutes == 420` and an hour seven greater than the UTC calendar. Repeat with UTC-03:30 across a date boundary. This is the regression proving a changed Home Zone is the offset written by the NTP path.

- [ ] **Step 2: Add the focused host target and verify RED**

Register `watchy_time_sync_tests` from `test_time_sync.c`, `time_sync_policy.c`, `settings_policy.c`, `rtc_calendar.c`, and `package_format.c`, with shell/HAL/package/core/SDK include directories. Then run `cmake -S tests/host -B build/host -G Ninja && cmake --build build/host --target watchy_time_sync_tests && ./build/host/watchy_time_sync_tests`.

Expected: compile/link failure until the decision interface exists.

- [ ] **Step 3: Implement the pure ownership decision and verify GREEN**

Return exactly the four outcomes asserted above. Re-run only `watchy_time_sync_tests` and confirm it passes.

- [ ] **Step 4: Extract the connected SNTP primitive**

Move the existing SNTP initialization, 10-second wait, timezone conversion, and RTC write out of `main.c`. For a fixed Home Zone, `watchy_time_sync_connected` must pass the synchronized epoch through the host-tested `watchy_time_sync_local_from_epoch`; a custom legacy POSIX zone may use the existing `TZ`/`tzset` fallback. It requires `WATCHY_WIFI_STA_CONNECTED`, deinitializes SNTP on every started path, and never stops Wi-Fi. `watchy_time_sync_saved_wifi` starts saved station Wi-Fi, waits up to `WATCHY_NTP_WIFI_TIMEOUT_MS`, invokes the connected primitive, and stops Wi-Fi on every path.

```c
if (watchy_time_sync_saved_wifi(settings) == WATCHY_STATUS_OK &&
    watchy_rtc_read_local(time) == WATCHY_STATUS_OK) {
    snprintf(detail, sizeof(detail), "TIME UPDATED");
} else {
    watchy_shell_fail(shell, WATCHY_SHELL_ERROR_NTP);
}
```

- [ ] **Step 5: Build focused policy and ESP32 targets**

Run `cmake --build build/host --target watchy_time_sync_tests` and `/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2`.

Expected: focused test and target link pass. This is a compile check, not the completed milestone gate.

- [ ] **Step 6: Commit Task 4**

```sh
git add components/watchy_shell/include/watchy/time_sync.h components/watchy_shell/src/time_sync_policy.c components/watchy_shell/src/time_sync_idf.c components/watchy_shell/CMakeLists.txt main/main.c tests/host/CMakeLists.txt tests/host/test_time_sync.c
git commit -m "refactor: share NTP synchronization service"
```

---

### Task 5: Authenticated Settings, Wi-Fi, RTC, and NTP APIs

**Files:**
- Modify: `components/watchy_shell/include/watchy/portal.h`
- Modify: `components/watchy_shell/src/portal_policy.c`
- Modify: `components/watchy_shell/src/portal_idf.c`
- Modify: `components/watchy_shell/CMakeLists.txt`
- Modify: `tests/host/CMakeLists.txt`
- Modify: `tests/host/test_portal.c`
- Modify: `tests/python/test_reproducibility.py`

**Interfaces:**
- Consumes: Tasks 2 and 4, settings NVS, RTC HAL, battery/storage/package APIs, and ESP-IDF cJSON.
- Produces PUT method support; route actions `GET_SETTINGS`, `UPDATE_SETTINGS`, `SET_TIME`, `SYNC_NTP`; and these pure policy interfaces:

```c
typedef struct {
    bool has_time_24h;
    bool time_24h;
    bool has_motion_wake;
    bool motion_wake;
    bool has_transition_level;
    watchy_transition_level_t transition_level;
    bool has_partial_refresh_limit;
    uint16_t partial_refresh_limit;
    bool has_timezone_offset;
    int16_t timezone_offset_minutes;
    bool has_ntp_server;
    const char *ntp_server;
} watchy_portal_settings_patch_t;

typedef struct {
    const char *ssid;
    bool password_present;
    const char *password;
} watchy_portal_wifi_patch_t;

bool watchy_portal_apply_settings_patch(
    const watchy_settings_t *current,
    const watchy_portal_settings_patch_t *patch,
    watchy_settings_t *out_settings);
bool watchy_portal_time_from_fields(int year, int month, int day,
                                    int hour, int minute,
                                    int16_t utc_offset_minutes,
                                    watchy_time_t *out_time);
bool watchy_portal_apply_wifi_patch(
    const watchy_settings_t *current,
    const watchy_portal_wifi_patch_t *patch,
    watchy_settings_t *out_settings);
```

- [ ] **Step 1: Write failing route tests**

```c
CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_GET,
                                "/api/v1/settings", &route));
CHECK(route.action == WATCHY_PORTAL_ROUTE_GET_SETTINGS);
CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_PUT,
                                "/api/v1/settings", &route));
CHECK(route.action == WATCHY_PORTAL_ROUTE_UPDATE_SETTINGS);
CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_PUT,
                                "/api/v1/wifi", &route));
CHECK(route.action == WATCHY_PORTAL_ROUTE_PROVISION_WIFI);
CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_PUT,
                                "/api/v1/time", &route));
CHECK(route.action == WATCHY_PORTAL_ROUTE_SET_TIME);
CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                "/api/v1/time/ntp", &route));
CHECK(route.action == WATCHY_PORTAL_ROUTE_SYNC_NTP);
CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                 "/api/v1/settings", &route));
```

- [ ] **Step 2: Write failing candidate-mutation tests**

```c
watchy_settings_t current;
watchy_settings_t result;
watchy_portal_settings_patch_t patch = {
    .has_time_24h = true, .time_24h = false,
    .has_motion_wake = true, .motion_wake = false,
    .has_transition_level = true,
    .transition_level = WATCHY_TRANSITION_LEVEL_REDUCED,
    .has_partial_refresh_limit = true, .partial_refresh_limit = 25u,
    .has_timezone_offset = true, .timezone_offset_minutes = 420,
    .has_ntp_server = true, .ntp_server = "time.cloudflare.com",
};
watchy_settings_defaults(&current);
CHECK(watchy_portal_apply_settings_patch(&current, &patch, &result));
CHECK(!result.time_24h && !result.motion_wake);
CHECK(strcmp(result.timezone, "UTC-7") == 0);
patch.ntp_server = "-invalid";
memset(&result, 0xa5, sizeof(result));
CHECK(!watchy_portal_apply_settings_patch(&current, &patch, &result));
CHECK(memcmp(&current, &result, sizeof(current)) == 0);
```

Add calendar-field tests for leap day, invalid month/day/hour/minute, and year limits.

Add Wi-Fi mutation cases that distinguish omission from an explicit empty password:

```c
watchy_portal_wifi_patch_t keep = {
    .ssid = "Home", .password_present = false, .password = NULL,
};
CHECK(watchy_portal_apply_wifi_patch(&current, &keep, &result));
CHECK(strcmp(result.wifi_password, current.wifi_password) == 0);
keep.ssid = "Different";
CHECK(!watchy_portal_apply_wifi_patch(&current, &keep, &result));
watchy_portal_wifi_patch_t open = {
    .ssid = "Cafe", .password_present = true, .password = "",
};
CHECK(watchy_portal_apply_wifi_patch(&current, &open, &result));
CHECK(result.wifi_password[0] == '\0');
```

Apply a timezone-bearing portal candidate through `watchy_timezone_action_apply` with the same injected fixture used by the on-watch policy tests. Assert RTC-write failure performs no settings save, settings-save failure writes the old offset back, success immediately rereads local time at the new offset, and the resulting settings response fixture uses that reread value.

- [ ] **Step 3: Run portal tests and verify RED**

Run `cmake --build build/host --target watchy_portal_tests && ./build/host/watchy_portal_tests`.

Expected: compile/link failure for PUT routes and mutation helpers.

- [ ] **Step 4: Implement pure routing and candidate policy, then verify GREEN**

Implement route, settings mutation, Wi-Fi mutation, and time construction in policy code. On failure, copy `current` into `out_settings` before returning false so callers cannot persist partial state. `watchy_portal_time_from_fields` receives the offset loaded from current settings and stores it in the constructed `watchy_time_t`. Add `settings_policy.c`, `timezone_action_policy.c`, `rtc_calendar.c`, and `package_format.c` to `watchy_portal_tests` before running the focused target.

- [ ] **Step 5: Add bounded JSON request handling**

Add ESP-IDF `json` to `REQUIRES`. Implement a shared reader that enforces `application/json`, `content_len` 1 through 1024, complete reads, cJSON object root, and unique member names. Reject missing/wrong/duplicate/out-of-range fields with `400 invalid_request`; reject a complete invalid settings candidate with `422 invalid_settings`.

```c
watchy_settings_t current;
watchy_settings_t candidate;
if (watchy_settings_load(&current) != WATCHY_STATUS_OK) return storage_error(request);
if (!parse_settings_patch(request, &patch) ||
    !watchy_portal_apply_settings_patch(&current, &patch, &candidate)) {
    return public_code(request, 422u, "invalid_settings");
}
if (watchy_rtc_read_local(&local) != WATCHY_STATUS_OK ||
    watchy_timezone_action_apply(&current, &candidate, &local,
                                 &portal_timezone_ops) != WATCHY_STATUS_OK) {
    return storage_error(request);
}
return send_settings_json(request, &candidate);
```

`GET settings` includes RTC fields when readable and omits every password field. `PUT settings` must use the same `watchy_timezone_action_apply` transaction as the watch, even when the patch changes other fields at the same time; after success, an immediate GET must return local RTC fields using the new offset. Add IDF adapter wrappers for RTC offset, settings save, and local read rather than duplicating rollback logic. `PUT wifi` parses whether `password` was present, applies `watchy_portal_apply_wifi_patch`, and zeroes JSON/password/settings scratch on all exits. An omitted password preserves credentials only when the submitted SSID is unchanged; a changed SSID requires the member; explicit `""` selects an open network. `PUT time` loads current settings, resolves its fixed offset, passes that offset to `watchy_portal_time_from_fields`, and calls `watchy_rtc_set_local`. `POST time/ntp` calls `watchy_time_sync_connected` only in client mode and refreshes authenticated activity before and after the bounded sync.

Before deleting a Wi-Fi request's cJSON tree, overwrite the password `valuestring` for its exact allocated string length, then clear the fixed password and settings scratch buffers. Extend `GET status` with `esp_app_get_description()` project version/build fields and an `onWatchDiagnostics:true` capability flag; do not run hardware diagnostics from an HTTP request.

- [ ] **Step 6: Add credential-secrecy static assertions**

Extend `test_reproducibility.py` to assert that settings/Wi-Fi response functions do not reference `wifi_password`, HTML symbols do not reference `network_secret` or `token`, the update-settings handler calls `watchy_timezone_action_apply`, and state-changing API handlers remain below the authenticated request-handler boundary.

- [ ] **Step 7: Build focused tests and target adapter**

Run:

```sh
cmake --build build/host --target watchy_portal_tests && ./build/host/watchy_portal_tests
python3 -m unittest tests.python.test_reproducibility
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: policy, secrecy, and ESP32 compilation checks pass.

- [ ] **Step 8: Commit Task 5**

```sh
git add components/watchy_shell/include/watchy/portal.h components/watchy_shell/src/portal_policy.c components/watchy_shell/src/portal_idf.c components/watchy_shell/CMakeLists.txt tests/host/CMakeLists.txt tests/host/test_portal.c tests/python/test_reproducibility.py
git commit -m "feat: expose device settings through portal"
```

---

### Task 6: AP-Only Captive DNS and Probe Redirects

**Files:**
- Create: `components/watchy_shell/include/watchy/captive_portal.h`
- Create: `components/watchy_shell/src/captive_portal_policy.c`
- Create: `components/watchy_shell/src/captive_portal_idf.c`
- Modify: `components/watchy_hal/include/watchy/radios.h`
- Modify: `components/watchy_hal/src/radios.c`
- Modify: `components/watchy_shell/src/portal_idf.c`
- Modify: `components/watchy_shell/CMakeLists.txt`
- Modify: `tests/host/CMakeLists.txt`
- Create: `tests/host/test_captive_portal.c`
- Modify: `tests/host/test_portal.c`

**Interfaces:**
- Consumes: AP address `192.168.4.1`, lwIP sockets, FreeRTOS static task storage, and portal start/stop lifecycle.
- Produces:

```c
bool watchy_captive_dns_build_reply(const uint8_t *query, size_t query_size,
                                    const uint8_t address[4],
                                    uint8_t *reply, size_t reply_capacity,
                                    size_t *out_reply_size);
watchy_status_t watchy_captive_portal_start(const char *address);
watchy_status_t watchy_captive_portal_stop(void);
bool watchy_portal_captive_redirect_path(const char *path);
watchy_status_t watchy_wifi_set_captive_portal_uri(const char *uri);
```

- [ ] **Step 1: Write failing DNS packet tests**

Use a literal one-question `example.com A IN` query and assert transaction ID/flags/question preservation plus a compressed A answer containing `192.168.4.1`. Add rejection cases for truncated labels, input compression pointers, `QDCOUNT != 1`, nonzero opcode, AAAA, non-IN, and output capacity one byte too small.

```c
CHECK(watchy_captive_dns_build_reply(query, sizeof(query), address,
                                     reply, sizeof(reply), &reply_size));
CHECK(reply[0] == query[0] && reply[1] == query[1]);
CHECK(reply[6] == 0u && reply[7] == 1u);
CHECK(memcmp(reply + reply_size - 4u, address, 4u) == 0);
```

- [ ] **Step 2: Write failing captive-path tests**

```c
CHECK(watchy_portal_captive_redirect_path("/generate_204"));
CHECK(watchy_portal_captive_redirect_path("/hotspot-detect.html"));
CHECK(watchy_portal_captive_redirect_path("/connecttest.txt"));
CHECK(watchy_portal_captive_redirect_path("/ncsi.txt"));
CHECK(watchy_portal_captive_redirect_path("/arbitrary-page"));
CHECK(!watchy_portal_captive_redirect_path("/api/v1/status"));
CHECK(!watchy_portal_captive_redirect_path("/api/v1/settings"));
```

- [ ] **Step 3: Configure and run the focused captive target; verify RED**

Register `watchy_captive_portal_tests` with pure policy sources, then run `cmake -S tests/host -B build/host -G Ninja && cmake --build build/host --target watchy_captive_portal_tests && ./build/host/watchy_captive_portal_tests`.

Expected: compile/link failure before the captive interfaces exist.

- [ ] **Step 4: Implement bounded DNS reply construction and verify GREEN**

Parse every label against `query_size`, reject input compression and unsupported query kinds, copy the validated question, and append only this 16-byte answer:

```c
const uint8_t answer[] = {
    0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
    address[0], address[1], address[2], address[3],
};
```

Set response/authoritative bits, preserve RD, set QDCOUNT/ANCOUNT to one, and zero NSCOUNT/ARCOUNT. Run the focused target until green.

- [ ] **Step 5: Implement the AP-only DNS service lifecycle**

Use one UDP socket bound to port 53, receive timeout no greater than 250 ms, fixed packet buffers no greater than 512 bytes, and a statically allocated FreeRTOS task. Stop sets the flag, closes/unblocks the socket, waits for task completion, and releases each resource once. Start DNS only after AP networking succeeds; unwind HTTP/DNS/Wi-Fi in reverse order on partial failure; stop DNS before Wi-Fi.

Expose a narrow radios HAL adapter that requires `WATCHY_WIFI_AP_RUNNING` and calls `esp_netif_dhcps_option(s_wifi_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, ...)` with the static, lifetime-stable string `http://192.168.4.1/`. Treat an unsupported DHCP option as a documented best-effort failure and continue with wildcard DNS and HTTP redirects; failures of the required DNS or HTTP services still trigger reverse-order AP cleanup.

- [ ] **Step 6: Add HTTP probe and unmatched-GET redirects**

Keep the existing central wildcard method handlers. At the top of GET handling, before authentication, redirect only when AP mode is active and `watchy_portal_captive_redirect_path(request->uri)` is true. The helper returns false for `/` and every `/api/v1` path, and true for known probes plus other unmatched non-API GET paths. Probe/unmatched GET responses are:

```http
HTTP/1.1 302 Found
Location: http://192.168.4.1/
Cache-Control: no-store
```

Root and every `/api/v1/*` route retain HTTP Basic authentication. Invalid API paths return authenticated JSON errors rather than redirects. Never add credentials to the redirect URL. The same wildcard router remains active in client mode, but its captive redirect branch is disabled.

- [ ] **Step 7: Build focused and ESP32 targets**

Run:

```sh
cmake --build build/host --target watchy_captive_portal_tests watchy_portal_tests
./build/host/watchy_captive_portal_tests
./build/host/watchy_portal_tests
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: policy tests pass and lwIP/FreeRTOS integration compiles.

- [ ] **Step 8: Commit Task 6**

```sh
git add components/watchy_shell/include/watchy/captive_portal.h components/watchy_shell/src/captive_portal_policy.c components/watchy_shell/src/captive_portal_idf.c components/watchy_hal/include/watchy/radios.h components/watchy_hal/src/radios.c components/watchy_shell/src/portal_idf.c components/watchy_shell/CMakeLists.txt tests/host/CMakeLists.txt tests/host/test_captive_portal.c tests/host/test_portal.c
git commit -m "feat: add captive AP portal discovery"
```

---

### Task 7: Embedded Monochrome Portal UI

**Files:**
- Create: `components/watchy_shell/web/portal.html`
- Modify: `components/watchy_shell/CMakeLists.txt`
- Modify: `components/watchy_shell/src/portal_idf.c`
- Create: `tests/python/test_portal_web.py`
- Create: `tests/js/test_portal_contract.mjs`
- Modify: `README.md`

**Interfaces:**
- Consumes: status, packages, settings, Wi-Fi, time, NTP, activation, removal, and upload APIs.
- Produces: one offline embedded document with Faces, Apps, and Device routes and responsive reference styling.

- [ ] **Step 1: Write failing static web-contract tests**

```python
def test_portal_is_offline_responsive_and_has_only_real_routes(self):
    page = self.page
    self.assertIn("--rail-width:248px", page)
    self.assertIn("@media (max-width:899px)", page)
    self.assertIn("@media (max-width:559px)", page)
    self.assertNotRegex(page, r"https?://(?!192\.168\.4\.1)")
    self.assertNotIn("Firmware update", page)
    self.assertNotIn("Backlight on shake", page)
    for label in ("Faces", "Apps", "Device", "Home timezone", "NTP server",
                  "Wi-Fi network", "Date", "Time", "Motion wake",
                  "Display motion", "Partial refresh limit"):
        self.assertIn(label, page)

def test_untrusted_values_use_text_content(self):
    self.assertNotIn("innerHTML", self.page)
    self.assertIn("textContent", self.page)
    self.assertNotIn("X-Watchy-Token", self.page)
```

The inline script must contain a side-effect-free `wifiRequestPayload(savedSsid, submittedSsid, password, passwordDirty, openNetwork)` between explicit `/* WIFI_PAYLOAD_BEGIN */` / `/* WIFI_PAYLOAD_END */` markers. The Node test reads the HTML, extracts/evaluates only that marked function, and asserts:

```js
assert.deepEqual(wifiRequestPayload('Home', 'Home', '', false, false), {ssid:'Home'});
assert.deepEqual(wifiRequestPayload('Home', 'Other', '', false, false), {ssid:'Other'});
assert.deepEqual(wifiRequestPayload('Home', 'Other', 'secret123', true, false),
                 {ssid:'Other', password:'secret123'});
assert.deepEqual(wifiRequestPayload('Home', 'Cafe', '', false, true),
                 {ssid:'Cafe', password:''});
```

- [ ] **Step 2: Run the static test and verify RED**

Run `python3 -m unittest tests.python.test_portal_web` and `node --test tests/js/test_portal_contract.mjs`.

Expected: failure because `components/watchy_shell/web/portal.html` does not exist.

- [ ] **Step 3: Build semantic HTML and reference-aligned CSS**

Use a 248 px desktop rail, `38px 44px 64px` main padding, two responsive breakpoints, `#000`, `#fff`, `#f4f3f1`, `#e7e5e1`, and `#6f6b64`, one-pixel rules, square buttons/switches, and inversion for active/selected state.

```css
:root { --rail-width:248px; --ink:#000; --paper:#fff; --wash:#f4f3f1; --muted:#6f6b64; }
body { margin:0; color:var(--ink); background:var(--paper);
       font-family:"Helvetica Neue",Helvetica,Arial,sans-serif; }
.mono { font-family:"IBM Plex Mono",ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }
@media (max-width:899px) { .shell { flex-direction:column; } .rail { width:100%; } }
@media (max-width:559px) { main { padding:26px 18px 56px; } .grid { grid-template-columns:1fr; } }
```

Use no external font requests. Provide semantic labels/buttons, visible focus outlines, and status text in addition to inversion.

- [ ] **Step 4: Implement real API-driven interactions**

Fetch status, packages, and settings once on load; refresh only after user actions. Split packages by type. Upload with `application/octet-stream`; activate/remove with existing routes. Device Save submits non-secret settings. Track whether the Wi-Fi password input has been edited: omit `password` when an unchanged SSID is saved with an untouched field, include the entered value when dirty, and include `password:""` only when the user explicitly enables a square **Open network / clear password** control. Blank the field and clear its dirty state after success. Set Time sends calendar fields, and NTP Sync is disabled with explanatory text in AP mode.

Render errors through `textContent` in `aria-live="polite"`. Update controls only from successful responses. Do not add an API polling timer.

- [ ] **Step 5: Embed and serve the page**

Add `EMBED_TXTFILES "web/portal.html"` to `idf_component_register`. Replace C page strings with linker symbols:

```c
extern const uint8_t portal_html_start[] asm("_binary_web_portal_html_start");
extern const uint8_t portal_html_end[] asm("_binary_web_portal_html_end");

return httpd_resp_send(request, (const char *)portal_html_start,
                       (ssize_t)(portal_html_end - portal_html_start));
```

- [ ] **Step 6: Run static, secrecy, and target checks**

Run:

```sh
python3 -m unittest tests.python.test_portal_web tests.python.test_reproducibility
node --test tests/js/test_portal_contract.mjs
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Expected: web/static checks pass and the embedded page links within the app partition.

- [ ] **Step 7: Perform local visual QA at desktop and phone widths**

Serve the page with a temporary mock API outside the repository. Inspect 1280×800 and 390×844 screenshots. Confirm rail-to-top-nav conversion, single-column tiny layout, unclipped type, visible focus, square controls, and no horizontal overflow. With a mock secured network, manually confirm the visible result behavior for unchanged SSID, changed SSID without a new password, typed password success, and explicit Open network; the exact request bodies remain enforced by `test_portal_contract.mjs`. Apply required corrections and rerun Task 7 checks.

- [ ] **Step 8: Update documentation and commit Task 7**

Document captive launch, username `watchy`, 32-character watch-displayed credential, 8-character AP password, five-minute idle/30-minute absolute lifetime, Saved Wi-Fi → reopen Client Portal → NTP flow, timezone behavior, and direct-IP fallback.

```sh
git add components/watchy_shell/web/portal.html components/watchy_shell/CMakeLists.txt components/watchy_shell/src/portal_idf.c tests/python/test_portal_web.py tests/js/test_portal_contract.mjs README.md
git commit -m "feat: redesign Watchy management portal"
```

---

### Task 8: Integration Review, Milestone Gate, and Device Flash

**Files:**
- Modify if findings require correction: files touched in Tasks 1-7.
- Modify: `docs/hardware-acceptance.md`

**Interfaces:**
- Consumes: all task commits and the approved design.
- Produces: reviewed clean milestone commit, complete verification evidence, and firmware-only serial flash preserving NVS/LittleFS.

- [ ] **Step 1: Update and commit the hardware acceptance checklist**

Add unchecked UAT rows for captive detection, direct-IP fallback, password/session lengths, idle/absolute expiry, settings secrecy/persistence, saved-Wi-Fi client mode, NTP timezone result, manual date/time, and continuous list scrolling.

```sh
git add docs/hardware-acceptance.md
git commit -m "docs: add portal and scrolling acceptance checks"
```

- [ ] **Step 2: Request a read-only code review**

Review the complete range after `da94098` against the spec. Require Critical/Important/Minor findings and inspect DNS bounds/lifecycle, auth fallthrough, password zeroing, JSON limits/types, NVS atomicity, radio ownership, navigation stack bounds, package rollback, and display responsiveness. Do not run the complete suite during review.

- [ ] **Step 3: Address review findings with focused RED/GREEN cycles**

For each Critical or Important finding, first add a focused regression test that fails for the reported behavior, implement the smallest correction, rerun only the affected target, and commit the fix. Record any deferred cosmetic Minor item in the handoff.

- [ ] **Step 4: Run the single milestone host gate**

```sh
/Users/maxb/.platformio/packages/tool-cmake/bin/cmake --build build/host -j8
/Users/maxb/.platformio/packages/tool-cmake/bin/ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tests/python
node --test tests/js/test_portal_contract.mjs
```

Expected: every host target, all existing and newly registered CTest suites, and every Python static/tooling test pass. Report the actual suite count.

- [ ] **Step 5: Build and validate the exact ESP32 artifact**

```sh
git diff --check
git status --short
git rev-parse --short HEAD
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2
```

Require empty `git status --short` before building so ESP-IDF's automatic Git-derived `PROJECT_VER` is not suffixed `-dirty`. Then run `strings .pio/build/watchy_v2/firmware.elf` and confirm it contains a line exactly equal to the short commit printed by `git rev-parse`. This project metadata mechanism already produced the exact prior milestone revision; if it does not produce the current exact revision after a clean rebuild, fail the gate and do not flash. Expected: build fits the 1.75 MiB app partition, diff is clean, worktree has no uncommitted changes, and the ELF identifies the exact commit.

- [ ] **Step 6: Flash firmware only after the gate passes**

Resolve the USB serial port with read-only PlatformIO device discovery, then run:

```sh
/Users/maxb/.platformio/penv/bin/platformio run -e watchy_v2 -t upload --upload-port /dev/cu.usbserial-5B0B0425311
```

Use the discovered explicit port if it differs. Confirm bootloader, partition table, and app hashes verify. Do not erase or write NVS/LittleFS.

- [ ] **Step 7: Hand off hardware UAT**

Give the user the nine acceptance steps from the spec, emphasizing captive presentation, Basic authentication, Wi-Fi save, client-mode reopen, NTP sync, timezone/manual local time on Hairline and Grid faces, five-minute expiry, and continuous scrolling with Back restoration.

- [ ] **Step 8: Record the flashed commit and remaining physical checks**

Report the exact flashed commit, actual passing test counts, RAM/flash use, whether NVS/LittleFS were preserved, and the still-unchecked physical acceptance rows. Do not mark hardware-only checks complete from host evidence.
