#include "watchy/radios.h"
#include "watchy/wifi_credentials.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

#define WATCHY_BLE_ADV_STOPPED_BIT BIT0
#define WATCHY_BLE_STOP_TIMEOUT_MS 1000

static const char *TAG = "watchy_radios";

static watchy_wifi_state_t s_wifi_state;
static esp_netif_t *s_wifi_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_initialized;
static bool s_wifi_started;
static watchy_ble_state_t s_ble_state;
static bool s_classic_memory_released;
static bool s_ble_controller_initialized;
static bool s_ble_controller_enabled;
static bool s_bluedroid_initialized;
static bool s_bluedroid_enabled;
static EventGroupHandle_t s_ble_events;

static void record_error(watchy_status_t *result, esp_err_t error) {
    if (error != ESP_OK) {
        *result = WATCHY_STATUS_INVALID_STATE;
    }
}

static bool operation_succeeded(watchy_status_t *result, esp_err_t error) {
    record_error(result, error);
    return error == ESP_OK;
}

static void wifi_event(void *argument, esp_event_base_t base, int32_t event, void *data) {
    (void)argument;
    (void)data;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED &&
        watchy_wifi_should_reconnect(s_wifi_state)) {
        s_wifi_state = WATCHY_WIFI_STA_STARTING;
        esp_wifi_connect();
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP &&
               s_wifi_state != WATCHY_WIFI_STOPPING) {
        s_wifi_state = WATCHY_WIFI_STA_CONNECTED;
    }
}

static watchy_status_t wifi_initialize(wifi_mode_t mode) {
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t error;

    if (s_wifi_state != WATCHY_WIFI_STOPPED) {
        ESP_LOGE(TAG, "wifi init rejected: state=%d", (int)s_wifi_state);
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(error));
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(error));
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_wifi_netif = mode == WIFI_MODE_AP ? esp_netif_create_default_wifi_ap()
                                        : esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "default netif create failed mode=%d", (int)mode);
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_wifi_init(&init_config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_wifi_initialized = true;
    s_wifi_state = WATCHY_WIFI_INITIALIZED;
    error = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL,
                                                &s_wifi_handler);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi event handler registration failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL,
                                                &s_ip_handler);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "IP event handler registration failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_wifi_set_mode(mode);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed mode=%d: %s", (int)mode, esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

static bool valid_string(const char *value, size_t capacity, size_t minimum) {
    const size_t length = value == NULL ? 0 : strnlen(value, capacity);
    return length >= minimum && length < capacity;
}

