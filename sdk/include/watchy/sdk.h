#ifndef WATCHY_SDK_H
#define WATCHY_SDK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_ABI_V1_MAJOR 1u
#define WATCHY_ABI_V1_MINOR 1u

typedef enum {
    WATCHY_STATUS_OK = 0,
    WATCHY_STATUS_INVALID_ARGUMENT = -1,
    WATCHY_STATUS_INVALID_STATE = -2,
    WATCHY_STATUS_INCOMPATIBLE_ABI = -3,
    WATCHY_STATUS_UNSUPPORTED = -4
} watchy_status_t;

typedef enum {
    WATCHY_REFRESH_PARTIAL = 0,
    WATCHY_REFRESH_FULL = 1
} watchy_refresh_mode_t;

typedef enum {
    WATCHY_PIXEL_MONO = 0,
    WATCHY_PIXEL_GRAY4 = 1
} watchy_pixel_format_t;

typedef enum {
    WATCHY_BUTTON_UP = 0,
    WATCHY_BUTTON_DOWN = 1,
    WATCHY_BUTTON_CONFIRM = 2,
    WATCHY_BUTTON_BACK = 3
} watchy_button_t;

typedef enum {
    WATCHY_EVENT_NONE = 0,
    WATCHY_EVENT_TICK = 1,
    WATCHY_EVENT_BUTTON = 2,
    WATCHY_EVENT_MOTION = 3,
    WATCHY_EVENT_BATTERY = 4,
    WATCHY_EVENT_NETWORK = 5,
    WATCHY_EVENT_BLUETOOTH = 6,
    WATCHY_EVENT_SYSTEM = 7
} watchy_event_type_t;

/* ABI 1.1 asynchronous radio operations. Request IDs are host-generated and
 * remain valid until their terminal status has been observed or cancelled. */
typedef uint32_t watchy_request_id_t;

typedef enum {
    WATCHY_ASYNC_PENDING = 0,
    WATCHY_ASYNC_SUCCEEDED = 1,
    WATCHY_ASYNC_FAILED = 2,
    WATCHY_ASYNC_CANCELLED = 3
} watchy_async_state_t;

typedef struct {
    watchy_async_state_t state;
    watchy_status_t result;
} watchy_async_status_t;

typedef enum {
    WATCHY_NETWORK_CONNECT = 0,
    WATCHY_NETWORK_DISCONNECT = 1
} watchy_network_request_t;

typedef enum {
    WATCHY_BLUETOOTH_START = 0,
    WATCHY_BLUETOOTH_STOP = 1
} watchy_bluetooth_request_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
} watchy_abi_version_t;

typedef struct {
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    int16_t utc_offset_minutes;
} watchy_time_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint8_t rotation;
    watchy_pixel_format_t format;
    uint8_t *pixels;
} watchy_canvas_t;

typedef struct {
    watchy_button_t button;
    bool pressed;
} watchy_button_event_t;

typedef struct {
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
    bool shake;
} watchy_motion_sample_t;

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
    bool charging;
} watchy_battery_state_t;

typedef struct {
    watchy_event_type_t type;
    union {
        watchy_button_event_t button;
        watchy_motion_sample_t motion;
        watchy_battery_state_t battery;
        uint32_t tick_seconds;
        bool connected;
    } data;
} watchy_event_t;

typedef struct {
    void *context;
    watchy_canvas_t (*acquire)(void *context);
    void (*release)(void *context, const watchy_canvas_t *canvas);
    watchy_status_t (*request_refresh)(void *context, watchy_refresh_mode_t mode);
} watchy_canvas_api_v1_t;

typedef struct {
    void *context;
    watchy_status_t (*now)(void *context, watchy_time_t *out_time);
    /* PCF8563 next-match alarm: minute/hour/day/weekday are matched. The full
     * value must be valid; year/month/second/UTC-offset are otherwise ignored. */
    watchy_status_t (*set_alarm)(void *context, const watchy_time_t *alarm_time);
} watchy_clock_api_v1_t;

