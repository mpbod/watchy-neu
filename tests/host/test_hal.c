#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "watchy/battery.h"
#include "watchy/board.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/power.h"

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
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, back | motion) == WATCHY_WAKE_BUTTON);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_EXT1, 0) == WATCHY_WAKE_OTHER);
    CHECK(watchy_power_map_wake(WATCHY_RAW_WAKE_TIMER, 0) == WATCHY_WAKE_OTHER);
    return 0;
}

int main(void) {
    CHECK(test_framebuffer_clips_pixels_and_rectangles() == 0);
    CHECK(test_button_wake_gpio_decodes_semantic_mask() == 0);
    CHECK(test_safe_mode_requires_back_and_down_held_together() == 0);
    CHECK(test_battery_percentage_and_install_boundaries_are_conservative() == 0);
    CHECK(test_wake_cause_mapping_distinguishes_hardware_sources() == 0);
    puts("PASS 5 HAL tests");
    return 0;
}
