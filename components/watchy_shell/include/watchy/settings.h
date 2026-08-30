#ifndef WATCHY_SETTINGS_H
#define WATCHY_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/packages.h"
#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_SETTINGS_TIMEZONE_MAX 63u
#define WATCHY_SETTINGS_NTP_SERVER_MAX 63u
#define WATCHY_SETTINGS_WIFI_SSID_MAX 32u
#define WATCHY_SETTINGS_WIFI_PASSWORD_MAX 64u
#define WATCHY_SETTINGS_DEFAULT_PARTIAL_LIMIT 20u
#define WATCHY_SETTINGS_PARTIAL_LIMIT_MAX 100u

typedef struct {
    char timezone[WATCHY_SETTINGS_TIMEZONE_MAX + 1u];
    bool time_24h;
    bool motion_wake;
    char active_watchface[WATCHY_PACKAGE_REF_MAX + 1u];
    char wifi_ssid[WATCHY_SETTINGS_WIFI_SSID_MAX + 1u];
    char wifi_password[WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u];
    uint16_t partial_refresh_limit;
    char ntp_server[WATCHY_SETTINGS_NTP_SERVER_MAX + 1u];
} watchy_settings_t;

void watchy_settings_defaults(watchy_settings_t *out_settings);
bool watchy_settings_valid(const watchy_settings_t *settings);
void watchy_settings_sanitize(const watchy_settings_t *stored,
                              watchy_settings_t *out_settings);
watchy_status_t watchy_settings_load(watchy_settings_t *out_settings);
watchy_status_t watchy_settings_save(const watchy_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif
