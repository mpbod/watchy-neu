#include "watchy/time_sync.h"

#include "watchy/rtc_calendar.h"

watchy_time_sync_decision_t watchy_time_sync_decide(
    bool wifi_configured,
    bool portal_active,
    watchy_portal_network_mode_t portal_mode) {
    if (!wifi_configured) {
        return WATCHY_TIME_SYNC_REJECT_NO_WIFI;
    }
    if (!portal_active) {
        return WATCHY_TIME_SYNC_START_SAVED_WIFI;
    }
    return portal_mode == WATCHY_PORTAL_NETWORK_CLIENT
               ? WATCHY_TIME_SYNC_BORROW_CONNECTED_WIFI
               : WATCHY_TIME_SYNC_REJECT_CLIENT_MODE_REQUIRED;
}

watchy_status_t watchy_time_sync_local_from_epoch(
    const watchy_settings_t *settings,
    int64_t utc_epoch,
    watchy_time_t *out_local) {
    int16_t utc_offset_minutes;
    if (settings == NULL || out_local == NULL ||
        !watchy_settings_timezone_offset(settings, &utc_offset_minutes)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_calendar_from_unix(utc_epoch, utc_offset_minutes, out_local);
}
