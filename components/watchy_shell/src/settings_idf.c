#include "watchy/settings.h"

#include <string.h>

#include "nvs.h"

#define WATCHY_SETTINGS_NVS_NAMESPACE "watchy_cfg"

static esp_err_t get_string(nvs_handle_t handle, const char *key, char *value, size_t capacity) {
    size_t size = capacity;
    char temporary[WATCHY_PACKAGE_REF_MAX + 1u];
    esp_err_t error;
    if (capacity > sizeof(temporary)) return ESP_ERR_INVALID_ARG;
    error = nvs_get_str(handle, key, temporary, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND || error == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_OK;
    }
    if (error != ESP_OK) return error;
    if (size == 0u || size > capacity) return ESP_OK;
    memcpy(value, temporary, size);
    return ESP_OK;
}

watchy_status_t watchy_settings_load(watchy_settings_t *out_settings) {
    watchy_settings_t stored;
    nvs_handle_t handle = 0u;
    uint8_t value;
    uint16_t partial;
    esp_err_t error;
    if (out_settings == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    watchy_settings_defaults(&stored);
    error = nvs_open(WATCHY_SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *out_settings = stored;
        return WATCHY_STATUS_OK;
    }
    if (error != ESP_OK) {
        *out_settings = stored;
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = get_string(handle, "tz", stored.timezone, sizeof(stored.timezone));
    esp_err_t field_error = nvs_get_u8(handle, "hour24", &value);
    if (field_error == ESP_OK && value <= 1u) {
        stored.time_24h = value == 1u;
    } else if (error == ESP_OK && field_error != ESP_ERR_NVS_NOT_FOUND && field_error != ESP_OK) {
        error = field_error;
    }
    field_error = nvs_get_u8(handle, "motion", &value);
    if (field_error == ESP_OK && value <= 1u) {
        stored.motion_wake = value == 1u;
    } else if (error == ESP_OK && field_error != ESP_ERR_NVS_NOT_FOUND && field_error != ESP_OK) {
        error = field_error;
    }
    if (error == ESP_OK) error = get_string(handle, "face", stored.active_watchface,
                                             sizeof(stored.active_watchface));
    if (error == ESP_OK) error = get_string(handle, "ssid", stored.wifi_ssid,
                                             sizeof(stored.wifi_ssid));
    if (error == ESP_OK) error = get_string(handle, "wifi_pass", stored.wifi_password,
                                             sizeof(stored.wifi_password));
    field_error = nvs_get_u16(handle, "partial", &partial);
    if (field_error == ESP_OK) {
        stored.partial_refresh_limit = partial;
    } else if (error == ESP_OK && field_error != ESP_ERR_NVS_NOT_FOUND) {
        error = field_error;
    }
    if (error == ESP_OK) error = get_string(handle, "ntp", stored.ntp_server,
                                             sizeof(stored.ntp_server));
    nvs_close(handle);
    watchy_settings_sanitize(&stored, out_settings);
    return error == ESP_OK ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_settings_save(const watchy_settings_t *settings) {
    nvs_handle_t handle = 0u;
    esp_err_t error;
    if (!watchy_settings_valid(settings)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    error = nvs_open(WATCHY_SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_str(handle, "tz", settings->timezone);
    if (error == ESP_OK) error = nvs_set_u8(handle, "hour24", settings->time_24h ? 1u : 0u);
    if (error == ESP_OK) error = nvs_set_u8(handle, "motion", settings->motion_wake ? 1u : 0u);
    if (error == ESP_OK) error = nvs_set_str(handle, "face", settings->active_watchface);
    if (error == ESP_OK) error = nvs_set_str(handle, "ssid", settings->wifi_ssid);
    if (error == ESP_OK) error = nvs_set_str(handle, "wifi_pass", settings->wifi_password);
    if (error == ESP_OK) error = nvs_set_u16(handle, "partial", settings->partial_refresh_limit);
    if (error == ESP_OK) error = nvs_set_str(handle, "ntp", settings->ntp_server);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0u) {
        nvs_close(handle);
    }
    return error == ESP_OK ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}
