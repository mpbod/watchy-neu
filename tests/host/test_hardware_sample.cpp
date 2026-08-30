#include <cstdio>
#include <cstring>

#include "watchy/package_host.h"
#include "watchy/package_runtime.h"

#include "../../samples/hardware-demo/main/hardware_demo.cpp"

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

struct Probe {
    bool requests_supported;
    bool asset_path_seen;
    bool state_read_path_seen;
    bool state_write_path_seen;
    unsigned exit_calls;
    unsigned network_status_calls;
    unsigned bluetooth_status_calls;
};

static watchy_status_t storage_read(void *opaque, const char *path, void *buffer,
                                    uint32_t capacity, uint32_t *out_size) {
    auto *probe = static_cast<Probe *>(opaque);
    char resolved[192];
    if (!watchy_package_resolve_storage_path("/data/packages/sample.hardware/1.0.0",
                                             "/data/state/sample.hardware", path, false,
                                             resolved, sizeof(resolved))) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (std::strcmp(resolved, "/data/packages/sample.hardware/1.0.0/probe.bin") == 0 &&
        capacity >= 5u) {
        probe->asset_path_seen = true;
        std::memcpy(buffer, "probe", 5u);
        *out_size = 5u;
        return WATCHY_STATUS_OK;
    }
    if (std::strcmp(resolved, "/data/state/sample.hardware/presses.bin") == 0 &&
        capacity >= sizeof(unsigned)) {
        const unsigned persisted = 7u;
        probe->state_read_path_seen = true;
        std::memcpy(buffer, &persisted, sizeof(persisted));
        *out_size = sizeof(persisted);
        return WATCHY_STATUS_OK;
    }
    return WATCHY_STATUS_INVALID_ARGUMENT;
}

static watchy_status_t storage_write(void *opaque, const char *path, const void *, uint32_t size) {
    auto *probe = static_cast<Probe *>(opaque);
    char resolved[192];
    probe->state_write_path_seen = watchy_package_resolve_storage_path(
        "/data/packages/sample.hardware/1.0.0", "/data/state/sample.hardware",
        path, true, resolved, sizeof(resolved)) &&
        std::strcmp(resolved, "/data/state/sample.hardware/presses.bin") == 0 &&
        size == sizeof(unsigned);
    return probe->state_write_path_seen ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_ARGUMENT;
}

static watchy_status_t unavailable_pulse(void *, uint16_t, uint8_t) {
    return WATCHY_STATUS_UNSUPPORTED;
}

static watchy_status_t network_request(void *opaque, watchy_network_request_t,
                                       watchy_request_id_t *request_id) {
    auto *probe = static_cast<Probe *>(opaque);
    if (!probe->requests_supported) return WATCHY_STATUS_UNSUPPORTED;
    *request_id = 41u;
    return WATCHY_STATUS_OK;
}

static watchy_status_t bluetooth_request(void *opaque, watchy_bluetooth_request_t,
                                         watchy_request_id_t *request_id) {
    auto *probe = static_cast<Probe *>(opaque);
    if (!probe->requests_supported) return WATCHY_STATUS_UNSUPPORTED;
    *request_id = 42u;
    return WATCHY_STATUS_OK;
}

static watchy_status_t network_status(void *opaque, watchy_request_id_t request_id,
                                      watchy_async_status_t *status) {
    auto *probe = static_cast<Probe *>(opaque);
    ++probe->network_status_calls;
    if (request_id != 41u) return WATCHY_STATUS_INVALID_ARGUMENT;
    status->state = WATCHY_ASYNC_SUCCEEDED;
    status->result = WATCHY_STATUS_OK;
    return WATCHY_STATUS_OK;
}

static watchy_status_t bluetooth_status(void *opaque, watchy_request_id_t request_id,
                                        watchy_async_status_t *status) {
    auto *probe = static_cast<Probe *>(opaque);
    ++probe->bluetooth_status_calls;
    if (request_id != 42u) return WATCHY_STATUS_INVALID_ARGUMENT;
    status->state = WATCHY_ASYNC_SUCCEEDED;
    status->result = WATCHY_STATUS_OK;
    return WATCHY_STATUS_OK;
}

static watchy_status_t request_exit(void *opaque) {
    ++static_cast<Probe *>(opaque)->exit_calls;
    return WATCHY_STATUS_OK;
}
static watchy_status_t unavailable_clock(void *, watchy_time_t *) { return WATCHY_STATUS_UNSUPPORTED; }
static watchy_status_t unavailable_motion(void *, watchy_motion_sample_t *) { return WATCHY_STATUS_UNSUPPORTED; }
static watchy_status_t unavailable_battery(void *, watchy_battery_state_t *) { return WATCHY_STATUS_UNSUPPORTED; }
static bool false_state(void *) { return false; }

