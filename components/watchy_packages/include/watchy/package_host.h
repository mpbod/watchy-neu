#ifndef WATCHY_PACKAGE_HOST_H
#define WATCHY_PACKAGE_HOST_H

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_PACKAGE_HOST_PATH_MAX 192u
#define WATCHY_PACKAGE_STATE_NODE_MAX 32u
#define WATCHY_PACKAGE_CALLBACK_BUDGET_MS 4000u

typedef struct {
    watchy_request_id_t id;
    uint32_t operation;
    watchy_async_status_t status;
    bool occupied;
} watchy_package_async_slot_t;

typedef struct {
    uint32_t capabilities;
    char package_root[WATCHY_PACKAGE_HOST_PATH_MAX];
    char state_root[WATCHY_PACKAGE_HOST_PATH_MAX];
    void *state_mutex;
    char traversal[WATCHY_PACKAGE_STATE_NODE_MAX][WATCHY_PACKAGE_HOST_PATH_MAX];
    watchy_canvas_t bound_canvas;
    size_t bound_canvas_bytes;
    bool canvas_acquired;
    watchy_request_id_t next_request_id;
    watchy_package_async_slot_t network_request;
    watchy_package_async_slot_t bluetooth_request;
    bool exit_requested;
    bool refresh_requested;
    watchy_refresh_mode_t refresh_mode;
    watchy_canvas_api_v1_t canvas;
    watchy_clock_api_v1_t clock;
    watchy_input_api_v1_t input;
    watchy_motion_api_v1_t motion;
    watchy_battery_api_v1_t battery;
    watchy_haptics_api_v1_t haptics;
    watchy_storage_api_v1_t storage;
    watchy_network_api_v1_t network;
    watchy_bluetooth_api_v1_t bluetooth;
    watchy_system_api_v1_t system;
    watchy_host_caps_v1_t host;
} watchy_package_host_context_t;

watchy_package_status_t watchy_package_host_init(watchy_package_host_context_t *context,
                                                 const watchy_package_manifest_t *manifest);
void watchy_package_host_deinit(watchy_package_host_context_t *context);
const watchy_host_caps_v1_t *watchy_package_host_caps(
    const watchy_package_host_context_t *context);
watchy_package_status_t watchy_package_host_pump(watchy_package_host_context_t *context);
bool watchy_package_host_take_exit(watchy_package_host_context_t *context);
bool watchy_package_host_take_refresh(watchy_package_host_context_t *context,
                                      watchy_refresh_mode_t *out_mode);

#ifdef __cplusplus
}
#endif

#endif
