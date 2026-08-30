#include "watchy/settings.h"

#include <string.h>

static bool terminated(const char *text, size_t capacity) {
    return text != NULL && strnlen(text, capacity) < capacity;
}

static bool timezone_valid(const char *timezone) {
    size_t length;
    if (!terminated(timezone, WATCHY_SETTINGS_TIMEZONE_MAX + 1u) ||
        (length = strlen(timezone)) == 0u) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char value = (unsigned char)timezone[index];
        const bool alpha_numeric = (value >= 'a' && value <= 'z') ||
                                   (value >= 'A' && value <= 'Z') ||
                                   (value >= '0' && value <= '9');
        const bool punctuation = value == '<' || value == '>' || value == '+' ||
                                 value == '-' || value == ':' || value == ',' ||
                                 value == '.' || value == '/';
        if (!alpha_numeric && !punctuation) {
            return false;
        }
    }
    return true;
}

static bool hostname_valid(const char *hostname) {
    size_t length;
    if (!terminated(hostname, WATCHY_SETTINGS_NTP_SERVER_MAX + 1u) ||
        (length = strlen(hostname)) == 0u || hostname[0] == '.' || hostname[length - 1u] == '.') {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char value = (unsigned char)hostname[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '-')) {
            return false;
        }
    }
    return true;
}

static bool wifi_valid(const char *ssid, const char *password) {
    size_t ssid_length;
    size_t password_length;
    if (!terminated(ssid, WATCHY_SETTINGS_WIFI_SSID_MAX + 1u) ||
        !terminated(password, WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u)) {
        return false;
    }
    ssid_length = strlen(ssid);
    password_length = strlen(password);
    if (ssid_length == 0u) return password_length == 0u;
    if (password_length == 0u) return true;
    if (password_length < 8u || password_length > WATCHY_SETTINGS_WIFI_PASSWORD_MAX) return false;
    if (password_length == WATCHY_SETTINGS_WIFI_PASSWORD_MAX) {
        for (size_t index = 0u; index < password_length; ++index) {
            const char value = password[index];
            if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
                  (value >= 'A' && value <= 'F'))) return false;
        }
    }
    return true;
}

static bool package_ref_valid(const char *reference) {
    const char *separator;
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    size_t identifier_length;
    if (!terminated(reference, WATCHY_PACKAGE_REF_MAX + 1u)) {
        return false;
    }
    if (reference[0] == '\0') {
        return true;
    }
    separator = strchr(reference, '@');
    if (separator == NULL || strchr(separator + 1u, '@') != NULL) {
        return false;
    }
    identifier_length = (size_t)(separator - reference);
    if (identifier_length == 0u || identifier_length > WATCHY_PACKAGE_ID_MAX) {
        return false;
    }
    memcpy(identifier, reference, identifier_length);
    identifier[identifier_length] = '\0';
    return watchy_package_id_valid(identifier) && watchy_package_version_valid(separator + 1u);
}

void watchy_settings_defaults(watchy_settings_t *out_settings) {
    if (out_settings == NULL) {
        return;
    }
    memset(out_settings, 0, sizeof(*out_settings));
    memcpy(out_settings->timezone, "UTC0", sizeof("UTC0"));
    out_settings->time_24h = true;
    out_settings->motion_wake = true;
    out_settings->partial_refresh_limit = WATCHY_SETTINGS_DEFAULT_PARTIAL_LIMIT;
    memcpy(out_settings->ntp_server, "pool.ntp.org", sizeof("pool.ntp.org"));
}

bool watchy_settings_valid(const watchy_settings_t *settings) {
    return settings != NULL && timezone_valid(settings->timezone) &&
           package_ref_valid(settings->active_watchface) &&
           wifi_valid(settings->wifi_ssid, settings->wifi_password) &&
           settings->partial_refresh_limit > 0u &&
           settings->partial_refresh_limit <= WATCHY_SETTINGS_PARTIAL_LIMIT_MAX &&
           hostname_valid(settings->ntp_server);
}

void watchy_settings_sanitize(const watchy_settings_t *stored,
                              watchy_settings_t *out_settings) {
    watchy_settings_t defaults;
    if (out_settings == NULL) {
        return;
    }
    watchy_settings_defaults(&defaults);
    *out_settings = defaults;
    if (stored == NULL) {
        return;
    }
    if (timezone_valid(stored->timezone)) {
        memcpy(out_settings->timezone, stored->timezone, sizeof(out_settings->timezone));
    }
    out_settings->time_24h = stored->time_24h;
    out_settings->motion_wake = stored->motion_wake;
    if (package_ref_valid(stored->active_watchface)) {
        memcpy(out_settings->active_watchface, stored->active_watchface,
               sizeof(out_settings->active_watchface));
    }
    if (wifi_valid(stored->wifi_ssid, stored->wifi_password)) {
        memcpy(out_settings->wifi_ssid, stored->wifi_ssid, sizeof(out_settings->wifi_ssid));
        memcpy(out_settings->wifi_password, stored->wifi_password,
               sizeof(out_settings->wifi_password));
    }
    if (stored->partial_refresh_limit > 0u &&
        stored->partial_refresh_limit <= WATCHY_SETTINGS_PARTIAL_LIMIT_MAX) {
        out_settings->partial_refresh_limit = stored->partial_refresh_limit;
    }
    if (hostname_valid(stored->ntp_server)) {
        memcpy(out_settings->ntp_server, stored->ntp_server, sizeof(out_settings->ntp_server));
    }
}