static watchy_event_t pressed(watchy_button_t button) {
    watchy_event_t value{};
    value.type = WATCHY_EVENT_BUTTON;
    value.data.button = {button, true};
    return value;
}

struct SampleRunner {
    Probe *probe;
    void *user_data;
    watchy_canvas_t *canvas;
    watchy_refresh_mode_t *mode;
    bool active;
};

static watchy_package_status_t runner_event(void *opaque, const watchy_event_t *event_value) {
    auto *runner = static_cast<SampleRunner *>(opaque);
    const unsigned prior_exit_calls = runner->probe->exit_calls;
    if (event(runner->user_data, event_value) != WATCHY_STATUS_OK) {
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    if (runner->probe->exit_calls != prior_exit_calls) {
        runner->active = false;
    }
    return WATCHY_PACKAGE_OK;
}

static bool runner_active(void *opaque) {
    return static_cast<SampleRunner *>(opaque)->active;
}

static watchy_package_status_t runner_render(void *opaque) {
    auto *runner = static_cast<SampleRunner *>(opaque);
    return render(runner->user_data, runner->canvas, runner->mode) == WATCHY_STATUS_OK
               ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_CALLBACK;
}

static watchy_package_status_t runner_stop(void *opaque) {
    auto *runner = static_cast<SampleRunner *>(opaque);
    stop(runner->user_data);
    runner->active = false;
    return WATCHY_PACKAGE_OK;
}

int main() {
    Probe probe{};
    watchy_storage_api_v1_t storage = {&probe, storage_read, storage_write};
    watchy_haptics_api_v1_t haptics = {nullptr, unavailable_pulse};
    watchy_network_api_v1_t network = {&probe, false_state, network_request, nullptr, network_status};
    watchy_bluetooth_api_v1_t bluetooth = {&probe, false_state, bluetooth_request, nullptr,
                                           bluetooth_status};
    watchy_system_api_v1_t system = {&probe, nullptr, nullptr, nullptr, request_exit, nullptr};
    watchy_clock_api_v1_t clock = {nullptr, unavailable_clock, nullptr};
    watchy_motion_api_v1_t motion = {nullptr, unavailable_motion};
    watchy_battery_api_v1_t battery = {nullptr, unavailable_battery};
    watchy_host_caps_v1_t host{};
    unsigned char pixels[200u * 25u]{};
    watchy_canvas_t canvas = {200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, pixels};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_FULL;
    void *user_data = nullptr;

    host.abi = {WATCHY_ABI_V1_MAJOR, WATCHY_ABI_V1_MINOR};
    host.size = sizeof(host);
    host.clock = &clock;
    host.motion = &motion;
    host.battery = &battery;
    host.haptics = &haptics;
    host.storage = &storage;
    host.network = &network;
    host.bluetooth = &bluetooth;
    host.system = &system;
    CHECK(load(&host, &user_data) == WATCHY_STATUS_OK);
    CHECK(start(user_data) == WATCHY_STATUS_OK);
    CHECK(probe.asset_path_seen);
    CHECK(probe.state_read_path_seen);
    CHECK(state.presses == 7u);

    SampleRunner sample_runner = {&probe, user_data, &canvas, &mode, true};
    const watchy_package_app_runner_t runner = {
        runner_event, runner_active, runner_render, runner_stop, &sample_runner,
    };

    CHECK(watchy_package_dispatch_app_button(&runner, WATCHY_BUTTON_CONFIRM) ==
          WATCHY_PACKAGE_OK);
    auto event_value = pressed(WATCHY_BUTTON_UP);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    event_value = pressed(WATCHY_BUTTON_DOWN);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    CHECK(watchy_package_dispatch_app_button(&runner, WATCHY_BUTTON_BACK) ==
          WATCHY_PACKAGE_OK);
    CHECK(!sample_runner.active);
    CHECK(probe.exit_calls == 1u);
    CHECK(probe.state_write_path_seen);

    probe.requests_supported = true;
    event_value = pressed(WATCHY_BUTTON_UP);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    event_value = pressed(WATCHY_BUTTON_DOWN);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    CHECK(render(user_data, &canvas, &mode) == WATCHY_STATUS_OK);
    CHECK(probe.network_status_calls == 1u);
    CHECK(probe.bluetooth_status_calls == 1u);

    stop(user_data);
    unload(user_data);
    std::puts("PASS hardware sample graceful statuses and polling");
    return 0;
}
