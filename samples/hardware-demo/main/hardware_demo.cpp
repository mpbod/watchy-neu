#include "watchy/sdk.hpp"
#include "drawing.hpp"

namespace {
struct State {
    const watchy_host_caps_v1_t *host;
    watchy_request_id_t network_request;
    watchy_request_id_t bluetooth_request;
    watchy_status_t storage_status;
    watchy_status_t haptics_status;
    watchy_status_t network_request_status;
    watchy_status_t network_poll_status;
    watchy_async_status_t network_status;
    watchy_status_t bluetooth_request_status;
    watchy_status_t bluetooth_poll_status;
    watchy_async_status_t bluetooth_status;
    watchy_status_t exit_status;
    bool network_pending;
    bool bluetooth_pending;
    unsigned presses;
    bool asset_read;
};
State state{};

watchy_status_t load(const watchy_host_caps_v1_t *host, void **user_data) noexcept {
    if (host == nullptr || user_data == nullptr || host->abi.major != 1u || host->abi.minor < 1u)
        return WATCHY_STATUS_INCOMPATIBLE_ABI;
    state = {};
    state.host = host;
    state.storage_status = WATCHY_STATUS_UNSUPPORTED;
    state.haptics_status = WATCHY_STATUS_UNSUPPORTED;
    state.network_request_status = WATCHY_STATUS_UNSUPPORTED;
    state.network_poll_status = WATCHY_STATUS_UNSUPPORTED;
    state.bluetooth_request_status = WATCHY_STATUS_UNSUPPORTED;
    state.bluetooth_poll_status = WATCHY_STATUS_UNSUPPORTED;
    state.exit_status = WATCHY_STATUS_UNSUPPORTED;
    *user_data = &state;
    return WATCHY_STATUS_OK;
}
void unload(void *) noexcept { state = {}; }
watchy_status_t start(void *opaque) noexcept {
    auto *current = static_cast<State *>(opaque);
    if (current == nullptr || current->host == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy::Storage storage(current->host->storage);
    unsigned char probe[8]{};
    uint32_t received = 0;
    current->asset_read = storage.read("assets/probe.bin", probe, sizeof(probe), &received) == WATCHY_STATUS_OK && received == 5u;
    unsigned persisted = 0;
    if (storage.read("presses.bin", &persisted, sizeof(persisted), &received) == WATCHY_STATUS_OK &&
        received == sizeof(persisted)) current->presses = persisted;
    return WATCHY_STATUS_OK;
}
void stop(void *) noexcept {}

watchy_status_t event(void *opaque, const watchy_event_t *event_value) noexcept {
    auto *current = static_cast<State *>(opaque);
    if (current == nullptr || current->host == nullptr || event_value == nullptr)
        return WATCHY_STATUS_INVALID_ARGUMENT;
    if (event_value->type != WATCHY_EVENT_BUTTON || !event_value->data.button.pressed)
        return WATCHY_STATUS_OK;
    ++current->presses;
    current->storage_status = watchy::Storage(current->host->storage).write(
        "presses.bin", &current->presses, sizeof(current->presses));
    switch (event_value->data.button.button) {
    case WATCHY_BUTTON_CONFIRM:
        current->haptics_status = watchy::Haptics(current->host->haptics).pulse(80u, 180u);
        break;
    case WATCHY_BUTTON_UP:
        current->network_request_status = watchy::Network(current->host->network).request(
            WATCHY_NETWORK_CONNECT, &current->network_request);
        current->network_pending = current->network_request_status == WATCHY_STATUS_OK;
        break;
    case WATCHY_BUTTON_DOWN:
        current->bluetooth_request_status = watchy::Bluetooth(current->host->bluetooth).request(
            WATCHY_BLUETOOTH_START, &current->bluetooth_request);
        current->bluetooth_pending = current->bluetooth_request_status == WATCHY_STATUS_OK;
        break;
    case WATCHY_BUTTON_BACK:
        current->exit_status = watchy::System(current->host->system).request_exit();
        break;
    default:
        return WATCHY_STATUS_OK;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t render(void *opaque, watchy_canvas_t *canvas, watchy_refresh_mode_t *mode) noexcept {
    auto *current = static_cast<State *>(opaque);
    if (current == nullptr || current->host == nullptr || canvas == nullptr || mode == nullptr)
        return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    watchy_motion_sample_t motion{};
    watchy_battery_state_t battery{};
    const auto clock_status = watchy::Clock(current->host->clock).now(&time);
    const auto motion_status = watchy::Motion(current->host->motion).sample(&motion);
    const auto battery_status = watchy::Battery(current->host->battery).read(&battery);
    watchy::Network network(current->host->network);
    watchy::Bluetooth bluetooth(current->host->bluetooth);
    watchy::Input input(current->host->input);
    if (current->network_pending) {
        current->network_poll_status = network.status(current->network_request,
                                                       &current->network_status);
        if (current->network_poll_status != WATCHY_STATUS_OK ||
            current->network_status.state != WATCHY_ASYNC_PENDING)
            current->network_pending = false;
    }
    if (current->bluetooth_pending) {
        current->bluetooth_poll_status = bluetooth.status(current->bluetooth_request,
                                                           &current->bluetooth_status);
        if (current->bluetooth_poll_status != WATCHY_STATUS_OK ||
            current->bluetooth_status.state != WATCHY_ASYNC_PENDING)
            current->bluetooth_pending = false;
    }
    sample::fill(canvas);
    sample::status_box(canvas, 12, 12, clock_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 12, 34, motion_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 12, 56, battery_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 12, 78, current->asset_read);
    sample::status_box(canvas, 12, 100, network.connected());
    sample::status_box(canvas, 12, 122, bluetooth.enabled());
    sample::status_box(canvas, 12, 144, current->storage_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 12, 166, current->haptics_status == WATCHY_STATUS_OK);
    sample::status_box(canvas, 164, 100,
                       current->network_request_status == WATCHY_STATUS_OK &&
                       (current->network_pending ||
                        (current->network_poll_status == WATCHY_STATUS_OK &&
                         current->network_status.result == WATCHY_STATUS_OK)));
    sample::status_box(canvas, 164, 122,
                       current->bluetooth_request_status == WATCHY_STATUS_OK &&
                       (current->bluetooth_pending ||
                        (current->bluetooth_poll_status == WATCHY_STATUS_OK &&
                         current->bluetooth_status.result == WATCHY_STATUS_OK)));
    sample::status_box(canvas, 164, 144, current->exit_status == WATCHY_STATUS_OK);
    for (unsigned button = 0; button < 4; ++button)
        sample::status_box(canvas, 44 + static_cast<int>(button) * 24, 12,
                           input.is_pressed(static_cast<watchy_button_t>(button)));
    if (clock_status == WATCHY_STATUS_OK) {
        sample::number(canvas, 43, 40, time.hour, 2);
        sample::number(canvas, 91, 40, time.minute, 2);
    }
    if (battery_status == WATCHY_STATUS_OK)
        sample::number(canvas, 44, 82, battery.percent, 3);
    if (motion_status == WATCHY_STATUS_OK) {
        sample::number(canvas, 44, 124, static_cast<unsigned>(motion.x_mg < 0 ? -motion.x_mg : motion.x_mg) % 1000u, 3);
        sample::status_box(canvas, 116, 124, motion.shake);
    }
    sample::number(canvas, 44, 164, current->presses % 1000u, 3);
    *mode = WATCHY_REFRESH_PARTIAL;
    return WATCHY_STATUS_OK;
}

watchy_package_descriptor_v1_t descriptor = {
    sizeof(watchy_package_descriptor_v1_t),
    {"sample.hardware", "Hardware Demo", "1.0.0", {1u, 1u}, 0u},
    {load, unload, start, stop, event, render},
};
}  // namespace

extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry() noexcept { return &descriptor; }

extern "C" __attribute__((visibility("hidden"))) void app_main() {}
