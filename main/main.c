#include "app_hooks.h"

#include "watchy/battery.h"
#include "watchy/buses.h"
#include "watchy/buttons.h"
#include "watchy/diagnostics.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/power.h"
#include "watchy/rtc.h"
#include "watchy/storage.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watchy";

static const uint8_t DIGITS[10][5] = {
    {0x07, 0x05, 0x05, 0x05, 0x07},
    {0x02, 0x06, 0x02, 0x02, 0x07},
    {0x07, 0x01, 0x07, 0x04, 0x07},
    {0x07, 0x01, 0x07, 0x01, 0x07},
    {0x05, 0x05, 0x07, 0x01, 0x01},
    {0x07, 0x04, 0x07, 0x01, 0x07},
    {0x07, 0x04, 0x07, 0x05, 0x07},
    {0x07, 0x01, 0x01, 0x01, 0x01},
    {0x07, 0x05, 0x07, 0x05, 0x07},
    {0x07, 0x05, 0x07, 0x01, 0x07},
};

static void draw_digit(uint8_t *framebuffer, int16_t x, int16_t y, uint8_t digit, int16_t scale) {
    for (int16_t row = 0; row < 5; ++row) {
        for (int16_t column = 0; column < 3; ++column) {
            if ((DIGITS[digit][row] & (uint8_t)(1u << (2 - column))) != 0u) {
                watchy_framebuffer_fill_rect(framebuffer, WATCHY_DISPLAY_FRAMEBUFFER_SIZE,
                                             x + column * scale, y + row * scale,
                                             scale, scale, true);
            }
        }
    }
}

static void render_status(const watchy_time_t *time,
                          bool time_valid,
                          const watchy_battery_state_t *battery,
                          bool battery_valid,
                          bool safe_mode,
                          watchy_wake_cause_t wake_cause) {
    watchy_canvas_t canvas = watchy_display_acquire();
    if (canvas.pixels == NULL) {
        return;
    }
    watchy_framebuffer_fill(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, false);
    if (time_valid) {
        const int16_t scale = 7;
        const int16_t top = 68;
        draw_digit(canvas.pixels, 35, top, (uint8_t)(time->hour / 10), scale);
        draw_digit(canvas.pixels, 63, top, (uint8_t)(time->hour % 10), scale);
        watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 94, top + 7, 5, 5, true);
        watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 94, top + 24, 5, 5, true);
        draw_digit(canvas.pixels, 106, top, (uint8_t)(time->minute / 10), scale);
        draw_digit(canvas.pixels, 134, top, (uint8_t)(time->minute % 10), scale);
    } else {
        watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 45, 92, 110, 8, true);
    }

    watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 30, 145, 140, 3, true);
    watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 30, 165, 140, 3, true);
    watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 30, 145, 3, 23, true);
    watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 167, 145, 3, 23, true);
    if (battery_valid) {
        const int16_t width = (int16_t)((uint32_t)battery->percent * 132u / 100u);
        watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 34, 151, width, 11, true);
    }
    if (safe_mode) {
        watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, 8, 8, 20, 20, true);
    }
    watchy_framebuffer_fill_rect(canvas.pixels, WATCHY_DISPLAY_FRAMEBUFFER_SIZE,
                                 175, 8, 4 + (int16_t)wake_cause * 3, 8, true);
}

static void log_diagnostics(void) {
    watchy_diagnostic_report_t report;
    watchy_diagnostics_collect(&report);
    for (size_t index = 0; index < report.count; ++index) {
        const watchy_diagnostic_entry_t *entry = &report.entries[index];
        ESP_LOGI(TAG, "diag service=%s state=%d status=%" PRId32 " detail=%s",
                 entry->service, entry->state, entry->status_code, entry->detail);
    }
}

void app_main(void) {
    const watchy_wake_cause_t wake_cause = watchy_power_capture_wake_cause();
    watchy_time_t time = {0};
    watchy_battery_state_t battery = {0};
    bool safe_mode;
    bool time_valid;
    bool battery_valid;
    bool timer_configured = false;

    ESP_LOGI(TAG, "boot wake_cause=%d", wake_cause);
    if (watchy_storage_init() != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "storage initialization failed");
    }
    if (watchy_buses_init() != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "bus initialization failed");
    }
    watchy_buttons_init();
    safe_mode = wake_cause == WATCHY_WAKE_COLD &&
                watchy_buttons_is_safe_mode_chord(watchy_buttons_sample());
    watchy_haptics_init();
    watchy_rtc_init();
    watchy_motion_init();
    watchy_battery_init();
    watchy_display_init();

    if (safe_mode) {
        watchy_app_safe_mode_hook();
    } else {
        watchy_app_loader_hook();
    }

    time_valid = watchy_rtc_read_local(&time) == WATCHY_STATUS_OK;
    battery_valid = watchy_battery_read(&battery) == WATCHY_STATUS_OK;
    log_diagnostics();
    render_status(&time, time_valid, &battery, battery_valid, safe_mode, wake_cause);
    if (watchy_display_ready()) {
        if (watchy_display_refresh(wake_cause == WATCHY_WAKE_COLD ? WATCHY_REFRESH_FULL
                                                                  : WATCHY_REFRESH_PARTIAL) !=
            WATCHY_STATUS_OK) {
            ESP_LOGE(TAG, "display refresh failed");
        }
    }
    if (watchy_rtc_ready()) {
        timer_configured = watchy_rtc_set_minute_timer(1) == WATCHY_STATUS_OK;
    }
    if (watchy_power_prepare_deep_sleep(timer_configured) != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "sleep preparation failed; remaining awake to avoid an unwakeable sleep");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    watchy_power_enter_deep_sleep();
}
