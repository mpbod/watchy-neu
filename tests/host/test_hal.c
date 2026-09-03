#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "watchy/battery.h"
#include "watchy/board.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/display_policy.h"
#include "watchy/power.h"
#include "watchy/radios.h"
#include "watchy/rtc_calendar.h"
#include "watchy/storage.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static bool pixel_is_black(const uint8_t *framebuffer, int x, int y) {
    const size_t offset = (size_t)y * WATCHY_DISPLAY_STRIDE + (size_t)x / 8u;
    const uint8_t bit = (uint8_t)(0x80u >> ((unsigned)x & 7u));
    return (framebuffer[offset] & bit) == 0u;
}

static int test_framebuffer_clips_pixels_and_rectangles(void) {
    uint8_t framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_framebuffer_fill(framebuffer, sizeof(framebuffer), false);

    watchy_framebuffer_draw_pixel(framebuffer, sizeof(framebuffer), -1, 10, true);
    watchy_framebuffer_draw_pixel(framebuffer, sizeof(framebuffer), 200, 10, true);
    watchy_framebuffer_draw_pixel(framebuffer, sizeof(framebuffer), 199, 199, true);
    watchy_framebuffer_fill_rect(framebuffer, sizeof(framebuffer), -2, -1, 4, 3, true);

    CHECK(pixel_is_black(framebuffer, 199, 199));
    CHECK(pixel_is_black(framebuffer, 0, 0));
    CHECK(pixel_is_black(framebuffer, 1, 0));
    CHECK(pixel_is_black(framebuffer, 0, 1));
    CHECK(pixel_is_black(framebuffer, 1, 1));
    CHECK(!pixel_is_black(framebuffer, 2, 0));
    CHECK(!pixel_is_black(framebuffer, 0, 2));
    CHECK(!pixel_is_black(framebuffer, 198, 199));
    return 0;
}

static int test_button_wake_gpio_decodes_semantic_mask(void) {
    const uint64_t raw = (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                         (UINT64_C(1) << WATCHY_PIN_BUTTON_UP) |
                         (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1);
    CHECK(watchy_buttons_decode_wake_gpio(raw) ==
          (WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP));
    CHECK(watchy_buttons_decode_wake_gpio(0) == 0u);
    return 0;
}

static int test_safe_mode_requires_back_and_down_held_together(void) {
    CHECK(watchy_buttons_is_safe_mode_chord(WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_DOWN));
    CHECK(watchy_buttons_is_safe_mode_chord(WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_DOWN |
                                             WATCHY_BUTTON_MASK_MENU));
    CHECK(!watchy_buttons_is_safe_mode_chord(WATCHY_BUTTON_MASK_BACK));
    CHECK(!watchy_buttons_is_safe_mode_chord(WATCHY_BUTTON_MASK_DOWN));
    CHECK(!watchy_buttons_is_safe_mode_chord(0));
    return 0;
}

static int test_button_filter_debounces_each_semantic_edge_once(void) {
    watchy_button_filter_t filter;

    /* Initial held Menu is baseline and emits nothing. */
    watchy_buttons_filter_init(&filter, WATCHY_BUTTON_MASK_MENU, 30u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 100u) == 0u);

    /* A new level does not emit until it remains stable for 30 ms. */
    watchy_buttons_filter_init(&filter, 0u, 30u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 100u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 129u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 130u) ==
          WATCHY_BUTTON_MASK_DOWN);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 1000u) == 0u);

    /* A release is silent, and a stable re-press emits once. */
    CHECK(watchy_buttons_filter_observe(&filter, 0u, 1100u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, 0u, 1129u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, 0u, 1130u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 1200u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 1230u) ==
          WATCHY_BUTTON_MASK_DOWN);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_DOWN, 1300u) == 0u);

    /* A bounce resets the candidate interval. */
    watchy_buttons_filter_init(&filter, 0u, 30u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 100u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 120u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, 0u, 125u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 140u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 169u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 170u) ==
          WATCHY_BUTTON_MASK_MENU);

    /* Independent buttons settle at different timestamps. */
    watchy_buttons_filter_init(&filter, 0u, 30u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 100u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_DOWN,
                                        110u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_DOWN,
                                        129u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_DOWN,
                                        130u) == WATCHY_BUTTON_MASK_MENU);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_DOWN,
                                        139u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_DOWN,
                                        140u) == WATCHY_BUTTON_MASK_DOWN);

    /* Simultaneous presses settle and emit together. */
    watchy_buttons_filter_init(&filter, 0u, 30u);
    CHECK(watchy_buttons_filter_observe(&filter,
                                       WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP,
                                       500u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter,
                                       WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP,
                                       529u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter,
                                       WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP,
                                       530u) ==
          (WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP));
    CHECK(watchy_buttons_filter_observe(&filter,
                                       WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_UP,
                                       1000u) == 0u);

    /* Unsigned timestamp subtraction handles wraparound. */
    watchy_buttons_filter_init(&filter, 0u, 30u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_UP,
                                        UINT32_MAX - 9u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_UP, 19u) == 0u);
    CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_UP, 20u) ==
          WATCHY_BUTTON_MASK_UP);

    /* Null filters and zero intervals are no-op, no-event calls. */
    CHECK(watchy_buttons_filter_observe(NULL, WATCHY_BUTTON_MASK_MENU, 0u) == 0u);
    memset(&filter, 0x5a, sizeof(filter));
    filter.debounce_ms = 0u;
    {
        const watchy_button_filter_t before = filter;
        CHECK(watchy_buttons_filter_observe(&filter, WATCHY_BUTTON_MASK_MENU, 100u) == 0u);
        CHECK(memcmp(&filter, &before, sizeof(filter)) == 0);
    }
    return 0;
}

