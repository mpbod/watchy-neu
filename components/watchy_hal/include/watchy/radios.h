#ifndef WATCHY_RADIOS_H
#define WATCHY_RADIOS_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_WIFI_STOPPED = 0,
    WATCHY_WIFI_INITIALIZED,
    WATCHY_WIFI_STA_STARTING,
    WATCHY_WIFI_STA_CONNECTED,
    WATCHY_WIFI_AP_RUNNING,
    WATCHY_WIFI_STOPPING,
} watchy_wifi_state_t;

typedef struct {
    char ssid[33];
    char password[65];
} watchy_wifi_sta_config_t;

typedef struct {
    char ssid[33];
    char password[65];
    uint8_t channel;
} watchy_wifi_ap_config_t;

typedef enum {
    WATCHY_BLE_STOPPED = 0,
    WATCHY_BLE_INITIALIZED,
    WATCHY_BLE_HOST_RUNNING,
    WATCHY_BLE_DIAGNOSTICS_ADVERTISING,
    WATCHY_BLE_STOPPING,
} watchy_ble_state_t;

watchy_status_t watchy_wifi_start_sta(const watchy_wifi_sta_config_t *config, bool persist);
watchy_status_t watchy_wifi_start_stored_sta(void);
watchy_status_t watchy_wifi_start_ap(const watchy_wifi_ap_config_t *config);
watchy_status_t watchy_wifi_set_captive_portal_uri(const char *uri);
watchy_wifi_state_t watchy_wifi_state(void);
watchy_status_t watchy_wifi_stop(void);

watchy_status_t watchy_ble_start(void);
watchy_status_t watchy_ble_start_diagnostics_advertising(void);
watchy_ble_state_t watchy_ble_state(void);
watchy_status_t watchy_ble_stop(void);
bool watchy_wifi_should_reconnect(watchy_wifi_state_t state);
watchy_status_t watchy_radios_stop_all(void);

#ifdef __cplusplus
}
#endif

#endif
