#include "watchy/time_sync.h"

#include "watchy/radios.h"
#include "watchy/rtc.h"
#include "watchy/rtc_calendar.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_NTP_WIFI_TIMEOUT_MS 15000u
#define WATCHY_NTP_SYNC_TIMEOUT_MS 10000u
#define WATCHY_NTP_WIFI_POLL_MS 100u

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static watchy_status_t local_from_posix_timezone(
    const watchy_settings_t *settings,
    time_t epoch,
    watchy_time_t *out_local) {
    struct tm local_tm;
    struct tm utc_tm;
    int64_t local_seconds;
    int64_t utc_seconds;
    int64_t offset_minutes;

    if (setenv("TZ", settings->timezone, 1) != 0) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    tzset();
    if (localtime_r(&epoch, &local_tm) == NULL ||
        gmtime_r(&epoch, &utc_tm) == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_local = (watchy_time_t){
        .year = (int16_t)(local_tm.tm_year + 1900),
        .month = (uint8_t)(local_tm.tm_mon + 1),
        .day = (uint8_t)local_tm.tm_mday,
        .hour = (uint8_t)local_tm.tm_hour,
        .minute = (uint8_t)local_tm.tm_min,
        .second = (uint8_t)local_tm.tm_sec,
        .weekday = (uint8_t)local_tm.tm_wday,
        .utc_offset_minutes = 0,
    };
    watchy_time_t utc = {
        .year = (int16_t)(utc_tm.tm_year + 1900),
        .month = (uint8_t)(utc_tm.tm_mon + 1),
        .day = (uint8_t)utc_tm.tm_mday,
        .hour = (uint8_t)utc_tm.tm_hour,
        .minute = (uint8_t)utc_tm.tm_min,
        .second = (uint8_t)utc_tm.tm_sec,
        .weekday = (uint8_t)utc_tm.tm_wday,
        .utc_offset_minutes = 0,
    };
    if (watchy_calendar_to_unix(out_local, &local_seconds) != WATCHY_STATUS_OK ||
        watchy_calendar_to_unix(&utc, &utc_seconds) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    offset_minutes = (local_seconds - utc_seconds) / 60;
    if (offset_minutes < -1439 || offset_minutes > 1439) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    out_local->utc_offset_minutes = (int16_t)offset_minutes;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_time_sync_connected(const watchy_settings_t *settings) {
    bool sntp_started = false;
    watchy_status_t result = WATCHY_STATUS_INVALID_STATE;
    watchy_time_t local;

    if (settings == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(settings->ntp_server);
    if (esp_netif_sntp_init(&sntp) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    sntp_started = true;
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(WATCHY_NTP_SYNC_TIMEOUT_MS)) == ESP_OK) {
        const time_t epoch = time(NULL);
        if (epoch >= 0) {
            int16_t fixed_offset;
            result = watchy_settings_timezone_offset(settings, &fixed_offset)
                         ? watchy_time_sync_local_from_epoch(settings, (int64_t)epoch, &local)
                         : local_from_posix_timezone(settings, epoch, &local);
            if (result == WATCHY_STATUS_OK) {
                result = watchy_rtc_set_local(&local);
            }
        }
    }
    if (sntp_started) {
        esp_netif_sntp_deinit();
    }
    return result;
}

watchy_status_t watchy_time_sync_saved_wifi(const watchy_settings_t *settings) {
    watchy_status_t result = WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_wifi_sta_config_t wifi = {0};

    if (settings == NULL || settings->wifi_ssid[0] == '\0') {
        goto cleanup;
    }
    memcpy(wifi.ssid, settings->wifi_ssid, sizeof(wifi.ssid));
    memcpy(wifi.password, settings->wifi_password, sizeof(wifi.password));
    if (watchy_wifi_start_sta(&wifi, false) != WATCHY_STATUS_OK) {
        result = WATCHY_STATUS_INVALID_STATE;
        goto cleanup;
    }
    const uint64_t deadline = now_ms() + WATCHY_NTP_WIFI_TIMEOUT_MS;
    while (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED &&
           (int64_t)(deadline - now_ms()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(WATCHY_NTP_WIFI_POLL_MS));
    }
    result = watchy_wifi_state() == WATCHY_WIFI_STA_CONNECTED
                 ? watchy_time_sync_connected(settings)
                 : WATCHY_STATUS_INVALID_STATE;

cleanup:
    (void)watchy_wifi_stop();
    return result;
}