static int test_battery_percentage_and_install_boundaries_are_conservative(void) {
    CHECK(watchy_battery_percent_from_mv(3300) == 0u);
    CHECK(watchy_battery_percent_from_mv(3400) == 0u);
    CHECK(watchy_battery_percent_from_mv(3500) == 5u);
    CHECK(watchy_battery_percent_from_mv(3550) == 10u);
    CHECK(watchy_battery_percent_from_mv(3700) == 30u);
    CHECK(watchy_battery_percent_from_mv(3900) == 70u);
    CHECK(watchy_battery_percent_from_mv(4150) == 100u);
    CHECK(watchy_battery_percent_from_mv(4300) == 100u);
    CHECK(!watchy_battery_is_install_safe(3549));
    CHECK(watchy_battery_is_install_safe(3550));
    return 0;
}

static int test_wake_cause_mapping_distinguishes_hardware_sources(void) {
    const uint64_t back = UINT64_C(1) << WATCHY_PIN_BUTTON_BACK;
    const uint64_t motion = UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1;

    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_UNDEFINED, 0) == WATCHY_WAKE_COLD);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT0, 0) == WATCHY_WAKE_RTC);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, back) == WATCHY_WAKE_BUTTON);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, motion) == WATCHY_WAKE_MOTION);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1,
                                UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_2) ==
          WATCHY_WAKE_OTHER);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, back | motion) == WATCHY_WAKE_BUTTON);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, 0) == WATCHY_WAKE_OTHER);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_TIMER, 0) == WATCHY_WAKE_TIMER);
    CHECK(watchy_power_safe_mode_chord_allowed(WATCHY_WAKE_COLD));
    CHECK(watchy_power_safe_mode_chord_allowed(WATCHY_WAKE_OTHER));
    CHECK(!watchy_power_safe_mode_chord_allowed(WATCHY_WAKE_RTC));
    CHECK(!watchy_power_safe_mode_chord_allowed(WATCHY_WAKE_TIMER));
    return 0;
}

static int test_ssd1681_busy_is_active_high_and_requires_settling(void) {
    watchy_display_busy_filter_t filter = {0};

    CHECK(!watchy_display_busy_observe(&filter, true));
    CHECK(!watchy_display_busy_observe(&filter, true));
    CHECK(!watchy_display_busy_observe(&filter, false));
    CHECK(watchy_display_busy_observe(&filter, false));
    CHECK(!watchy_display_busy_observe(&filter, true));
    return 0;
}

