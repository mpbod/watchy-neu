#include "watchy/battery.h"
#include "watchy/buses.h"
#include "watchy/buttons.h"
#include "watchy/diagnostics.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/package_runtime.h"
#include "watchy/portal.h"
#include "watchy/power.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"
#include "watchy/rtc_calendar.h"
#include "watchy/settings.h"
#include "watchy/shell.h"
#include "watchy/shell_render.h"
#include "watchy/storage.h"
#include "watchy/watchface_action.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_SHELL_IDLE_MS 30000u
#define WATCHY_BUTTON_POLL_MS 50u
#define WATCHY_NTP_WIFI_TIMEOUT_MS 15000u

static const char *TAG = "watchy";
static RTC_DATA_ATTR bool s_safe_mode_latched;

typedef struct {
    watchy_shell_presentation_outcome_t outcome;
    watchy_button_mask_t cancelled_buttons;
} shell_presentation_result_t;

typedef struct {
    bool prepared;
    bool catalog_readable;
    bool execution_blocked;
    bool package_warning;
    bool settings_save_failed;
} package_boot_state_t;

static shell_presentation_result_t shell_presentation(
    watchy_shell_presentation_outcome_t outcome,
    watchy_button_mask_t cancelled_buttons) {
    return (shell_presentation_result_t){outcome, cancelled_buttons};
}

static uint64_t milliseconds(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void collect_and_log_diagnostics(watchy_diagnostic_report_t *report) {
    if (report == NULL) return;
    watchy_diagnostics_collect(report);
    for (size_t index = 0u; index < report->count; ++index) {
        const watchy_diagnostic_entry_t *entry = &report->entries[index];
        ESP_LOGI(TAG, "diag service=%s state=%d status=%" PRId32 " detail=%s",
                 entry->service, entry->state, entry->status_code, entry->detail);
    }
}

static shell_presentation_result_t refresh_shell(
    watchy_shell_t *shell,
    const watchy_settings_t *settings,
    const watchy_time_t *time,
    const watchy_battery_state_t *battery,
    const watchy_package_catalog_t *catalog,
    const watchy_diagnostic_report_t *diagnostics,
    const char *detail,
    watchy_refresh_mode_t mode,
    const watchy_transition_request_v1_t *request) {
    watchy_canvas_t canvas = watchy_display_acquire();
    if (canvas.pixels == NULL) {
        watchy_display_invalidate_previous();
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
        return shell_presentation(WATCHY_SHELL_PRESENT_FAILED, 0u);
    }
    watchy_shell_render(&canvas, shell, settings, time, battery, catalog, diagnostics, detail);
    const watchy_status_t status = watchy_display_present(mode, request);
    if (status == WATCHY_STATUS_CANCELLED) {
        watchy_button_mask_t cancelled_buttons = 0u;
        if (watchy_display_take_cancelled_buttons(&cancelled_buttons)) {
            return shell_presentation(WATCHY_SHELL_PRESENT_CANCELLED,
                                      cancelled_buttons);
        }
        ESP_LOGE(TAG, "cancelled shell presentation had no sampled button");
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
        return shell_presentation(WATCHY_SHELL_PRESENT_FAILED, 0u);
    }
    if (status == WATCHY_STATUS_INVALID_STATE) {
        watchy_button_mask_t returned_buttons = 0u;
        (void)watchy_display_take_cancelled_buttons(&returned_buttons);
        ESP_LOGE(TAG, "shell display presentation failed; attempting full error target");
        watchy_display_invalidate_previous();
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
        canvas = watchy_display_acquire();
        if (canvas.pixels != NULL) {
            watchy_shell_render(&canvas, shell, settings, time, battery, catalog,
                                diagnostics, NULL);
            if (watchy_display_present(WATCHY_REFRESH_FULL, NULL) == WATCHY_STATUS_OK) {
                return shell_presentation(WATCHY_SHELL_PRESENT_RECOVERY,
                                          returned_buttons);
            }
        }
        ESP_LOGE(TAG, "shell display error target failed");
        return shell_presentation(WATCHY_SHELL_PRESENT_FAILED,
                                  returned_buttons);
    }
    if (status != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "shell display refresh failed");
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
        return shell_presentation(WATCHY_SHELL_PRESENT_FAILED, 0u);
    }
    return shell_presentation(WATCHY_SHELL_PRESENT_TARGET, 0u);
}

static watchy_shell_input_t input_from_mask(watchy_button_mask_t mask) {
    if ((mask & WATCHY_BUTTON_MASK_MENU) != 0u) return WATCHY_SHELL_INPUT_MENU;
    if ((mask & WATCHY_BUTTON_MASK_BACK) != 0u) return WATCHY_SHELL_INPUT_BACK;
    if ((mask & WATCHY_BUTTON_MASK_UP) != 0u) return WATCHY_SHELL_INPUT_UP;
    return WATCHY_SHELL_INPUT_DOWN;
}

static watchy_button_t package_button_from_mask(watchy_button_mask_t mask) {
    if ((mask & WATCHY_BUTTON_MASK_MENU) != 0u) return WATCHY_BUTTON_CONFIRM;
    if ((mask & WATCHY_BUTTON_MASK_BACK) != 0u) return WATCHY_BUTTON_BACK;
    if ((mask & WATCHY_BUTTON_MASK_UP) != 0u) return WATCHY_BUTTON_UP;
    return WATCHY_BUTTON_DOWN;
}

static watchy_package_status_t package_app_event(void *context,
                                                  const watchy_event_t *event) {
    (void)context;
    return watchy_packages_runner_event(event);
}

static bool package_app_active(void *context) {
    (void)context;
    return watchy_packages_runner_active();
}

static watchy_package_status_t package_app_render(void *context) {
    (void)context;
    return watchy_packages_runner_render();
}

