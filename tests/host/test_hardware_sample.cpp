#include <cstdio>

#include "../../samples/hardware-demo/main/hardware_demo.cpp"

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

struct Probe {
    bool requests_supported;
    unsigned network_status_calls;
    unsigned bluetooth_status_calls;
};

static watchy_status_t unavailable_storage_read(void *, const char *, void *, uint32_t,
                                                uint32_t *) {
    return WATCHY_STATUS_UNSUPPORTED;
}

static watchy_status_t unavailable_storage_write(void *, const char *, const void *, uint32_t) {
    return WATCHY_STATUS_UNSUPPORTED;
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

static watchy_status_t unavailable_exit(void *) { return WATCHY_STATUS_UNSUPPORTED; }
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

int main() {
    Probe probe{};
    watchy_storage_api_v1_t storage = {nullptr, unavailable_storage_read, unavailable_storage_write};
    watchy_haptics_api_v1_t haptics = {nullptr, unavailable_pulse};
    watchy_network_api_v1_t network = {&probe, false_state, network_request, nullptr, network_status};
    watchy_bluetooth_api_v1_t bluetooth = {&probe, false_state, bluetooth_request, nullptr,
                                           bluetooth_status};
    watchy_system_api_v1_t system = {nullptr, nullptr, nullptr, nullptr, unavailable_exit, nullptr};
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

    auto event_value = pressed(WATCHY_BUTTON_CONFIRM);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    event_value = pressed(WATCHY_BUTTON_UP);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    event_value = pressed(WATCHY_BUTTON_DOWN);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    event_value = pressed(WATCHY_BUTTON_BACK);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);

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
