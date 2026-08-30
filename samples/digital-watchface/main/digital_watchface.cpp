#include "watchy/sdk.hpp"
#include "drawing.hpp"

namespace {
struct State { const watchy_host_caps_v1_t *host; };
State state{};

watchy_status_t load(const watchy_host_caps_v1_t *host, void **user_data) noexcept {
    if (host == nullptr || user_data == nullptr || host->abi.major != WATCHY_ABI_V1_MAJOR ||
        host->abi.minor < WATCHY_ABI_V1_MINOR) return WATCHY_STATUS_INCOMPATIBLE_ABI;
    state.host = host;
    *user_data = &state;
    return WATCHY_STATUS_OK;
}
void unload(void *) noexcept { state.host = nullptr; }
watchy_status_t start(void *) noexcept { return WATCHY_STATUS_OK; }
void stop(void *) noexcept {}
watchy_status_t event(void *, const watchy_event_t *value) noexcept {
    return value != nullptr ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_ARGUMENT;
}

watchy_status_t render(void *opaque, watchy_canvas_t *canvas,
                       watchy_refresh_mode_t *mode) noexcept {
    auto *current = static_cast<State *>(opaque);
    if (current == nullptr || current->host == nullptr || canvas == nullptr || mode == nullptr)
        return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t now{};
    watchy_battery_state_t battery{};
    watchy::Clock clock(current->host->clock);
    watchy::Battery battery_api(current->host->battery);
    if (clock.now(&now) != WATCHY_STATUS_OK) return WATCHY_STATUS_UNSUPPORTED;
    sample::fill(canvas);
    sample::digit(canvas, 18, 55, now.hour / 10u);
    sample::digit(canvas, 55, 55, now.hour % 10u);
    sample::rect(canvas, 94, 70, 6, 6);
    sample::rect(canvas, 94, 91, 6, 6);
    sample::digit(canvas, 109, 55, now.minute / 10u);
    sample::digit(canvas, 146, 55, now.minute % 10u);
    sample::number(canvas, 54, 128, static_cast<unsigned>(now.month), 2);
    sample::number(canvas, 103, 128, static_cast<unsigned>(now.day), 2);
    if (battery_api.read(&battery) == WATCHY_STATUS_OK)
        sample::rect(canvas, 20, 180, static_cast<int>(battery.percent) * 16 / 10, 7);
    *mode = now.minute == 0u ? WATCHY_REFRESH_FULL : WATCHY_REFRESH_PARTIAL;
    return WATCHY_STATUS_OK;
}

watchy_package_descriptor_v1_t descriptor = {
    sizeof(watchy_package_descriptor_v1_t),
    {"sample.digital", "Digital", "1.0.0", {1u, 1u}, 0u},
    {load, unload, start, stop, event, render},
};
}  // namespace

extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry() noexcept { return &descriptor; }

extern "C" __attribute__((visibility("hidden"))) void app_main() {}