static int test_display_retained_state_controls_boot_refresh_and_commits_only_on_success(void) {
    watchy_display_retained_state_t retained = {0};
    uint8_t old_frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    uint8_t new_frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];

    memset(old_frame, 0xa5, sizeof(old_frame));
    memset(new_frame, 0x5a, sizeof(new_frame));
    CHECK(!watchy_display_retained_valid(&retained));
    CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20) ==
          WATCHY_REFRESH_FULL);

    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_FULL, old_frame, sizeof(old_frame));
    CHECK(watchy_display_retained_valid(&retained));
    CHECK(memcmp(retained.previous_frame, old_frame, sizeof(old_frame)) == 0);
    CHECK(retained.partial_count == 0u);
    CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20) ==
          WATCHY_REFRESH_PARTIAL);

    /* A failed hardware refresh performs no commit, preserving both cadence and old RAM. */
    CHECK(retained.partial_count == 0u);
    CHECK(memcmp(retained.previous_frame, old_frame, sizeof(old_frame)) == 0);
    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_PARTIAL, new_frame, sizeof(new_frame));
    CHECK(retained.partial_count == 1u);
    CHECK(memcmp(retained.previous_frame, new_frame, sizeof(new_frame)) == 0);

    retained.checksum ^= 1u;
    CHECK(!watchy_display_retained_valid(&retained));
    CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20) ==
          WATCHY_REFRESH_FULL);
    return 0;
}

static watchy_display_retained_state_t valid_retained_state(uint16_t partial_count) {
    static uint8_t frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained = {0};

    memset(frame, 0x11, sizeof(frame));
    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_FULL, frame, sizeof(frame));
    for (uint16_t write = 0u; write < partial_count; ++write) {
        watchy_display_commit_refresh(&retained, WATCHY_REFRESH_PARTIAL, frame, sizeof(frame));
    }
    return retained;
}

static int test_display_refresh_policy_promotes_and_counts_each_physical_write(void) {
    static uint8_t frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained = valid_retained_state(18u);

    memset(frame, 0x22, sizeof(frame));
    CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20u) ==
          WATCHY_REFRESH_PARTIAL);
    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_PARTIAL, frame, sizeof(frame));
    CHECK(retained.partial_count == 19u);
    CHECK(memcmp(retained.previous_frame, frame, sizeof(frame)) == 0);

    memset(frame, 0x33, sizeof(frame));
    CHECK(watchy_display_prepare_refresh(&retained, WATCHY_REFRESH_PARTIAL, 20u) ==
          WATCHY_REFRESH_FULL);
    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_FULL, frame, sizeof(frame));
    CHECK(retained.partial_count == 0u);
    CHECK(memcmp(retained.previous_frame, frame, sizeof(frame)) == 0);
    return 0;
}

static watchy_status_t simulate_transition_writes(watchy_display_retained_state_t *retained,
                                                   size_t write_count,
                                                   size_t fail_at) {
    static uint8_t frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];

    for (size_t write = 0u; write < write_count; ++write) {
        watchy_refresh_mode_t completed;

        memset(frame, (int)(0x40u + write), sizeof(frame));
        if (write == fail_at) {
            watchy_display_invalidate_retained(retained);
            return WATCHY_STATUS_INVALID_STATE;
        }
        completed = watchy_display_prepare_refresh(retained, WATCHY_REFRESH_PARTIAL, 20u);
        watchy_display_commit_refresh(retained, completed, frame, sizeof(frame));
    }
    return WATCHY_STATUS_OK;
}

static int test_display_multi_write_tracks_each_frame_and_invalidates_on_failure(void) {
    static uint8_t expected_frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained = valid_retained_state(18u);

    memset(expected_frame, 0x42, sizeof(expected_frame));
    CHECK(simulate_transition_writes(&retained, 3u, SIZE_MAX) == WATCHY_STATUS_OK);
    CHECK(watchy_display_retained_valid(&retained));
    CHECK(retained.partial_count == 1u);
    CHECK(memcmp(retained.previous_frame, expected_frame, sizeof(expected_frame)) == 0);

    retained = valid_retained_state(0u);
    memset(expected_frame, 0x40, sizeof(expected_frame));
    CHECK(simulate_transition_writes(&retained, 3u, 1u) == WATCHY_STATUS_INVALID_STATE);
    CHECK(!watchy_display_retained_valid(&retained));
    CHECK(memcmp(retained.previous_frame, expected_frame, sizeof(expected_frame)) == 0);
    return 0;
}