static watchy_package_status_t package_app_stop(void *context) {
    (void)context;
    return watchy_packages_runner_stop();
}

static void take_display_cancelled_buttons(watchy_button_mask_t *buttons) {
    watchy_button_mask_t cancelled_buttons = 0u;

    if (buttons != NULL &&
        watchy_display_take_cancelled_buttons(&cancelled_buttons)) {
        *buttons |= cancelled_buttons;
    }
}

static watchy_package_app_run_result_t run_package_app(const char *package_ref) {
    static const watchy_package_app_runner_t runner = {
        .event = package_app_event,
        .active = package_app_active,
        .render = package_app_render,
        .stop = package_app_stop,
        .context = NULL,
    };
    watchy_button_mask_t pending_buttons = 0u;
    uint64_t last_activity;
    watchy_package_status_t status = watchy_packages_runner_start(package_ref, false);
    watchy_package_app_run_result_t result = {0};

    take_display_cancelled_buttons(&pending_buttons);
    if (status != WATCHY_PACKAGE_OK) {
        result.cancelled_buttons = pending_buttons;
        return result;
    }
    if (!watchy_packages_runner_active()) {
        result.succeeded = true;
        result.cancelled_buttons = pending_buttons;
        return result;
    }
    status = watchy_packages_runner_render();
    take_display_cancelled_buttons(&pending_buttons);
    last_activity = milliseconds();
    while (status == WATCHY_PACKAGE_OK && watchy_packages_runner_active()) {
        watchy_button_mask_t pressed = pending_buttons;
        pending_buttons = 0u;
        if (pressed == 0u) {
            watchy_button_press_event_t event;
            if (watchy_buttons_take_press(&event, WATCHY_BUTTON_POLL_MS)) {
                pressed = event.mask;
            }
        }
        if (milliseconds() - last_activity >= WATCHY_SHELL_IDLE_MS) {
            pending_buttons |= pressed;
            status = watchy_packages_runner_stop();
            break;
        }
        if (pressed == 0u && watchy_buttons_overflowed()) {
            ESP_LOGE(TAG, "button input queue overflow while package app was active");
            status = WATCHY_PACKAGE_ERR_STATE;
            break;
        }
        if (pressed != 0u) {
            last_activity = milliseconds();
            status = watchy_package_dispatch_app_button(
                &runner, package_button_from_mask(pressed));
            take_display_cancelled_buttons(&pending_buttons);
        }
    }
    if (watchy_packages_runner_active()) (void)watchy_packages_runner_stop();
    take_display_cancelled_buttons(&pending_buttons);
    result.succeeded = status == WATCHY_PACKAGE_OK;
    result.cancelled_buttons = pending_buttons;
    return result;
}

static watchy_package_status_t action_select_builtin(void *context) {
    (void)context;
    return watchy_packages_select_builtin();
}

static watchy_package_status_t action_select_watchface(void *context,
                                                       const char *package_ref) {
    (void)context;
    return watchy_packages_select_watchface(package_ref);
}

static watchy_watchface_run_result_t action_run_watchface(void *context,
                                                           bool safe_mode,
                                                           bool force_full_refresh) {
    (void)context;
    watchy_watchface_run_result_t result = {
        .rendered = watchy_packages_run_watchface(safe_mode, force_full_refresh),
    };
    (void)watchy_display_take_cancelled_buttons(&result.cancelled_buttons);
    return result;
}

static void action_force_full_refresh(void *context) {
    (void)context;
    watchy_display_invalidate_previous();
}

static watchy_package_status_t action_snapshot(void *context,
                                                watchy_package_catalog_t *catalog) {
    (void)context;
    return watchy_packages_snapshot(catalog);
}

static watchy_package_status_t boot_import_factory_seed(void *context) {
    (void)context;
    return watchy_packages_import_factory_seed(false);
}

static watchy_package_status_t boot_recover_stale_pending(void *context) {
    (void)context;
    return watchy_packages_recover_stale_pending();
}

static watchy_status_t action_save_settings(void *context,
                                             const watchy_settings_t *settings) {
    (void)context;
    return watchy_settings_save(settings);
}

static watchy_status_t portal_exit_stop(void *context) {
    (void)context;
    return watchy_portal_stop();
}

static watchy_status_t portal_exit_load_settings(
    void *context,
    watchy_settings_t *out_settings) {
    (void)context;
    return watchy_settings_load(out_settings);
}

static watchy_package_app_run_result_t action_run_app(
    void *context,
    const char *package_ref) {
    (void)context;
    return run_package_app(package_ref);
}

static const watchy_watchface_action_ops_t watchface_action_ops = {
    .select_builtin = action_select_builtin,
    .select_watchface = action_select_watchface,
    .run_watchface = action_run_watchface,
    .force_full_refresh = action_force_full_refresh,
    .snapshot = action_snapshot,
    .save_settings = action_save_settings,
    .context = NULL,
};

static const watchy_watchface_boot_ops_t watchface_boot_ops = {
    .import_factory_seed = boot_import_factory_seed,
    .recover_stale_pending = boot_recover_stale_pending,
    .snapshot = action_snapshot,
    .context = NULL,
};

static const watchy_watchface_portal_exit_ops_t portal_exit_ops = {
    .stop_portal = portal_exit_stop,
    .load_settings = portal_exit_load_settings,
    .snapshot = action_snapshot,
    .save_settings = action_save_settings,
    .context = NULL,
};