typedef struct {
    void *context;
    bool (*is_pressed)(void *context, watchy_button_t button);
} watchy_input_api_v1_t;

typedef struct {
    void *context;
    watchy_status_t (*sample)(void *context, watchy_motion_sample_t *out_sample);
} watchy_motion_api_v1_t;

typedef struct {
    void *context;
    watchy_status_t (*read)(void *context, watchy_battery_state_t *out_state);
} watchy_battery_api_v1_t;

typedef struct {
    void *context;
    watchy_status_t (*pulse)(void *context, uint16_t duration_ms, uint8_t strength);
} watchy_haptics_api_v1_t;

typedef struct {
    void *context;
    watchy_status_t (*read)(void *context,
                            const char *path,
                            void *buffer,
                            uint32_t buffer_size,
                            uint32_t *out_size);
    watchy_status_t (*write)(void *context, const char *path, const void *data, uint32_t data_size);
} watchy_storage_api_v1_t;

typedef struct {
    void *context;
    bool (*connected)(void *context);
    watchy_status_t (*request)(void *context,
                              watchy_network_request_t request,
                              watchy_request_id_t *out_request_id);
    watchy_status_t (*cancel)(void *context, watchy_request_id_t request_id);
    watchy_status_t (*status)(void *context,
                             watchy_request_id_t request_id,
                             watchy_async_status_t *out_status);
} watchy_network_api_v1_t;

typedef struct {
    void *context;
    bool (*enabled)(void *context);
    watchy_status_t (*request)(void *context,
                              watchy_bluetooth_request_t request,
                              watchy_request_id_t *out_request_id);
    watchy_status_t (*cancel)(void *context, watchy_request_id_t request_id);
    watchy_status_t (*status)(void *context,
                             watchy_request_id_t request_id,
                             watchy_async_status_t *out_status);
} watchy_bluetooth_api_v1_t;

typedef struct {
    void *context;
    uint32_t (*millis)(void *context);
    void (*sleep_ms)(void *context, uint32_t duration_ms);
    void (*log)(void *context, const char *message);
    watchy_status_t (*request_exit)(void *context);
    watchy_status_t (*request_refresh)(void *context, watchy_refresh_mode_t mode);
} watchy_system_api_v1_t;

typedef struct {
    watchy_abi_version_t abi;
    uint32_t size;
    const watchy_canvas_api_v1_t *canvas;
    const watchy_clock_api_v1_t *clock;
    const watchy_input_api_v1_t *input;
    const watchy_motion_api_v1_t *motion;
    const watchy_battery_api_v1_t *battery;
    const watchy_haptics_api_v1_t *haptics;
    const watchy_storage_api_v1_t *storage;
    const watchy_network_api_v1_t *network;
    const watchy_bluetooth_api_v1_t *bluetooth;
    const watchy_system_api_v1_t *system;
} watchy_host_caps_v1_t;

typedef struct {
    const char *identifier;
    const char *name;
    const char *version;
    watchy_abi_version_t abi;
    uint32_t flags;
} watchy_package_metadata_t;

typedef struct {
    watchy_status_t (*on_load)(const watchy_host_caps_v1_t *host, void **user_data);
    void (*on_unload)(void *user_data);
    watchy_status_t (*on_start)(void *user_data);
    void (*on_stop)(void *user_data);
    watchy_status_t (*on_event)(void *user_data, const watchy_event_t *event);
    watchy_status_t (*on_render)(void *user_data, watchy_canvas_t *canvas, watchy_refresh_mode_t *mode);
} watchy_package_callbacks_t;

typedef struct {
    uint32_t size;
    watchy_package_metadata_t metadata;
    watchy_package_callbacks_t callbacks;
} watchy_package_descriptor_v1_t;

typedef const watchy_package_descriptor_v1_t *(*watchy_package_entry_fn_t)(void);

#ifdef __cplusplus
}
#endif

#endif
