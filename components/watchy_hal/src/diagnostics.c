#include "watchy/diagnostics.h"

#include "watchy/battery.h"
#include "watchy/buses.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/power.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"
#include "watchy/storage.h"

#include <string.h>

static void add_entry(watchy_diagnostic_report_t *report,
                      const char *service,
                      watchy_diagnostic_state_t state,
                      watchy_status_t status,
                      const char *detail) {
    watchy_diagnostic_entry_t *entry = &report->entries[report->count++];
    entry->service = service;
    entry->state = state;
    entry->status_code = status;
    entry->detail = detail;
}

void watchy_diagnostics_collect(watchy_diagnostic_report_t *out_report) {
    watchy_time_t time;
    watchy_motion_data_t motion;
    watchy_battery_state_t battery;
    size_t total;
    size_t free;
    watchy_status_t status;

    if (out_report == NULL) {
        return;
    }
    memset(out_report, 0, sizeof(*out_report));
    add_entry(out_report, "buses", watchy_buses_ready() ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              watchy_buses_ready() ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE, "I2C and SPI ownership");
    add_entry(out_report, "display", watchy_display_ready() ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              watchy_display_ready() ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE, "SSD1681 framebuffer");
    add_entry(out_report, "buttons", watchy_buttons_ready() ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              watchy_buttons_ready() ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE, "four active-high inputs");

    status = watchy_rtc_read_local(&time);
    add_entry(out_report, "rtc", status == WATCHY_STATUS_OK ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              status, status == WATCHY_STATUS_OK ? "PCF8563 clock valid" : "RTC read or VL flag failed");

    status = watchy_motion_read(&motion);
    add_entry(out_report, "motion", status == WATCHY_STATUS_OK ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              status, status == WATCHY_STATUS_OK ? "BMA423 acceleration available" : "BMA423 read failed");

    status = watchy_battery_read(&battery);
    add_entry(out_report, "battery", status == WATCHY_STATUS_OK ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              status, watchy_battery_charging_supported() ? "calibrated ADC" : "calibrated ADC; charge state unsupported");

    add_entry(out_report, "haptics", watchy_haptics_ready() ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              watchy_haptics_ready() ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE, "motor forced off");

    status = watchy_storage_space(&total, &free);
    add_entry(out_report, "storage", status == WATCHY_STATUS_OK ? WATCHY_DIAGNOSTIC_PASS : WATCHY_DIAGNOSTIC_FAIL,
              status, status == WATCHY_STATUS_OK
                          ? "LittleFS mounted"
                          : (watchy_storage_state() == WATCHY_STORAGE_CORRUPT
                                 ? "LittleFS mount failed; nonblank media preserved for recovery"
                                 : "LittleFS unavailable"));

    const watchy_wifi_state_t wifi_state = watchy_wifi_state();
    add_entry(out_report, "wifi",
              wifi_state == WATCHY_WIFI_STOPPED
                  ? WATCHY_DIAGNOSTIC_STOPPED
                  : (wifi_state == WATCHY_WIFI_STOPPING ? WATCHY_DIAGNOSTIC_UNAVAILABLE
                                                        : WATCHY_DIAGNOSTIC_PASS),
              wifi_state == WATCHY_WIFI_STOPPING ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK,
              wifi_state == WATCHY_WIFI_STOPPED ? "radio not initialized by normal boot"
                                                : "radio lifecycle active");
    const watchy_ble_state_t ble_state = watchy_ble_state();
    add_entry(out_report, "ble",
              ble_state == WATCHY_BLE_STOPPED
                  ? WATCHY_DIAGNOSTIC_STOPPED
                  : (ble_state == WATCHY_BLE_STOPPING ? WATCHY_DIAGNOSTIC_UNAVAILABLE
                                                      : WATCHY_DIAGNOSTIC_PASS),
              ble_state == WATCHY_BLE_STOPPING ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK,
              ble_state == WATCHY_BLE_STOPPED ? "radio not initialized by normal boot"
                                              : "radio lifecycle active");
    status = watchy_power_last_prepare_status();
    add_entry(out_report, "power",
              status == WATCHY_STATUS_OK ? WATCHY_DIAGNOSTIC_PASS
                                         : (watchy_power_prepare_attempted()
                                                ? WATCHY_DIAGNOSTIC_FAIL
                                                : WATCHY_DIAGNOSTIC_UNAVAILABLE),
              status, status == WATCHY_STATUS_OK ? "last sleep preparation succeeded"
                                                 : (watchy_power_prepare_attempted()
                                                        ? "last sleep preparation failed"
                                                        : "sleep preparation not yet verified"));
}