static void complete_package_boot(bool safe_mode,
                                  watchy_settings_t *settings,
                                  watchy_package_catalog_t *catalog,
                                  package_boot_state_t *state) {
    watchy_watchface_boot_result_t result = {0};
    if (state == NULL || state->prepared) return;

    const watchy_status_t status = watchy_watchface_boot_prepare(
        safe_mode, catalog, &watchface_boot_ops, &result);
    state->prepared = true;
    state->catalog_readable = status == WATCHY_STATUS_OK && result.catalog_readable;
    state->execution_blocked = status != WATCHY_STATUS_OK ||
                               !watchy_watchface_boot_allows_package_execution(
                                   safe_mode, &result);
    state->package_warning = status != WATCHY_STATUS_OK || result.package_warning;
    if (state->catalog_readable && !safe_mode &&
        watchy_watchface_reconcile_settings(
            settings, catalog, action_save_settings, NULL) != WATCHY_STATUS_OK) {
        state->settings_save_failed = true;
    }
}

static bool ensure_package_catalog(watchy_shell_t *shell,
                                   watchy_settings_t *settings,
                                   watchy_package_catalog_t *catalog,
                                   package_boot_state_t *state) {
    complete_package_boot(shell->safe_mode, settings, catalog, state);
    watchy_shell_set_package_catalog(
        shell, state->catalog_readable ? catalog : NULL, state->catalog_readable);
    shell->package_warning = shell->package_warning || state->package_warning;
    if (state->settings_save_failed) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
        return false;
    }
    if (state->execution_blocked && !shell->safe_mode) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
        return false;
    }
    return true;
}

static bool reconcile_package_mutation(
    watchy_shell_t *shell,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    watchy_package_mutation_result_t mutation) {
    watchy_watchface_catalog_mutation_result_t result = {0};
    if (watchy_watchface_reconcile_catalog_mutation(
            mutation, settings, catalog, action_snapshot, action_save_settings,
            NULL, &result) != WATCHY_STATUS_OK) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
        return false;
    }
    if (result.catalog_refreshed) {
        watchy_shell_set_package_catalog(shell, catalog, true);
    }
    if (result.settings_save_failed) {
        shell->package_warning = shell->package_warning || result.package_failed;
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
        return false;
    }
    if (result.package_failed) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
        return false;
    }
    return true;
}

static bool present_selected_watchface(
    watchy_shell_t *shell,
    watchy_shell_presentation_state_t *presentation,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    bool package_execution_blocked,
    bool force_full_refresh,
    bool *out_force_builtin_full) {
    watchy_watchface_run_result_t result = {0};
    const watchy_status_t status = watchy_watchface_return_selected(
        shell->safe_mode, package_execution_blocked, force_full_refresh,
        settings, catalog, &watchface_action_ops, &result);
    if (out_force_builtin_full != NULL) *out_force_builtin_full = false;
    if (status == WATCHY_STATUS_OK) {
        watchy_shell_set_package_catalog(shell, catalog, true);
        watchy_shell_presentation_observe(
            presentation, shell, WATCHY_SHELL_PRESENT_TARGET, 0u);
        return true;
    }
    if (status == WATCHY_STATUS_CANCELLED) {
        watchy_shell_set_package_catalog(shell, catalog, true);
        watchy_shell_presentation_observe(
            presentation, shell, WATCHY_SHELL_PRESENT_CANCELLED,
            result.cancelled_buttons);
        return true;
    }
    if (status == WATCHY_STATUS_UNSUPPORTED) return false;

    shell->package_warning = true;
    if (watchy_packages_snapshot(catalog) == WATCHY_PACKAGE_OK) {
        watchy_shell_set_package_catalog(shell, catalog, true);
        if (watchy_watchface_reconcile_settings(
                settings, catalog, action_save_settings, NULL) != WATCHY_STATUS_OK) {
            result.settings_save_failed = true;
        }
    }
    if (result.settings_save_failed) {
        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
    }
    watchy_display_invalidate_previous();
    if (out_force_builtin_full != NULL) *out_force_builtin_full = true;
    return false;
}

