#include <cstdio>

#include "../../samples/digital-watchface/main/digital_watchface.cpp"

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

struct Probe {
    watchy_time_t time{2026, 8u, 31u, 12u, 34u, 0u, 1u, 420};
    watchy_status_t request_status{WATCHY_STATUS_OK};
    watchy_transition_request_v1_t request{};
    unsigned request_calls{};
};

static watchy_status_t probe_clock(void *opaque, watchy_time_t *out_time) {
    *out_time = static_cast<Probe *>(opaque)->time;
    return WATCHY_STATUS_OK;
}

static watchy_status_t unavailable_clock(void *, watchy_time_t *) {
    return WATCHY_STATUS_UNSUPPORTED;
}

static watchy_status_t unavailable_battery(void *, watchy_battery_state_t *) {
    return WATCHY_STATUS_UNSUPPORTED;
}

static watchy_status_t capture_transition(void *opaque,
                                          const watchy_transition_request_v1_t *request) {
    auto *probe = static_cast<Probe *>(opaque);
    ++probe->request_calls;
    probe->request = *request;
    return probe->request_status;
}

static watchy_event_t pressed(watchy_button_t button) {
    watchy_event_t value{};
    value.type = WATCHY_EVENT_BUTTON;
    value.data.button = {button, true};
    return value;
}

static int exercise_sample(uint16_t abi_minor,
                           Probe *probe,
                           bool expect_requests,
                           bool transition_callback_available) {
    watchy_clock_api_v1_t clock = {probe, probe_clock, nullptr};
    watchy_battery_api_v1_t battery = {nullptr, unavailable_battery};
    watchy_system_api_v1_t system = {
        probe, nullptr, nullptr, nullptr, nullptr, nullptr,
        transition_callback_available ? capture_transition : nullptr,
    };
    watchy_host_caps_v1_t host{};
    unsigned char pixels[200u * 25u]{};
    watchy_canvas_t canvas = {200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, pixels};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_FULL;
    void *user_data = nullptr;

    host.abi = {WATCHY_ABI_V1_MAJOR, abi_minor};
    host.size = sizeof(host);
    host.clock = &clock;
    host.battery = &battery;
    host.system = &system;
    CHECK(load(&host, &user_data) == WATCHY_STATUS_OK);
    CHECK(start(user_data) == WATCHY_STATUS_OK);

    CHECK(render(user_data, &canvas, &mode) == WATCHY_STATUS_OK);
    CHECK(mode == WATCHY_REFRESH_PARTIAL || mode == WATCHY_REFRESH_FULL);
    CHECK(probe->request_calls == 0u);

    ++probe->time.minute;
    CHECK(render(user_data, &canvas, &mode) == WATCHY_STATUS_OK);
    CHECK(probe->request_calls == 0u);

    const watchy_event_t event_value = pressed(WATCHY_BUTTON_CONFIRM);
    CHECK(event(user_data, &event_value) == WATCHY_STATUS_OK);
    ++probe->time.minute;
    CHECK(render(user_data, &canvas, &mode) == WATCHY_STATUS_OK);
    CHECK(probe->request_calls == (expect_requests ? 1u : 0u));

    if (expect_requests) {
        const watchy_transition_request_v1_t expected = {
            sizeof(watchy_transition_request_v1_t),
            WATCHY_TRANSITION_ODOMETER,
            WATCHY_TRANSITION_DIRECTION_UP,
            {146, 55, 30, 54},
            WATCHY_TRANSITION_HAS_RECT,
            {0u, 0u},
        };
        CHECK(probe->request.size == expected.size);
        CHECK(probe->request.effect == expected.effect);
        CHECK(probe->request.direction == expected.direction);
        CHECK(probe->request.rect.x == expected.rect.x);
        CHECK(probe->request.rect.y == expected.rect.y);
        CHECK(probe->request.rect.width == expected.rect.width);
        CHECK(probe->request.rect.height == expected.rect.height);
        CHECK(probe->request.flags == expected.flags);
        CHECK(probe->request.reserved[0] == 0u);
        CHECK(probe->request.reserved[1] == 0u);
    }

    stop(user_data);
    unload(user_data);
    return 0;
}

static int exercise_service_failure() {
    watchy_clock_api_v1_t clock = {nullptr, unavailable_clock, nullptr};
    watchy_battery_api_v1_t battery = {nullptr, unavailable_battery};
    watchy_host_caps_v1_t host{};
    unsigned char pixels[200u * 25u]{};
    watchy_canvas_t canvas = {200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, pixels};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_FULL;
    void *user_data = nullptr;

    host.abi = {WATCHY_ABI_V1_MAJOR, WATCHY_ABI_V1_MINOR};
    host.size = sizeof(host);
    host.clock = &clock;
    host.battery = &battery;
    CHECK(load(&host, &user_data) == WATCHY_STATUS_OK);
    CHECK(start(user_data) == WATCHY_STATUS_OK);
    CHECK(render(user_data, &canvas, &mode) == WATCHY_STATUS_OK);
    CHECK(mode == WATCHY_REFRESH_PARTIAL);
    stop(user_data);
    unload(user_data);
    return 0;
}

int main() {
    Probe supported;
    CHECK(exercise_sample(WATCHY_ABI_V1_MINOR, &supported, true, true) == 0);

    Probe unsupported;
    unsupported.request_status = WATCHY_STATUS_UNSUPPORTED;
    CHECK(exercise_sample(WATCHY_ABI_V1_MINOR, &unsupported, true, true) == 0);

    Probe abi_v11;
    CHECK(exercise_sample(1u, &abi_v11, false, true) == 0);

    Probe abi_v12_without_transition_callback;
    CHECK(exercise_sample(WATCHY_ABI_V1_MINOR, &abi_v12_without_transition_callback,
                          false, false) == 0);
    CHECK(exercise_service_failure() == 0);

    std::puts("PASS digital sample optional attended transition request");
    return 0;
}
