#include "watchy/settings.h"

#include <string.h>

static bool terminated(const char *text, size_t capacity) {
    return text != NULL && strnlen(text, capacity) < capacity;
}

static bool ascii_alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

static bool parse_timezone_name(const char **cursor) {
    const char *text = *cursor;
    size_t length = 0u;
    if (*text == '<') {
        ++text;
        while (*text != '\0' && *text != '>') {
            if (!(ascii_alpha(*text) || (*text >= '0' && *text <= '9') ||
                  *text == '+' || *text == '-')) {
                return false;
            }
            ++length;
            ++text;
        }
        if (*text != '>' || length < 3u) return false;
        *cursor = text + 1u;
        return true;
    }
    while (ascii_alpha(*text)) {
        ++length;
        ++text;
    }
    if (length < 3u) return false;
    *cursor = text;
    return true;
}

static bool parse_timezone_offset(const char **cursor) {
    const char *text = *cursor;
    unsigned hour = 0u;
    unsigned digits = 0u;
    if (*text == '+' || *text == '-') ++text;
    while (*text >= '0' && *text <= '9' && digits < 2u) {
        hour = hour * 10u + (unsigned)(*text - '0');
        ++digits;
        ++text;
    }
    if (digits == 0u || (*text >= '0' && *text <= '9') || hour > 24u) return false;
    for (unsigned field = 0u; field < 2u && *text == ':'; ++field) {
        ++text;
        if (!(text[0] >= '0' && text[0] <= '5' && text[1] >= '0' && text[1] <= '9')) {
            return false;
        }
        if (hour == 24u && (text[0] != '0' || text[1] != '0')) return false;
        text += 2u;
    }
    *cursor = text;
    return true;
}

static bool parse_timezone_rule(const char **cursor) {
    const char *text = *cursor;
    unsigned month = 0u;
    unsigned week = 0u;
    unsigned day = 0u;
    if (*text++ != 'M' || *text < '1' || *text > '9') return false;
    month = (unsigned)(*text++ - '0');
    if (*text >= '0' && *text <= '9') month = month * 10u + (unsigned)(*text++ - '0');
    if (*text++ != '.' || *text < '1' || *text > '5') return false;
    week = (unsigned)(*text++ - '0');
    if (*text++ != '.' || *text < '0' || *text > '6') return false;
    day = (unsigned)(*text++ - '0');
    if (month < 1u || month > 12u || week < 1u || week > 5u || day > 6u) return false;
    *cursor = text;
    return true;
}

static bool timezone_valid(const char *timezone) {
    const char *cursor;
    if (!terminated(timezone, WATCHY_SETTINGS_TIMEZONE_MAX + 1u) ||
        timezone[0] == '\0' || strchr(timezone, '/') != NULL) {
        return false;
    }
    cursor = timezone;
    if (!parse_timezone_name(&cursor) || !parse_timezone_offset(&cursor)) return false;
    if (*cursor == '\0') return true;
    if (!parse_timezone_name(&cursor)) return false;
    if (*cursor == '+' || *cursor == '-' || (*cursor >= '0' && *cursor <= '9')) {
        if (!parse_timezone_offset(&cursor)) return false;
    }
    if (*cursor++ != ',' || !parse_timezone_rule(&cursor) || *cursor++ != ',' ||
        !parse_timezone_rule(&cursor)) {
        return false;
    }
    return *cursor == '\0';
}

