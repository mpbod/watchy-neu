#include "watchy/sdk.h"

static watchy_status_t smoke_load(const watchy_host_caps_v1_t *host, void **user_data) {
    if (host == 0 || user_data == 0 || host->size < sizeof(*host)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *user_data = 0;
    return WATCHY_STATUS_OK;
}

static void smoke_unload(void *user_data) { (void)user_data; }
static watchy_status_t smoke_start(void *user_data) { (void)user_data; return WATCHY_STATUS_OK; }
static void smoke_stop(void *user_data) { (void)user_data; }
static watchy_status_t smoke_event(void *user_data, const watchy_event_t *event) {
    (void)user_data;
    return event != 0 ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_ARGUMENT;
}
static watchy_status_t smoke_render(void *user_data,
                                   watchy_canvas_t *canvas,
                                   watchy_refresh_mode_t *mode) {
    (void)user_data;
    if (canvas == 0 || mode == 0) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *mode = WATCHY_REFRESH_PARTIAL;
    return WATCHY_STATUS_OK;
}

static const watchy_package_descriptor_v1_t descriptor = {
    .size = sizeof(watchy_package_descriptor_v1_t),
    .metadata = {
        .identifier = "smoke.fixture",
        .name = "Loader Smoke Fixture",
        .version = "1.0",
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .flags = 0u,
    },
    .callbacks = {
        .on_load = smoke_load,
        .on_unload = smoke_unload,
        .on_start = smoke_start,
        .on_stop = smoke_stop,
        .on_event = smoke_event,
        .on_render = smoke_render,
    },
};

__attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) {
    return &descriptor;
}
