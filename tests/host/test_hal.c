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

static int test_sleep_admission_and_wake_source_debounce_are_fail_closed(void) {
    watchy_sleep_requirements_t requirements = {
        .timer_configured = true,
        .radios_stopped = true,
        .motor_off = true,
        .display_hibernated = true,
        .rtc_source_cleared = true,
        .motion_source_configured = true,
        .ext0_configured = true,
        .ext1_configured = true,
        .sources_inactive = true,
    };
    watchy_wake_source_filter_t filter = {0};

    CHECK(watchy_power_sleep_allowed(&requirements));
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
    CHECK(!watchy_power_release_pin_for_sleep(WATCHY_PIN_MOTOR));
    CHECK(watchy_power_release_pin_for_sleep(WATCHY_PIN_DISPLAY_BUSY));
    CHECK(watchy_power_release_pin_for_sleep(WATCHY_PIN_BATTERY_ADC));
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
    CHECK(test_battery_percentage_and_install_boundaries_are_conservative() == 0);
    CHECK(test_wake_cause_mapping_distinguishes_hardware_sources() == 0);
    CHECK(test_ssd1681_busy_is_active_high_and_requires_settling() == 0);
    CHECK(test_display_retained_state_controls_boot_refresh_and_commits_only_on_success() == 0);
    CHECK(test_pcf8563_calendar_validates_bcd_dates_century_and_unix_offsets() == 0);
    CHECK(test_pcf8563_alarm_encodes_documented_next_match_fields() == 0);
    CHECK(test_sleep_admission_and_wake_source_debounce_are_fail_closed() == 0);
    CHECK(test_motor_pin_is_retained_low_instead_of_generically_released() == 0);
    CHECK(test_rtc_initial_clock_only_becomes_ready_after_valid_decode() == 0);
    CHECK(test_radio_reconnect_is_blocked_while_stopping() == 0);
    CHECK(test_storage_only_classifies_fully_erased_media_as_blank() == 0);
    puts("PASS 14 HAL tests");
    return 0;
}