static watchy_status_t persist_sta(const watchy_wifi_sta_config_t *config) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WATCHY_WIFI_CREDENTIAL_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_str(handle, WATCHY_WIFI_CREDENTIAL_SSID_KEY, config->ssid);
    }
    if (error == ESP_OK) {
        error = nvs_set_str(handle, WATCHY_WIFI_CREDENTIAL_PASSWORD_KEY, config->password);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (error == ESP_OK || handle != 0) {
        nvs_close(handle);
    }
    return error == ESP_OK ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_wifi_start_sta(const watchy_wifi_sta_config_t *config, bool persist) {
    wifi_config_t wifi_config = {0};
    if (config == NULL || !valid_string(config->ssid, sizeof(config->ssid), 1) ||
        !valid_string(config->password, sizeof(config->password), 0)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (wifi_initialize(WIFI_MODE_STA) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    memcpy(wifi_config.sta.ssid, config->ssid, strnlen(config->ssid, sizeof(config->ssid)));
    memcpy(wifi_config.sta.password, config->password,
           strnlen(config->password, sizeof(config->password)));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK || esp_wifi_start() != ESP_OK) {
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_wifi_started = true;
    s_wifi_state = WATCHY_WIFI_STA_STARTING;
    if (esp_wifi_connect() != ESP_OK) {
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (persist && persist_sta(config) != WATCHY_STATUS_OK) {
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_wifi_start_stored_sta(void) {
    watchy_wifi_sta_config_t config = {0};
    nvs_handle_t handle = 0;
    size_t ssid_size = sizeof(config.ssid);
    size_t password_size = sizeof(config.password);
    esp_err_t error = nvs_open(WATCHY_WIFI_CREDENTIAL_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_str(handle, WATCHY_WIFI_CREDENTIAL_SSID_KEY, config.ssid, &ssid_size);
    }
    if (error == ESP_OK) {
        error = nvs_get_str(handle, WATCHY_WIFI_CREDENTIAL_PASSWORD_KEY,
                            config.password, &password_size);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return error == ESP_OK ? watchy_wifi_start_sta(&config, false) : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_wifi_start_ap(const watchy_wifi_ap_config_t *config) {
    wifi_config_t wifi_config = {0};
    const size_t password_length = config == NULL ? 0 : strnlen(config->password, sizeof(config->password));
    if (config == NULL || !valid_string(config->ssid, sizeof(config->ssid), 1) ||
        config->channel < 1 || config->channel > 13 ||
        (password_length != 0 && (password_length < 8 || password_length >= sizeof(config->password)))) {
        ESP_LOGE(TAG, "AP config rejected ssid_len=%u password_len=%u channel=%u",
                 config == NULL ? 0u : (unsigned)strnlen(config->ssid, sizeof(config->ssid)),
                 (unsigned)password_length, config == NULL ? 0u : (unsigned)config->channel);
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (wifi_initialize(WIFI_MODE_AP) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    memcpy(wifi_config.ap.ssid, config->ssid, strnlen(config->ssid, sizeof(config->ssid)));
    wifi_config.ap.ssid_len = (uint8_t)strnlen(config->ssid, sizeof(config->ssid));
    memcpy(wifi_config.ap.password, config->password, password_length);
    wifi_config.ap.channel = config->channel;
    wifi_config.ap.max_connection = 2;
    wifi_config.ap.authmode = password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    esp_err_t error = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = esp_wifi_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start(AP) failed: %s", esp_err_to_name(error));
        watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_wifi_started = true;
    s_wifi_state = WATCHY_WIFI_AP_RUNNING;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_wifi_set_captive_portal_uri(const char *uri) {
    esp_err_t option_error;
    esp_err_t start_error;

    if (uri == NULL || uri[0] == '\0') {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (s_wifi_state != WATCHY_WIFI_AP_RUNNING || s_wifi_netif == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (esp_netif_dhcps_stop(s_wifi_netif) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    option_error = esp_netif_dhcps_option(
        s_wifi_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
        (void *)uri, (uint32_t)strlen(uri));
    start_error = esp_netif_dhcps_start(s_wifi_netif);
    return option_error == ESP_OK && start_error == ESP_OK
               ? WATCHY_STATUS_OK
               : WATCHY_STATUS_INVALID_STATE;
}

watchy_wifi_state_t watchy_wifi_state(void) {
    return s_wifi_state;
}

watchy_status_t watchy_wifi_stop(void) {
    watchy_status_t result = WATCHY_STATUS_OK;
    const watchy_wifi_state_t previous_state = s_wifi_state;
    const bool had_resources = s_wifi_initialized || s_wifi_netif != NULL || s_wifi_handler != NULL ||
                               s_ip_handler != NULL;
    if (!had_resources && s_wifi_state == WATCHY_WIFI_STOPPED) {
        return WATCHY_STATUS_OK;
    }
    s_wifi_state = WATCHY_WIFI_STOPPING;
    if (s_wifi_started && (previous_state == WATCHY_WIFI_STA_STARTING ||
                           previous_state == WATCHY_WIFI_STA_CONNECTED)) {
        record_error(&result, esp_wifi_disconnect());
    }
    if (s_wifi_started) {
        if (operation_succeeded(&result, esp_wifi_stop())) {
            s_wifi_started = false;
        }
    }
    if (s_wifi_handler != NULL) {
        if (operation_succeeded(&result, esp_event_handler_instance_unregister(
                                             WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler))) {
            s_wifi_handler = NULL;
        }
    }
    if (s_ip_handler != NULL) {
        if (operation_succeeded(&result, esp_event_handler_instance_unregister(
                                             IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler))) {
            s_ip_handler = NULL;
        }
    }
    if (s_wifi_initialized) {
        if (operation_succeeded(&result, esp_wifi_deinit())) {
            s_wifi_initialized = false;
        }
    }
    if (!s_wifi_initialized && s_wifi_netif != NULL) {
        esp_netif_destroy_default_wifi(s_wifi_netif);
        s_wifi_netif = NULL;
    }
    if (s_wifi_started || s_wifi_initialized || s_wifi_handler != NULL || s_ip_handler != NULL ||
        s_wifi_netif != NULL) {
        result = WATCHY_STATUS_INVALID_STATE;
        s_wifi_state = WATCHY_WIFI_STOPPING;
    } else {
        s_wifi_state = WATCHY_WIFI_STOPPED;
    }
    return result;
}

static esp_ble_adv_params_t DIAGNOSTIC_ADV_PARAMS = {
    .adv_int_min = 0x40,
    .adv_int_max = 0x80,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *parameter) {
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT && parameter->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS &&
        s_ble_state != WATCHY_BLE_STOPPING &&
        esp_ble_gap_start_advertising(&DIAGNOSTIC_ADV_PARAMS) == ESP_OK) {
        s_ble_state = WATCHY_BLE_DIAGNOSTICS_ADVERTISING;
    } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
        if (s_ble_events != NULL) {
            xEventGroupSetBits(s_ble_events, WATCHY_BLE_ADV_STOPPED_BIT);
        }
        if (s_ble_state != WATCHY_BLE_STOPPED && s_ble_state != WATCHY_BLE_STOPPING) {
            s_ble_state = WATCHY_BLE_HOST_RUNNING;
        }
    }
}

watchy_status_t watchy_ble_start(void) {
    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (s_ble_state == WATCHY_BLE_HOST_RUNNING ||
        s_ble_state == WATCHY_BLE_DIAGNOSTICS_ADVERTISING) {
        return WATCHY_STATUS_OK;
    }
    if (s_ble_state != WATCHY_BLE_STOPPED) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ble_events = xEventGroupCreate();
    if (s_ble_events == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!s_classic_memory_released) {
        if (esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
            watchy_ble_stop();
            return WATCHY_STATUS_INVALID_STATE;
        }
        s_classic_memory_released = true;
    }
    if (esp_bt_controller_init(&controller_config) != ESP_OK) {
        watchy_ble_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ble_controller_initialized = true;
    s_ble_state = WATCHY_BLE_INITIALIZED;
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
        watchy_ble_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ble_controller_enabled = true;
    if (esp_bluedroid_init() != ESP_OK) {
        watchy_ble_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_bluedroid_initialized = true;
    if (esp_bluedroid_enable() != ESP_OK) {
        watchy_ble_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_bluedroid_enabled = true;
    if (esp_ble_gap_register_callback(gap_event) != ESP_OK) {
        watchy_ble_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ble_state = WATCHY_BLE_HOST_RUNNING;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_ble_start_diagnostics_advertising(void) {
    esp_ble_adv_data_t advertising = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = false,
        .min_interval = 0,
        .max_interval = 0,
        .appearance = 0,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 0,
        .p_service_uuid = NULL,
        .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    };
    if (s_ble_state == WATCHY_BLE_STOPPED && watchy_ble_start() != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (s_ble_state != WATCHY_BLE_HOST_RUNNING) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (esp_ble_gap_set_device_name("Watchy-Diag") != ESP_OK ||
        esp_ble_gap_config_adv_data(&advertising) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_ble_state_t watchy_ble_state(void) {
    return s_ble_state;
}

watchy_status_t watchy_ble_stop(void) {
    watchy_status_t result = WATCHY_STATUS_OK;
    const watchy_ble_state_t previous_state = s_ble_state;
    if (previous_state == WATCHY_BLE_STOPPED && !s_ble_controller_initialized &&
        !s_bluedroid_initialized && s_ble_events == NULL) {
        return WATCHY_STATUS_OK;
    }
    s_ble_state = WATCHY_BLE_STOPPING;
    if (previous_state == WATCHY_BLE_DIAGNOSTICS_ADVERTISING) {
        xEventGroupClearBits(s_ble_events, WATCHY_BLE_ADV_STOPPED_BIT);
        if (esp_ble_gap_stop_advertising() != ESP_OK ||
            (xEventGroupWaitBits(s_ble_events, WATCHY_BLE_ADV_STOPPED_BIT, pdTRUE, pdTRUE,
                                 pdMS_TO_TICKS(WATCHY_BLE_STOP_TIMEOUT_MS)) &
             WATCHY_BLE_ADV_STOPPED_BIT) == 0u) {
            result = WATCHY_STATUS_INVALID_STATE;
        }
    }
    if (s_bluedroid_enabled) {
        if (operation_succeeded(&result, esp_bluedroid_disable())) {
            s_bluedroid_enabled = false;
        }
    }
    if (s_bluedroid_initialized) {
        if (operation_succeeded(&result, esp_bluedroid_deinit())) {
            s_bluedroid_initialized = false;
        }
    }
    if (s_ble_controller_enabled) {
        if (operation_succeeded(&result, esp_bt_controller_disable())) {
            s_ble_controller_enabled = false;
        }
    }
    if (s_ble_controller_initialized) {
        if (operation_succeeded(&result, esp_bt_controller_deinit())) {
            s_ble_controller_initialized = false;
        }
    }
    if (!s_bluedroid_enabled && !s_bluedroid_initialized && !s_ble_controller_enabled &&
        !s_ble_controller_initialized && s_ble_events != NULL) {
        vEventGroupDelete(s_ble_events);
        s_ble_events = NULL;
    }
    if (s_bluedroid_enabled || s_bluedroid_initialized || s_ble_controller_enabled ||
        s_ble_controller_initialized || s_ble_events != NULL) {
        result = WATCHY_STATUS_INVALID_STATE;
        s_ble_state = WATCHY_BLE_STOPPING;
    } else {
        s_ble_state = WATCHY_BLE_STOPPED;
    }
    return result;
}

watchy_status_t watchy_radios_stop_all(void) {
    const watchy_status_t wifi = watchy_wifi_stop();
    const watchy_status_t ble = watchy_ble_stop();
    return wifi == WATCHY_STATUS_OK && ble == WATCHY_STATUS_OK ? WATCHY_STATUS_OK
                                                               : WATCHY_STATUS_INVALID_STATE;
}