typedef struct {
    char events[16];
    size_t event_count;
    size_t write_calls;
    size_t feed_calls;
    size_t fail_at;
    size_t cancel_after;
    watchy_refresh_mode_t modes[5];
    bool sampled_black[5];
    uint8_t *last_successful_frame;
} display_transition_fake_t;

static void record_display_event(display_transition_fake_t *fake, char event) {
    if (fake->event_count < sizeof(fake->events)) {
        fake->events[fake->event_count++] = event;
    }
}

static watchy_status_t fake_physical_write(void *context,
                                           const uint8_t *frame,
                                           watchy_refresh_mode_t mode) {
    display_transition_fake_t *fake = (display_transition_fake_t *)context;
    const size_t write = fake->write_calls++;

    record_display_event(fake, 'W');
    fake->modes[write] = mode;
    fake->sampled_black[write] = pixel_is_black(frame, 150, 100);
    if (write == fake->fail_at) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    memcpy(fake->last_successful_frame, frame, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    return WATCHY_STATUS_OK;
}

static bool fake_transition_cancel(void *context) {
    display_transition_fake_t *fake = (display_transition_fake_t *)context;

    record_display_event(fake, 'C');
    return fake->write_calls >= fake->cancel_after;
}

static void fake_transition_feed(void *context) {
    display_transition_fake_t *fake = (display_transition_fake_t *)context;

    record_display_event(fake, 'F');
    ++fake->feed_calls;
}

static watchy_transition_plan_t push_right_plan(void) {
    return (watchy_transition_plan_t){
        .effect = WATCHY_TRANSITION_PUSH,
        .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
        .rect = {0, 0, WATCHY_DISPLAY_WIDTH, WATCHY_DISPLAY_HEIGHT},
        .write_count = 2u,
    };
}

static watchy_transition_plan_t plan_for_write_count(uint8_t write_count) {
    static const watchy_transition_effect_t effects[] = {
        WATCHY_TRANSITION_CUT,
        WATCHY_TRANSITION_CUT,
        WATCHY_TRANSITION_FLASH,
        WATCHY_TRANSITION_PUSH,
        WATCHY_TRANSITION_WIPE,
        WATCHY_TRANSITION_SHUTTER,
    };
    return (watchy_transition_plan_t){
        .effect = effects[write_count],
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {0, 0, WATCHY_DISPLAY_WIDTH, WATCHY_DISPLAY_HEIGHT},
        .write_count = write_count,
    };
}

static int test_display_forecasts_every_optional_write_and_actual_target_mode(void) {
    for (uint8_t write_count = 1u; write_count <= 5u; ++write_count) {
        const watchy_transition_plan_t plan = plan_for_write_count(write_count);
        watchy_display_retained_state_t retained =
            valid_retained_state((uint16_t)(20u - write_count - 1u));

        CHECK(!watchy_display_plan_requires_clear(&retained, &plan,
                                                  WATCHY_REFRESH_PARTIAL, 20u));
        retained = valid_retained_state((uint16_t)(20u - write_count));
        CHECK(watchy_display_plan_requires_clear(&retained, &plan,
                                                 WATCHY_REFRESH_PARTIAL, 20u));

        if (write_count == 1u) {
            retained = valid_retained_state(19u);
            CHECK(!watchy_display_plan_requires_clear(&retained, &plan,
                                                      WATCHY_REFRESH_FULL, 20u));
        } else {
            retained = valid_retained_state((uint16_t)(20u - write_count));
            CHECK(!watchy_display_plan_requires_clear(&retained, &plan,
                                                      WATCHY_REFRESH_FULL, 20u));
            retained = valid_retained_state((uint16_t)(21u - write_count));
            CHECK(watchy_display_plan_requires_clear(&retained, &plan,
                                                     WATCHY_REFRESH_FULL, 20u));
        }
    }
    return 0;
}

static watchy_display_retained_state_t retained_with_frame(const uint8_t *frame,
                                                           uint16_t partial_count) {
    watchy_display_retained_state_t retained = {0};

    watchy_display_commit_refresh(&retained, WATCHY_REFRESH_FULL, frame,
                                  WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    for (uint16_t write = 0u; write < partial_count; ++write) {
        watchy_display_commit_refresh(&retained, WATCHY_REFRESH_PARTIAL, frame,
                                      WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    }
    return retained;
}

static int test_display_transition_adapter_clears_before_plan_wide_threshold(void) {
    static uint8_t original_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t fallback_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t source_snapshot[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t target[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t scratch[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t last_successful[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained;
    watchy_transition_plan_t plan = push_right_plan();
    watchy_transition_result_t result;
    display_transition_fake_t fake = {
        .fail_at = SIZE_MAX,
        .cancel_after = SIZE_MAX,
        .last_successful_frame = last_successful,
    };
    watchy_display_transition_io_t io = {
        .retained = &retained,
        .partial_limit = 20u,
        .target_requested = WATCHY_REFRESH_PARTIAL,
        .physical_write = fake_physical_write,
        .cancel = fake_transition_cancel,
        .feed = fake_transition_feed,
        .context = &fake,
    };

    memset(original_source, 0xff, sizeof(original_source));
    memset(fallback_source, 0x55, sizeof(fallback_source));
    memset(target, 0x00, sizeof(target));
    retained = retained_with_frame(original_source, 18u);

    CHECK(watchy_display_execute_plan(&plan, fallback_source, target, source_snapshot, scratch,
                                      sizeof(scratch), &io, &result) == WATCHY_STATUS_OK);
    CHECK(result.completed && result.writes_completed == 2u);
    CHECK(fake.write_calls == 2u && fake.feed_calls == 2u);
    CHECK(fake.event_count == 4u && memcmp(fake.events, "WFWF", 4u) == 0);
    CHECK(fake.modes[0] == WATCHY_REFRESH_FULL);
    CHECK(fake.modes[1] == WATCHY_REFRESH_FULL);
    CHECK(!fake.sampled_black[0] && fake.sampled_black[1]);
    CHECK(memcmp(source_snapshot, original_source, sizeof(original_source)) == 0);
    CHECK(watchy_display_retained_valid(&retained));
    CHECK(retained.partial_count == 0u);
    CHECK(memcmp(retained.previous_frame, target, sizeof(target)) == 0);
    return 0;
}

static int test_display_cancellation_maps_to_distinct_status_and_button_edges(void) {
    watchy_transition_result_t result = {
        .writes_completed = 1u,
        .cancelled = true,
        .source_valid = true,
    };

    CHECK(watchy_display_execution_status(WATCHY_STATUS_OK, &result, false) ==
          WATCHY_STATUS_CANCELLED);
    CHECK(watchy_display_execution_status(WATCHY_STATUS_OK, &result, true) ==
          WATCHY_STATUS_INVALID_STATE);
    result.cancelled = false;
    result.completed = true;
    result.last_frame_is_target = true;
    CHECK(watchy_display_execution_status(WATCHY_STATUS_OK, &result, false) ==
          WATCHY_STATUS_OK);
    CHECK(watchy_display_new_button_mask(WATCHY_BUTTON_MASK_MENU,
                                         WATCHY_BUTTON_MASK_MENU |
                                             WATCHY_BUTTON_MASK_DOWN) ==
          WATCHY_BUTTON_MASK_DOWN);
    return 0;
}

static int test_display_transition_adapter_cancellation_retains_last_successful_frame(void) {
    static uint8_t original_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t fallback_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t source_snapshot[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t target[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t scratch[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t last_successful[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained;
    watchy_transition_plan_t plan = push_right_plan();
    watchy_transition_result_t result;
    display_transition_fake_t fake = {
        .fail_at = SIZE_MAX,
        .cancel_after = 1u,
        .last_successful_frame = last_successful,
    };
    watchy_display_transition_io_t io = {
        .retained = &retained,
        .partial_limit = 20u,
        .target_requested = WATCHY_REFRESH_PARTIAL,
        .physical_write = fake_physical_write,
        .cancel = fake_transition_cancel,
        .feed = fake_transition_feed,
        .context = &fake,
    };

    memset(original_source, 0xff, sizeof(original_source));
    memset(fallback_source, 0x55, sizeof(fallback_source));
    memset(target, 0x00, sizeof(target));
    retained = retained_with_frame(original_source, 0u);

    CHECK(watchy_display_execute_plan(&plan, fallback_source, target, source_snapshot, scratch,
                                      sizeof(scratch), &io, &result) == WATCHY_STATUS_OK);
    CHECK(result.cancelled && !result.completed && result.writes_completed == 1u);
    CHECK(fake.write_calls == 1u && fake.feed_calls == 1u);
    CHECK(fake.event_count == 3u && memcmp(fake.events, "WFC", 3u) == 0);
    CHECK(watchy_display_retained_valid(&retained));
    CHECK(retained.partial_count == 1u);
    CHECK(memcmp(retained.previous_frame, last_successful, sizeof(last_successful)) == 0);
    return 0;
}

static int test_display_transition_adapter_write_failure_invalidates_retained_source(void) {
    static uint8_t original_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t fallback_source[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t source_snapshot[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t target[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t scratch[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static uint8_t last_successful[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_display_retained_state_t retained;
    watchy_transition_plan_t plan = push_right_plan();
    watchy_transition_result_t result;
    display_transition_fake_t fake = {
        .fail_at = 1u,
        .cancel_after = SIZE_MAX,
        .last_successful_frame = last_successful,
    };
    watchy_display_transition_io_t io = {
        .retained = &retained,
        .partial_limit = 20u,
        .target_requested = WATCHY_REFRESH_PARTIAL,
        .physical_write = fake_physical_write,
        .cancel = fake_transition_cancel,
        .feed = fake_transition_feed,
        .context = &fake,
    };

    memset(original_source, 0xff, sizeof(original_source));
    memset(fallback_source, 0x55, sizeof(fallback_source));
    memset(target, 0x00, sizeof(target));
    retained = retained_with_frame(original_source, 0u);

    CHECK(watchy_display_execute_plan(&plan, fallback_source, target, source_snapshot, scratch,
                                      sizeof(scratch), &io, &result) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_WRITE);
    CHECK(result.writes_completed == 1u);
    CHECK(fake.write_calls == 2u && fake.feed_calls == 1u);
    CHECK(fake.event_count == 4u && memcmp(fake.events, "WFCW", 4u) == 0);
    CHECK(!watchy_display_retained_valid(&retained));
    CHECK(memcmp(retained.previous_frame, last_successful, sizeof(last_successful)) == 0);
    return 0;
}

static int test_pcf8563_calendar_validates_bcd_dates_century_and_unix_offsets(void) {
    uint8_t registers[7] = {0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
    watchy_time_t time;
    watchy_time_t local;
    int64_t unix_seconds;

    CHECK(watchy_pcf8563_decode(registers, &time) == WATCHY_STATUS_OK);
    CHECK(time.year == 2024 && time.month == 2 && time.day == 29);
    registers[5] = 0x82;
    registers[6] = 0x99;
    registers[3] = 0x28;
    CHECK(watchy_pcf8563_decode(registers, &time) == WATCHY_STATUS_OK);
    CHECK(time.year == 1999);
    CHECK(watchy_pcf8563_encode(&time, registers) == WATCHY_STATUS_OK);
    CHECK((registers[5] & 0x80u) != 0u);
    time.year = 2024;
    CHECK(watchy_pcf8563_encode(&time, registers) == WATCHY_STATUS_OK);
    CHECK((registers[5] & 0x80u) == 0u);

    registers[0] = 0x6a;
    CHECK(watchy_pcf8563_decode(registers, &time) == WATCHY_STATUS_INVALID_STATE);
    registers[0] = 0x56;
    registers[3] = 0x29;
    registers[5] = 0x02;
    registers[6] = 0x23;
    CHECK(watchy_pcf8563_decode(registers, &time) == WATCHY_STATUS_INVALID_STATE);
    registers[3] = 0x31;
    registers[5] = 0x04;
    registers[6] = 0x24;
    CHECK(watchy_pcf8563_decode(registers, &time) == WATCHY_STATUS_INVALID_STATE);

    time = (watchy_time_t){.year = 1970, .month = 1, .day = 1, .weekday = 4};
    CHECK(watchy_calendar_to_unix(&time, &unix_seconds) == WATCHY_STATUS_OK);
    CHECK(unix_seconds == 0);
    CHECK(watchy_calendar_from_unix(1704067200, 420, &local) == WATCHY_STATUS_OK);
    CHECK(local.year == 2024 && local.month == 1 && local.day == 1 &&
          local.hour == 7 && local.minute == 0 && local.utc_offset_minutes == 420);
    return 0;
}

static int test_pcf8563_alarm_encodes_documented_next_match_fields(void) {
    uint8_t alarm[4] = {0};
    watchy_time_t time = {
        .year = 2037, .month = 11, .day = 31, .hour = 23, .minute = 45,
        .second = 59, .weekday = 2, .utc_offset_minutes = 420,
    };
    CHECK(watchy_pcf8563_alarm_encode(&time, alarm) == WATCHY_STATUS_OK);
    CHECK(alarm[0] == 0x45u);
    CHECK(alarm[1] == 0x23u);
    CHECK(alarm[2] == 0x31u);
    CHECK(alarm[3] == 0x02u);
    time.weekday = 7u;
    CHECK(watchy_pcf8563_alarm_encode(&time, alarm) == WATCHY_STATUS_INVALID_ARGUMENT);
    return 0;
}

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

static int test_sleep_admission_and_wake_source_debounce_are_fail_closed(void) {
    watchy_sleep_requirements_t requirements = valid_timer_sleep_requirements();
    watchy_wake_source_filter_t filter = {0};

    CHECK(watchy_power_sleep_allowed(&requirements));
    requirements.motion_wake_enabled = false;
    requirements.motion_source_configured = false;
    CHECK(watchy_power_sleep_allowed(&requirements));
    requirements = valid_timer_sleep_requirements();
    requirements.buttons_quiesced = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements = valid_timer_sleep_requirements();
    requirements.timer_configured = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.timer_configured = true;
    requirements.radios_stopped = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.radios_stopped = true;
    requirements.motor_off = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.motor_off = true;
    requirements.display_hibernated = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.display_hibernated = true;
    requirements.rtc_source_cleared = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.rtc_source_cleared = true;
    requirements.motion_source_configured = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.motion_source_configured = true;
    requirements.ext0_configured = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.ext0_configured = true;
    requirements.ext1_configured = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));
    requirements.ext1_configured = true;
    requirements.sources_inactive = false;
    CHECK(!watchy_power_sleep_allowed(&requirements));

    requirements = (watchy_sleep_requirements_t){
        .radios_stopped = true,
        .motor_off = true,
        .display_hibernated = true,
        .buttons_quiesced = true,
        .ext1_configured = true,
        .sources_inactive = true,
        .button_only = true,
    };
    CHECK(watchy_power_sleep_allowed(&requirements));
    requirements.ext0_configured = true;
    CHECK(!watchy_power_sleep_allowed(&requirements));

    CHECK(!watchy_power_wake_sources_observe(&filter, UINT64_C(1) << WATCHY_PIN_BUTTON_BACK));
    CHECK(!watchy_power_wake_sources_observe(&filter, 0));
    CHECK(!watchy_power_wake_sources_observe(&filter, 0));
    CHECK(watchy_power_wake_sources_observe(&filter, 0));
    CHECK(!watchy_power_wake_sources_observe(&filter, UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1));
    return 0;
}

static int test_motor_pin_is_retained_low_instead_of_generically_released(void) {
    CHECK(watchy_power_button_needs_internal_pulldown(WATCHY_PIN_BUTTON_MENU));
    CHECK(watchy_power_button_needs_internal_pulldown(WATCHY_PIN_BUTTON_BACK));
    CHECK(watchy_power_button_needs_internal_pulldown(WATCHY_PIN_BUTTON_DOWN));
    CHECK(!watchy_power_button_needs_internal_pulldown(WATCHY_PIN_BUTTON_UP));
    CHECK(!watchy_power_release_pin_for_sleep(WATCHY_PIN_MOTOR));
    CHECK(watchy_power_release_pin_for_sleep(WATCHY_PIN_DISPLAY_BUSY));
    CHECK(!watchy_power_release_pin_for_sleep(WATCHY_PIN_BATTERY_ADC));
    return 0;
}

static int test_rtc_initial_clock_only_becomes_ready_after_valid_decode(void) {
    uint8_t registers[7] = {0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
    CHECK(watchy_rtc_initial_clock_ready(registers));
    registers[0] |= 0x80u;
    CHECK(!watchy_rtc_initial_clock_ready(registers));
    registers[0] = 0x6au;
    CHECK(!watchy_rtc_initial_clock_ready(registers));
    return 0;
}

static int test_radio_reconnect_is_blocked_while_stopping(void) {
    CHECK(watchy_wifi_should_reconnect(WATCHY_WIFI_STA_STARTING));
    CHECK(watchy_wifi_should_reconnect(WATCHY_WIFI_STA_CONNECTED));
    CHECK(!watchy_wifi_should_reconnect(WATCHY_WIFI_STOPPING));
    CHECK(!watchy_wifi_should_reconnect(WATCHY_WIFI_INITIALIZED));
    CHECK(!watchy_wifi_should_reconnect(WATCHY_WIFI_STOPPED));
    return 0;
}

static int test_storage_only_classifies_fully_erased_media_as_blank(void) {
    uint8_t bytes[32];
    memset(bytes, 0xff, sizeof(bytes));
    CHECK(watchy_storage_region_is_erased(bytes, sizeof(bytes)));
    bytes[17] = 0x7f;
    CHECK(!watchy_storage_region_is_erased(bytes, sizeof(bytes)));
    CHECK(!watchy_storage_region_is_erased(NULL, sizeof(bytes)));
    return 0;
}

int main(void) {
    CHECK(test_framebuffer_clips_pixels_and_rectangles() == 0);
    CHECK(test_button_wake_gpio_decodes_semantic_mask() == 0);
    CHECK(test_safe_mode_requires_back_and_down_held_together() == 0);
    CHECK(test_button_filter_debounces_each_semantic_edge_once() == 0);
    CHECK(test_battery_percentage_and_install_boundaries_are_conservative() == 0);
    CHECK(test_wake_cause_mapping_distinguishes_hardware_sources() == 0);
    CHECK(test_ssd1681_busy_is_active_high_and_requires_settling() == 0);
    CHECK(test_display_retained_state_controls_boot_refresh_and_commits_only_on_success() == 0);
    CHECK(test_display_refresh_policy_promotes_and_counts_each_physical_write() == 0);
    CHECK(test_display_multi_write_tracks_each_frame_and_invalidates_on_failure() == 0);
    CHECK(test_display_forecasts_every_optional_write_and_actual_target_mode() == 0);
    CHECK(test_display_transition_adapter_clears_before_plan_wide_threshold() == 0);
    CHECK(test_display_transition_adapter_cancellation_retains_last_successful_frame() == 0);
    CHECK(test_display_cancellation_maps_to_distinct_status_and_button_edges() == 0);
    CHECK(test_display_transition_adapter_write_failure_invalidates_retained_source() == 0);
    CHECK(test_pcf8563_calendar_validates_bcd_dates_century_and_unix_offsets() == 0);
    CHECK(test_pcf8563_alarm_encodes_documented_next_match_fields() == 0);
    CHECK(test_sleep_admission_and_wake_source_debounce_are_fail_closed() == 0);
    CHECK(test_motor_pin_is_retained_low_instead_of_generically_released() == 0);
    CHECK(test_rtc_initial_clock_only_becomes_ready_after_valid_decode() == 0);
    CHECK(test_radio_reconnect_is_blocked_while_stopping() == 0);
    CHECK(test_storage_only_classifies_fully_erased_media_as_blank() == 0);
    puts("PASS 22 HAL tests");
    return 0;
}