static watchy_status_t sync_time_ntp(const watchy_settings_t *settings) {
    watchy_wifi_sta_config_t wifi = {0};
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(settings->ntp_server);
    bool sntp_started = false;
    watchy_status_t result = WATCHY_STATUS_INVALID_STATE;
    if (settings->wifi_ssid[0] == '\0') return WATCHY_STATUS_INVALID_ARGUMENT;
    memcpy(wifi.ssid, settings->wifi_ssid, sizeof(wifi.ssid));
    memcpy(wifi.password, settings->wifi_password, sizeof(wifi.password));
    if (watchy_wifi_start_sta(&wifi, false) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    const uint64_t deadline = milliseconds() + WATCHY_NTP_WIFI_TIMEOUT_MS;
    while (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED &&
           (int64_t)(deadline - milliseconds()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED ||
        esp_netif_sntp_init(&sntp) != ESP_OK) goto cleanup;
    sntp_started = true;
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK ||
        setenv("TZ", settings->timezone, 1) != 0) goto cleanup;
    tzset();
    time_t epoch = time(NULL);
    struct tm local_tm;
    struct tm utc_tm;
    if (epoch < 0 || localtime_r(&epoch, &local_tm) == NULL ||
        gmtime_r(&epoch, &utc_tm) == NULL) goto cleanup;
    watchy_time_t local_zero = {
        .year = (int16_t)(local_tm.tm_year + 1900), .month = (uint8_t)(local_tm.tm_mon + 1),
        .day = (uint8_t)local_tm.tm_mday, .hour = (uint8_t)local_tm.tm_hour,
        .minute = (uint8_t)local_tm.tm_min, .second = (uint8_t)local_tm.tm_sec,
        .weekday = (uint8_t)local_tm.tm_wday, .utc_offset_minutes = 0,
    };
    watchy_time_t utc_zero = {
        .year = (int16_t)(utc_tm.tm_year + 1900), .month = (uint8_t)(utc_tm.tm_mon + 1),
        .day = (uint8_t)utc_tm.tm_mday, .hour = (uint8_t)utc_tm.tm_hour,
        .minute = (uint8_t)utc_tm.tm_min, .second = (uint8_t)utc_tm.tm_sec,
        .weekday = (uint8_t)utc_tm.tm_wday, .utc_offset_minutes = 0,
    };
    int64_t local_seconds;
    int64_t utc_seconds;
    if (watchy_calendar_to_unix(&local_zero, &local_seconds) != WATCHY_STATUS_OK ||
        watchy_calendar_to_unix(&utc_zero, &utc_seconds) != WATCHY_STATUS_OK ||
        (local_seconds - utc_seconds) / 60 < -1439 ||
        (local_seconds - utc_seconds) / 60 > 1439) goto cleanup;
    local_zero.utc_offset_minutes = (int16_t)((local_seconds - utc_seconds) / 60);
    result = watchy_rtc_set_local(&local_zero);
cleanup:
    if (sntp_started) esp_netif_sntp_deinit();
    (void)watchy_wifi_stop();
    return result;
}

static uint8_t days_in_month(const watchy_time_t *time) {
    watchy_time_t candidate = *time;
    candidate.day = 31u;
    while (candidate.day > 28u && !watchy_calendar_valid(&candidate)) --candidate.day;
    return candidate.day;
}

static void adjust_manual_time(watchy_time_t *time, uint8_t field, int delta) {
    if (field == 0u) {
        time->year = (int16_t)(time->year + delta);
        if (time->year > 2099) time->year = 2000;
        if (time->year < 2000) time->year = 2099;
    } else if (field == 1u) {
        int month = (int)time->month + delta;
        if (month > 12) month = 1;
        if (month < 1) month = 12;
        time->month = (uint8_t)month;
    } else if (field == 2u) {
        const uint8_t maximum = days_in_month(time);
        int day = (int)time->day + delta;
        if (day > maximum) day = 1;
        if (day < 1) day = maximum;
        time->day = (uint8_t)day;
    } else if (field == 3u) {
        time->hour = (uint8_t)(((int)time->hour + delta + 24) % 24);
    } else {
        time->minute = (uint8_t)(((int)time->minute + delta + 60) % 60);
    }
    const uint8_t maximum = days_in_month(time);
    if (time->day > maximum) time->day = maximum;
    time->second = 0u;
    int64_t epoch;
    watchy_time_t normalized;
    if (watchy_calendar_to_unix(time, &epoch) == WATCHY_STATUS_OK &&
        watchy_calendar_from_unix(epoch, time->utc_offset_minutes, &normalized) == WATCHY_STATUS_OK) {
        *time = normalized;
    }
}

static void manual_time_detail(const watchy_time_t *time, uint8_t field,
                               bool editing, char *detail, size_t size) {
    static const char *const names[] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE"};
    snprintf(detail, size, "%04d-%02u-%02u %02u:%02u\n%s %s", time->year, time->month,
             time->day, time->hour, time->minute, names[field], editing ? "EDIT" : "SELECT");
}

static void run_shell(watchy_shell_t *shell,
                      watchy_shell_presentation_state_t *presentation,
                      watchy_settings_t *settings,
                      watchy_time_t *time,
                      watchy_battery_state_t *battery,
                      watchy_package_catalog_t *catalog,
                      watchy_diagnostic_report_t *diagnostics,
                      package_boot_state_t *package_boot,
                      const char *safe_reason) {
    bool input_overflow_reported = false;
    uint64_t last_activity = milliseconds();
    char detail[192] = {0};
    if (shell->safe_mode && safe_reason != NULL) snprintf(detail, sizeof(detail), "%s", safe_reason);
    while (!shell->sleep_requested) {
        watchy_button_mask_t pressed =
            watchy_shell_presentation_take_cancelled_buttons(presentation);
        if (pressed == 0u) {
            watchy_button_press_event_t event;
            if (watchy_buttons_take_press(&event, WATCHY_BUTTON_POLL_MS)) {
                pressed = event.mask;
            }
        }
        if (pressed == 0u && !input_overflow_reported && watchy_buttons_overflowed()) {
            ESP_LOGE(TAG, "button input queue overflow; navigation event was lost");
            input_overflow_reported = true;
            shell->package_warning = true;
        }
        if (watchy_portal_active()) {
            if ((pressed & WATCHY_BUTTON_MASK_BACK) != 0u || watchy_portal_timed_out()) {
                const watchy_shell_screen_t before_screen = shell->screen;
                const watchy_shell_input_t transition_input =
                    (pressed & WATCHY_BUTTON_MASK_BACK) != 0u
                        ? WATCHY_SHELL_INPUT_BACK : WATCHY_SHELL_INPUT_IDLE;
                watchy_watchface_portal_exit_result_t portal_result = {0};
                const watchy_status_t portal_status = watchy_watchface_portal_exit(
                    settings, catalog, &portal_exit_ops, &portal_result);
                watchy_shell_input(shell, transition_input);
                detail[0] = '\0';
                watchy_shell_error_t portal_error =
                    portal_status == WATCHY_STATUS_OK
                        ? portal_result.error : WATCHY_SHELL_ERROR_PACKAGE;
                if (portal_result.catalog_refreshed) {
                    watchy_shell_set_package_catalog(shell, catalog, true);
                } else {
                    watchy_shell_set_package_catalog(shell, NULL, false);
                }
                if (portal_result.settings_reloaded) {
                    const watchy_status_t partial_status =
                        watchy_display_set_partial_limit(settings->partial_refresh_limit);
                    const watchy_status_t transition_status =
                        watchy_display_set_transition_policy(settings->transition_level,
                                                             true, shell->safe_mode,
                                                             battery->millivolts);
                    if (partial_status != WATCHY_STATUS_OK ||
                        transition_status != WATCHY_STATUS_OK) {
                        if (portal_error == WATCHY_SHELL_ERROR_NONE) {
                            portal_error = WATCHY_SHELL_ERROR_DISPLAY;
                        }
                    }
                }
                if (portal_error != WATCHY_SHELL_ERROR_NONE) {
                    watchy_shell_fail(shell, portal_error);
                }
                bool force_builtin_full = false;
                if (shell->screen == WATCHY_SHELL_WATCHFACE &&
                    portal_error == WATCHY_SHELL_ERROR_NONE &&
                    present_selected_watchface(
                        shell, presentation, settings, catalog,
                        package_boot->execution_blocked, true, &force_builtin_full)) {
                    last_activity = milliseconds();
                    continue;
                }
                watchy_transition_request_v1_t request;
                const watchy_shell_transition_context_t change = {
                    .from = before_screen,
                    .to = shell->screen,
                    .input = transition_input,
                    .saved = false,
                    .sleep_requested = shell->sleep_requested,
                    .safe_mode = shell->safe_mode,
                };
                const bool has_request = watchy_shell_transition_for_change(&change, &request);
                watchy_refresh_mode_t refresh_mode = force_builtin_full
                                                          ? WATCHY_REFRESH_FULL
                                                          : WATCHY_REFRESH_PARTIAL;
                if (shell->screen == WATCHY_SHELL_ERROR &&
                    before_screen != WATCHY_SHELL_ERROR) {
                    watchy_display_invalidate_previous();
                    refresh_mode = WATCHY_REFRESH_FULL;
                }
                const shell_presentation_result_t result =
                    refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                                  NULL, refresh_mode, has_request ? &request : NULL);
                watchy_shell_presentation_observe(presentation, shell, result.outcome,
                                                  result.cancelled_buttons);
                last_activity = milliseconds();
            }
            continue;
        }
        if (pressed != 0u) {
            const watchy_shell_input_t shell_input = input_from_mask(pressed);
            if (!package_boot->prepared &&
                watchy_watchface_catalog_needed_before_input(shell, shell_input) &&
                !ensure_package_catalog(shell, settings, catalog, package_boot)) {
                const shell_presentation_result_t result =
                    refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                                  NULL, WATCHY_REFRESH_FULL, NULL);
                watchy_shell_presentation_observe(presentation, shell, result.outcome,
                                                  result.cancelled_buttons);
                last_activity = milliseconds();
                continue;
            }
            watchy_shell_screen_t transition_from = shell->screen;
            bool saved = false;
            bool post_action_presentation_needed = true;
            detail[0] = '\0';
            watchy_shell_input(shell, shell_input);
            last_activity = milliseconds();
            watchy_shell_action_request_t action_request;
            (void)watchy_shell_take_action_request(shell, &action_request);
            const watchy_shell_action_t action = action_request.action;
            switch (action) {
            case WATCHY_SHELL_ACTION_SAVE_SETTINGS:
                if (shell->selection == 0u) settings->time_24h = !settings->time_24h;
                else if (shell->selection == 1u) settings->motion_wake = !settings->motion_wake;
                else if (shell->selection == 2u) {
                    if (!watchy_settings_cycle_transition_level(settings) ||
                        watchy_display_set_transition_policy(settings->transition_level,
                                                             true, shell->safe_mode,
                                                             battery->millivolts) !=
                            WATCHY_STATUS_OK) {
                        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
                        break;
                    }
                } else if (shell->selection == 7u) {
                    settings->partial_refresh_limit =
                        settings->partial_refresh_limit >= WATCHY_SETTINGS_PARTIAL_LIMIT_MAX
                            ? 5u : (uint16_t)(settings->partial_refresh_limit + 5u);
                    if (watchy_display_set_partial_limit(settings->partial_refresh_limit) !=
                        WATCHY_STATUS_OK) {
                        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_DISPLAY);
                        break;
                    }
                }
                if (watchy_settings_save(settings) != WATCHY_STATUS_OK) {
                    watchy_shell_fail(shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
                } else saved = true;
                break;
            case WATCHY_SHELL_ACTION_MANUAL_INCREMENT:
            case WATCHY_SHELL_ACTION_MANUAL_DECREMENT:
                adjust_manual_time(time, shell->selection,
                                   action == WATCHY_SHELL_ACTION_MANUAL_INCREMENT ? 1 : -1);
                break;
            case WATCHY_SHELL_ACTION_SAVE_MANUAL_TIME:
                if (watchy_rtc_set_local(time) != WATCHY_STATUS_OK) {
                    watchy_shell_fail(shell, WATCHY_SHELL_ERROR_MANUAL_TIME);
                } else saved = true;
                break;
            case WATCHY_SHELL_ACTION_SYNC_NTP: {
                snprintf(detail, sizeof(detail), "SYNCING");
                watchy_transition_request_v1_t sync_request;
                const watchy_shell_transition_context_t sync_change = {
                    .from = transition_from,
                    .to = shell->screen,
                    .input = shell_input,
                    .saved = false,
                    .sync_progress = true,
                    .sleep_requested = shell->sleep_requested,
                    .safe_mode = shell->safe_mode,
                    .has_rect = true,
                    .rect = watchy_shell_sync_progress_rect(),
                };
                const bool has_sync_request =
                    watchy_shell_transition_for_change(&sync_change, &sync_request);
                const shell_presentation_result_t sync_result =
                    refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                                  detail, WATCHY_REFRESH_PARTIAL,
                                  has_sync_request ? &sync_request : NULL);
                watchy_shell_presentation_observe(presentation, shell, sync_result.outcome,
                                                  sync_result.cancelled_buttons);
                const bool sync_visible = watchy_shell_presentation_needs_post_action(
                    sync_result.outcome);
                post_action_presentation_needed = sync_visible;
                if (sync_visible) {
                    if (sync_time_ntp(settings) == WATCHY_STATUS_OK &&
                        watchy_rtc_read_local(time) == WATCHY_STATUS_OK) {
                        snprintf(detail, sizeof(detail), "TIME UPDATED");
                    } else if (shell->screen != WATCHY_SHELL_ERROR) {
                        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_NTP);
                    }
                }
                transition_from = sync_visible ? WATCHY_SHELL_NTP_SYNC : shell->screen;
                last_activity = milliseconds();
                break;
            }
            case WATCHY_SHELL_ACTION_START_PORTAL_CLIENT:
            case WATCHY_SHELL_ACTION_START_PORTAL_AP: {
                watchy_portal_session_info_t info;
                const watchy_portal_network_mode_t mode = action == WATCHY_SHELL_ACTION_START_PORTAL_AP
                    ? WATCHY_PORTAL_NETWORK_AP : WATCHY_PORTAL_NETWORK_CLIENT;
                if (watchy_portal_start(mode, settings, &info) == WATCHY_STATUS_OK) {
                    if (!watchy_portal_format_watch_instructions(
                            &info, detail, sizeof(detail))) {
                        (void)watchy_portal_stop();
                        watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PORTAL);
                    }
                } else watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PORTAL);
                break;
            }
            case WATCHY_SHELL_ACTION_REMOVE_PACKAGE: {
                size_t catalog_index;
                if (watchy_shell_selected_package(shell, &catalog_index) &&
                    catalog_index < catalog->count) {
                    const watchy_package_mutation_result_t remove_result =
                        watchy_packages_remove_observed(
                            catalog->packages[catalog_index].package_ref);
                    if (reconcile_package_mutation(shell, settings, catalog,
                                                   remove_result)) {
                        snprintf(detail, sizeof(detail), "PACKAGE REMOVED");
                    }
                } else {
                    watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
                }
                break;
            }
            case WATCHY_SHELL_ACTION_RUN_PACKAGE: {
                size_t catalog_index;
                watchy_package_app_run_result_t app_result = {0};
                watchy_status_t app_status = WATCHY_STATUS_INVALID_STATE;
                if (watchy_shell_selected_package(shell, &catalog_index) &&
                    catalog_index < catalog->count) {
                    app_status = watchy_package_app_action_apply(
                        shell->safe_mode, package_boot->execution_blocked,
                        catalog->packages[catalog_index].package_ref,
                        action_run_app, NULL, &app_result);
                }
                if (app_result.cancelled_buttons != 0u) {
                    watchy_shell_presentation_observe(
                        presentation, shell,
                        app_status == WATCHY_STATUS_OK
                            ? WATCHY_SHELL_PRESENT_CANCELLED
                            : WATCHY_SHELL_PRESENT_FAILED,
                        app_result.cancelled_buttons);
                }
                if (app_status == WATCHY_STATUS_OK) {
                    snprintf(detail, sizeof(detail), "PACKAGE EXITED");
                } else {
                    shell->package_warning = true;
                    watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
                }
                last_activity = milliseconds();
                break;
            }
            case WATCHY_SHELL_ACTION_PURGE_PACKAGES:
                if (reconcile_package_mutation(
                        shell, settings, catalog,
                        watchy_packages_safe_mode_purge_observed())) {
                    snprintf(detail, sizeof(detail), "PACKAGES REMOVED");
                }
                break;
            case WATCHY_SHELL_ACTION_SELECT_BUILTIN:
            case WATCHY_SHELL_ACTION_SELECT_WATCHFACE: {
                watchy_watchface_run_result_t run_result = {0};
                const watchy_status_t status = watchy_watchface_action_apply(
                    &action_request, shell->safe_mode, package_boot->execution_blocked,
                    settings, catalog,
                    &watchface_action_ops, &run_result);
                if (status == WATCHY_STATUS_OK) {
                    watchy_shell_set_package_catalog(shell, catalog, true);
                    post_action_presentation_needed = !run_result.rendered;
                } else if (status == WATCHY_STATUS_CANCELLED) {
                    watchy_shell_set_package_catalog(shell, catalog, true);
                    watchy_shell_presentation_observe(
                        presentation, shell, WATCHY_SHELL_PRESENT_CANCELLED,
                        run_result.cancelled_buttons);
                    post_action_presentation_needed = false;
                } else {
                    shell->package_warning = true;
                    watchy_shell_fail(
                        shell, run_result.settings_save_failed
                                   ? WATCHY_SHELL_ERROR_SETTINGS_SAVE
                                   : WATCHY_SHELL_ERROR_PACKAGE);
                }
                last_activity = milliseconds();
                break;
            }
            case WATCHY_SHELL_ACTION_NORMAL_REBOOT:
                (void)watchy_portal_stop();
                s_safe_mode_latched = false;
                esp_restart();
            case WATCHY_SHELL_ACTION_NONE:
                break;
            }
            if (shell->screen == WATCHY_SHELL_MANUAL_TIME) {
                manual_time_detail(time, shell->selection, shell->editing, detail, sizeof(detail));
            } else if (shell->screen == WATCHY_SHELL_DIAGNOSTICS) {
                if (!watchy_motion_ready()) (void)watchy_motion_init();
                collect_and_log_diagnostics(diagnostics);
                watchy_shell_set_diagnostic_count(shell, diagnostics->count);
            }
            if (post_action_presentation_needed) {
                bool force_builtin_full = false;
                if (shell->screen == WATCHY_SHELL_WATCHFACE && !shell->safe_mode) {
                    post_action_presentation_needed = !present_selected_watchface(
                        shell, presentation, settings, catalog,
                        package_boot->execution_blocked,
                        shell_input == WATCHY_SHELL_INPUT_BACK,
                        &force_builtin_full);
                }
                if (!post_action_presentation_needed) {
                    last_activity = milliseconds();
                    continue;
                }
                watchy_transition_request_v1_t request;
                const watchy_shell_transition_context_t change = {
                    .from = transition_from,
                    .to = shell->screen,
                    .input = shell_input,
                    .saved = saved,
                    .sleep_requested = shell->sleep_requested,
                    .safe_mode = shell->safe_mode,
                    .has_rect = saved,
                    .rect = saved
                                ? watchy_shell_settings_confirmation_rect(shell->selection)
                                : (watchy_transition_rect_t){0},
                };
                const bool has_request = watchy_shell_transition_for_change(&change, &request);
                watchy_refresh_mode_t refresh_mode = force_builtin_full
                                                          ? WATCHY_REFRESH_FULL
                                                          : WATCHY_REFRESH_PARTIAL;
                if (shell->screen == WATCHY_SHELL_ERROR &&
                    transition_from != WATCHY_SHELL_ERROR) {
                    watchy_display_invalidate_previous();
                    refresh_mode = WATCHY_REFRESH_FULL;
                }
                const shell_presentation_result_t result =
                    refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                                  detail[0] == '\0' ? NULL : detail, refresh_mode,
                                  has_request ? &request : NULL);
                watchy_shell_presentation_observe(presentation, shell, result.outcome,
                                                  result.cancelled_buttons);
            }
        } else if (milliseconds() - last_activity >= WATCHY_SHELL_IDLE_MS) {
            if (!package_boot->prepared &&
                !ensure_package_catalog(shell, settings, catalog, package_boot)) {
                const shell_presentation_result_t result =
                    refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                                  NULL, WATCHY_REFRESH_FULL, NULL);
                watchy_shell_presentation_observe(presentation, shell, result.outcome,
                                                  result.cancelled_buttons);
                last_activity = milliseconds();
                continue;
            }
            const watchy_shell_screen_t before_screen = shell->screen;
            watchy_shell_input(shell, WATCHY_SHELL_INPUT_IDLE);
            bool force_builtin_full = false;
            if (!shell->safe_mode && present_selected_watchface(
                    shell, presentation, settings, catalog,
                    package_boot->execution_blocked, true, &force_builtin_full)) {
                last_activity = milliseconds();
                continue;
            }
            watchy_transition_request_v1_t request;
            const watchy_shell_transition_context_t change = {
                .from = before_screen,
                .to = shell->screen,
                .input = WATCHY_SHELL_INPUT_IDLE,
                .saved = false,
                .sleep_requested = shell->sleep_requested,
                .safe_mode = shell->safe_mode,
            };
            const bool has_request = watchy_shell_transition_for_change(&change, &request);
            const shell_presentation_result_t result =
                refresh_shell(shell, settings, time, battery, catalog, diagnostics,
                              NULL, force_builtin_full ? WATCHY_REFRESH_FULL
                                                       : WATCHY_REFRESH_PARTIAL,
                              has_request ? &request : NULL);
            watchy_shell_presentation_observe(presentation, shell, result.outcome,
                                              result.cancelled_buttons);
            last_activity = milliseconds();
        }
    }
}