static bool hostname_valid(const char *hostname) {
    size_t length;
    if (!terminated(hostname, WATCHY_SETTINGS_NTP_SERVER_MAX + 1u) ||
        (length = strlen(hostname)) == 0u || hostname[0] == '.' || hostname[length - 1u] == '.') {
        return false;
    }
    size_t label_length = 0u;
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char value = (unsigned char)hostname[index];
        const bool alpha_numeric = (value >= 'a' && value <= 'z') ||
                                   (value >= 'A' && value <= 'Z') ||
                                   (value >= '0' && value <= '9');
        if (value == '.') {
            if (label_length == 0u || label_length > 63u || hostname[index - 1u] == '-') {
                return false;
            }
            label_length = 0u;
        } else if (!alpha_numeric && value != '-') {
            return false;
        } else {
            if (label_length == 0u && value == '-') return false;
            if (++label_length > 63u) return false;
        }
    }
    return label_length != 0u && hostname[length - 1u] != '-';
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
    for (size_t index = 0u; index < ssid_length; ++index) {
        const unsigned char value = (unsigned char)ssid[index];
        if (value < 0x20u || value > 0x7eu) return false;
    }
    if (password_length == 0u) return true;
    if (password_length < 8u || password_length > WATCHY_SETTINGS_WIFI_PASSWORD_MAX) return false;
    if (password_length == WATCHY_SETTINGS_WIFI_PASSWORD_MAX) {
        for (size_t index = 0u; index < password_length; ++index) {
            const char value = password[index];
            if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
                  (value >= 'A' && value <= 'F'))) return false;
        }
    } else for (size_t index = 0u; index < password_length; ++index) {
        const unsigned char value = (unsigned char)password[index];
        if (value < 0x20u || value > 0x7eu) return false;
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

static bool transition_level_valid(watchy_transition_level_t value) {
    return value == WATCHY_TRANSITION_LEVEL_FULL ||
           value == WATCHY_TRANSITION_LEVEL_REDUCED ||
           value == WATCHY_TRANSITION_LEVEL_OFF;
}

static watchy_transition_level_t next_transition_level(watchy_transition_level_t value) {
    return value == WATCHY_TRANSITION_LEVEL_FULL ? WATCHY_TRANSITION_LEVEL_REDUCED
         : value == WATCHY_TRANSITION_LEVEL_REDUCED ? WATCHY_TRANSITION_LEVEL_OFF
                                                    : WATCHY_TRANSITION_LEVEL_FULL;
}

void watchy_settings_defaults(watchy_settings_t *out_settings) {
    if (out_settings == NULL) {
        return;
    }
    memset(out_settings, 0, sizeof(*out_settings));
    memcpy(out_settings->timezone, "UTC0", sizeof("UTC0"));
    out_settings->time_24h = true;
    out_settings->motion_wake = true;
    out_settings->transition_level = WATCHY_TRANSITION_LEVEL_FULL;
    out_settings->partial_refresh_limit = WATCHY_SETTINGS_DEFAULT_PARTIAL_LIMIT;
    memcpy(out_settings->ntp_server, "pool.ntp.org", sizeof("pool.ntp.org"));
}

bool watchy_settings_valid(const watchy_settings_t *settings) {
    return settings != NULL && timezone_valid(settings->timezone) &&
           package_ref_valid(settings->active_watchface) &&
           wifi_valid(settings->wifi_ssid, settings->wifi_password) &&
           transition_level_valid(settings->transition_level) &&
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
    if (transition_level_valid(stored->transition_level)) {
        out_settings->transition_level = stored->transition_level;
    }
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

bool watchy_settings_cycle_transition_level(watchy_settings_t *settings) {
    if (settings == NULL) {
        return false;
    }
    settings->transition_level = next_transition_level(settings->transition_level);
    return true;
}

bool watchy_settings_set_wifi(watchy_settings_t *settings,
                              const char *ssid,
                              const char *password) {
    watchy_settings_t candidate;
    if (settings == NULL || !wifi_valid(ssid, password)) {
        return false;
    }
    candidate = *settings;
    memset(candidate.wifi_ssid, 0, sizeof(candidate.wifi_ssid));
    memset(candidate.wifi_password, 0, sizeof(candidate.wifi_password));
    memcpy(candidate.wifi_ssid, ssid, strlen(ssid));
    memcpy(candidate.wifi_password, password, strlen(password));
    if (!watchy_settings_valid(&candidate)) {
        return false;
    }
    *settings = candidate;
    return true;
}
