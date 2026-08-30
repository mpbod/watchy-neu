#include "watchy/sdk.hpp"

namespace {
watchy_status_t on_load(const watchy_host_caps_v1_t *, void **state) noexcept {
    if (state == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *state = nullptr;
    return WATCHY_STATUS_OK;
}
void on_unload(void *) noexcept {}
watchy_status_t on_start(void *) noexcept { return WATCHY_STATUS_OK; }
void on_stop(void *) noexcept {}
watchy_status_t on_event(void *, const watchy_event_t *event) noexcept {
    return event != nullptr ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_ARGUMENT;
}
watchy_status_t on_render(void *, watchy_canvas_t *canvas, watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *mode = WATCHY_REFRESH_PARTIAL;
    return WATCHY_STATUS_OK;
}

watchy_package_descriptor_v1_t descriptor = {
    sizeof(watchy_package_descriptor_v1_t),
    {"example.package", "Example package", "1.0.0",
     {WATCHY_ABI_V1_MAJOR, WATCHY_ABI_V1_MINOR}, 0u},
    {on_load, on_unload, on_start, on_stop, on_event, on_render},
};
}  // namespace

extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry() noexcept {
    return &descriptor;
}

extern "C" __attribute__((visibility("hidden"))) void app_main() {}