void app_main(void) {
    static watchy_package_catalog_t catalog;
    const watchy_wake_cause_t wake_cause = watchy_power_capture_wake_cause();
    watchy_settings_t settings;
    watchy_shell_t shell;
    watchy_shell_presentation_state_t presentation = {0};
    watchy_diagnostic_report_t diagnostics;
    watchy_time_t time = {0};
    watchy_battery_state_t battery = {0};
    bool timer_configured = false;
    bool rtc_valid;
    bool safe_mode;
    bool package_selected = false;
    bool package_rendered = false;
    bool package_failed = false;
    watchy_button_mask_t boot_cancelled_buttons = 0u;
    package_boot_state_t package_boot = {.execution_blocked = true};
    bool settings_load_failed = false;
    bool settings_save_failed = false;
    bool display_failed = false;
    const char *safe_reason = "BACK+DOWN HELD";

    ESP_LOGI(TAG, "boot wake_cause=%d", wake_cause);
    if (watchy_storage_init() != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "storage initialization failed");
        safe_reason = "STORAGE UNAVAILABLE";
    }
    if (watchy_buses_init() != WATCHY_STATUS_OK) ESP_LOGE(TAG, "bus initialization failed");
    (void)watchy_buttons_init();
    /* Prepare AP credentials while ESP-IDF's early-boot hardware entropy
     * source can be safely bracketed, before the battery ADC is initialized. */
    (void)watchy_portal_prepare_ap_password();
    const bool safe_mode_chord = watchy_power_safe_mode_chord_allowed(wake_cause) &&
                                 watchy_buttons_is_safe_mode_chord(watchy_buttons_sample());
    safe_mode = safe_mode_chord || s_safe_mode_latched;
    if (safe_mode_chord) s_safe_mode_latched = true;
    const bool defer_package_boot =
        watchy_watchface_boot_should_defer(wake_cause, safe_mode);
    (void)watchy_haptics_init();
    (void)watchy_rtc_init();
    if (!defer_package_boot) (void)watchy_motion_init();
    (void)watchy_battery_init();
    display_failed = watchy_display_init() != WATCHY_STATUS_OK;
    if (watchy_settings_load(&settings) != WATCHY_STATUS_OK) {
        watchy_settings_defaults(&settings);
        settings_load_failed = true;
    }
    if (!display_failed &&
        watchy_display_set_partial_limit(settings.partial_refresh_limit) != WATCHY_STATUS_OK) {
        display_failed = true;
    }
    rtc_valid = watchy_rtc_read_local(&time) == WATCHY_STATUS_OK;
    if (!rtc_valid) {
        time = (watchy_time_t){.year = 2024, .month = 1u, .day = 1u,
                              .weekday = 1u, .utc_offset_minutes = 0};
    }
    (void)watchy_battery_read(&battery);
    if (watchy_display_set_transition_policy(settings.transition_level,
                                              wake_cause == WATCHY_WAKE_BUTTON,
                                              safe_mode, battery.millivolts) !=
        WATCHY_STATUS_OK) {
        display_failed = true;
    }

    if (!defer_package_boot) {
        complete_package_boot(safe_mode, &settings, &catalog, &package_boot);
        settings_save_failed = package_boot.settings_save_failed;
        package_failed = package_boot.package_warning;
    }
    if (package_boot.catalog_readable) {
        for (size_t index = 0u; index < catalog.count; ++index) {
            package_selected = package_selected || catalog.packages[index].active ||
                               catalog.packages[index].pending;
        }
    }
    const bool package_wake = wake_cause != WATCHY_WAKE_BUTTON &&
                              !(wake_cause == WATCHY_WAKE_MOTION && !settings.motion_wake);
    if (rtc_valid && !safe_mode && !package_boot.execution_blocked &&
        package_wake && package_selected) {
        package_rendered = watchy_packages_run_watchface(false, false);
        if (watchy_display_take_cancelled_buttons(&boot_cancelled_buttons)) {
            package_rendered = false;
        }
        if (watchy_packages_snapshot(&catalog) != WATCHY_PACKAGE_OK) {
            package_boot.catalog_readable = false;
            package_failed = true;
        } else if (watchy_watchface_reconcile_settings(
                       &settings, &catalog, action_save_settings, NULL) !=
                   WATCHY_STATUS_OK) {
            settings_save_failed = true;
        }
        package_failed = package_failed ||
                         (!package_rendered && boot_cancelled_buttons == 0u);
    }
    watchy_shell_begin(&shell, wake_cause, settings.motion_wake, safe_mode, package_failed);
    if (package_boot.prepared && package_boot.execution_blocked && !safe_mode) {
        watchy_shell_fail(&shell, WATCHY_SHELL_ERROR_PACKAGE);
    }
    if (boot_cancelled_buttons != 0u) {
        watchy_shell_presentation_observe(&presentation, &shell,
                                          WATCHY_SHELL_PRESENT_CANCELLED,
                                          boot_cancelled_buttons);
    }
    if (!rtc_valid && !safe_mode) {
        const bool interactive_time_recovery = wake_cause == WATCHY_WAKE_BUTTON ||
                                               wake_cause == WATCHY_WAKE_COLD ||
                                               wake_cause == WATCHY_WAKE_OTHER;
        watchy_shell_require_manual_time(&shell, interactive_time_recovery);
    }
    watchy_shell_set_package_catalog(
        &shell, package_boot.catalog_readable ? &catalog : NULL,
        package_boot.catalog_readable);
    memset(&diagnostics, 0, sizeof(diagnostics));
    if (!defer_package_boot) collect_and_log_diagnostics(&diagnostics);
    watchy_shell_set_diagnostic_count(&shell, diagnostics.count);
    if (settings_load_failed) {
        watchy_shell_fail(&shell, WATCHY_SHELL_ERROR_SETTINGS_LOAD);
    } else if (settings_save_failed) {
        watchy_shell_fail(&shell, WATCHY_SHELL_ERROR_SETTINGS_SAVE);
    } else if (display_failed) {
        watchy_shell_fail(&shell, WATCHY_SHELL_ERROR_DISPLAY);
    }
    if (!package_rendered &&
        !watchy_shell_presentation_has_pending_input(&presentation) &&
        !(wake_cause == WATCHY_WAKE_MOTION && !settings.motion_wake)) {
        watchy_transition_request_v1_t request;
        const watchy_transition_request_v1_t *request_ptr = NULL;
        if (wake_cause == WATCHY_WAKE_BUTTON) {
            const watchy_shell_transition_context_t change = {
                .from = WATCHY_SHELL_WATCHFACE,
                .to = shell.screen,
                .input = WATCHY_SHELL_INPUT_MENU,
                .saved = false,
                .sleep_requested = shell.sleep_requested,
                .safe_mode = shell.safe_mode,
            };
            if (watchy_shell_transition_for_change(&change, &request)) {
                request_ptr = &request;
            }
        }
        watchy_refresh_mode_t refresh_mode = wake_cause == WATCHY_WAKE_COLD
                                                  ? WATCHY_REFRESH_FULL
                                                  : WATCHY_REFRESH_PARTIAL;
        if (safe_mode || !rtc_valid || shell.screen == WATCHY_SHELL_ERROR) {
            watchy_display_invalidate_previous();
            refresh_mode = WATCHY_REFRESH_FULL;
        }
        const shell_presentation_result_t result =
            refresh_shell(&shell, &settings, &time, &battery, &catalog, &diagnostics,
                          safe_mode ? safe_reason : NULL, refresh_mode, request_ptr);
        watchy_shell_presentation_observe(&presentation, &shell, result.outcome,
                                          result.cancelled_buttons);
    }
    if (!shell.sleep_requested &&
        (wake_cause == WATCHY_WAKE_BUTTON || safe_mode || !rtc_valid ||
         !watchy_shell_presentation_allows_sleep(&presentation))) {
        run_shell(&shell, &presentation, &settings, &time, &battery, &catalog,
                  &diagnostics, &package_boot,
                  safe_mode ? safe_reason : NULL);
    }
    (void)watchy_portal_stop();
    if (!watchy_shell_presentation_allows_sleep(&presentation)) {
        ESP_LOGE(TAG, "display target not presented; remaining awake");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (watchy_rtc_ready()) timer_configured = watchy_rtc_set_minute_timer(1u) == WATCHY_STATUS_OK;
    if (timer_configured && settings.motion_wake && !watchy_motion_ready()) {
        (void)watchy_motion_init();
    }
    const watchy_status_t sleep_status = timer_configured
        ? watchy_power_prepare_deep_sleep_with_motion(true, settings.motion_wake)
        : watchy_power_prepare_button_only_sleep();
    if (sleep_status != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "sleep preparation failed; remaining awake");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    watchy_power_enter_deep_sleep();
}
