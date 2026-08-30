#include "watchy/sdk.hpp"
#include "drawing.hpp"

namespace {
struct State {
    const watchy_host_caps_v1_t *host;
    watchy_status_t clock_status;
    watchy_status_t battery_status;
    uint8_t previous_minute;
    bool previous_minute_valid;
    bool button_render_pending;
};
State state{};

watchy_status_t load(const watchy_host_caps_v1_t *host, void **user_data) noexcept {
    if (host == nullptr || user_data == nullptr || host->abi.major != WATCHY_ABI_V1_MAJOR ||
        host->abi.minor < 1u) return WATCHY_STATUS_INCOMPATIBLE_ABI;
    state = {};
    state.host = host;
    *user_data = &state;
    return WATCHY_STATUS_OK;
}
void unload(void *) noexcept { state.host = nullptr; }
watchy_status_t start(void *) noexcept { return WATCHY_STATUS_OK; }
void stop(void *) noexcept {}
watchy_status_t event(void *opaque, const watchy_event_t *value) noexcept {
    auto *current = static_cast<State *>(opaque);
    if (current == nullptr || value == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    if (value->type == WATCHY_EVENT_BUTTON && value->data.button.pressed) {
        current->button_render_pending = true;
    }
    return WATCHY_STATUS_OK;
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
    current->clock_status = clock.now(&now);
    current->battery_status = battery_api.read(&battery);
    const bool request_odometer = current->button_render_pending &&
                                  current->clock_status == WATCHY_STATUS_OK &&
                                  current->previous_minute_valid &&
                                  current->previous_minute != now.minute;
    current->button_render_pending = false;
    sample::fill(canvas);
    sample::status_box(canvas, 4, 4, current->clock_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 184, 4, current->battery_status == WATCHY_STATUS_OK);
    if (current->clock_status == WATCHY_STATUS_OK) {
        sample::digit(canvas, 18, 55, now.hour / 10u);
        sample::digit(canvas, 55, 55, now.hour % 10u);
        sample::rect(canvas, 94, 70, 6, 6);
        sample::rect(canvas, 94, 91, 6, 6);
        sample::digit(canvas, 109, 55, now.minute / 10u);
        sample::digit(canvas, 146, 55, now.minute % 10u);
        sample::number(canvas, 54, 128, static_cast<unsigned>(now.month), 2);
        sample::number(canvas, 103, 128, static_cast<unsigned>(now.day), 2);
    }
    if (current->battery_status == WATCHY_STATUS_OK)
        sample::rect(canvas, 20, 180, static_cast<int>(battery.percent) * 16 / 10, 7);
    if (request_odometer && current->host->abi.minor >= WATCHY_ABI_V1_MINOR &&
        current->host->system != nullptr) {
        watchy_transition_request_v1_t request{};
        request.size = sizeof(request);
        request.effect = WATCHY_TRANSITION_ODOMETER;
        request.direction = WATCHY_TRANSITION_DIRECTION_UP;
        request.rect = {146, 55, 30, 54};
        request.flags = WATCHY_TRANSITION_HAS_RECT;
        (void)watchy::System(current->host->system).request_transition(&request);
    }
    if (current->clock_status == WATCHY_STATUS_OK) {
        current->previous_minute = now.minute;
        current->previous_minute_valid = true;
    }
    *mode = current->clock_status == WATCHY_STATUS_OK && now.minute == 0u
                ? WATCHY_REFRESH_FULL
                : WATCHY_REFRESH_PARTIAL;
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
