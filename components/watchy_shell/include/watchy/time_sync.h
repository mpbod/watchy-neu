#ifndef WATCHY_TIME_SYNC_H
#define WATCHY_TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/portal.h"
#include "watchy/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_TIME_SYNC_START_SAVED_WIFI = 0,
    WATCHY_TIME_SYNC_BORROW_CONNECTED_WIFI,
    WATCHY_TIME_SYNC_REJECT_CLIENT_MODE_REQUIRED,
    WATCHY_TIME_SYNC_REJECT_NO_WIFI,
} watchy_time_sync_decision_t;

watchy_time_sync_decision_t watchy_time_sync_decide(
    bool wifi_configured,
    bool portal_active,
    watchy_portal_network_mode_t portal_mode);
watchy_status_t watchy_time_sync_connected(const watchy_settings_t *settings);
watchy_status_t watchy_time_sync_saved_wifi(const watchy_settings_t *settings);
watchy_status_t watchy_time_sync_local_from_epoch(
    const watchy_settings_t *settings,
    int64_t utc_epoch,
    watchy_time_t *out_local);

#ifdef __cplusplus
}
#endif

#endif
