#ifndef WATCHY_PACKAGE_HOST_H
#define WATCHY_PACKAGE_HOST_H

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_PACKAGE_HOST_PATH_MAX 192u

typedef struct {
    uint32_t capabilities;
    char package_root[WATCHY_PACKAGE_HOST_PATH_MAX];
    char state_root[WATCHY_PACKAGE_HOST_PATH_MAX];
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
const watchy_host_caps_v1_t *watchy_package_host_caps(
    const watchy_package_host_context_t *context);

#ifdef __cplusplus
}
#endif

#endif
