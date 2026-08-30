#include <cstdio>

#include "../../samples/digital-watchface/main/digital_watchface.cpp"

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static watchy_status_t unavailable_clock(void *, watchy_time_t *) {
    return WATCHY_STATUS_UNSUPPORTED;
}

static watchy_status_t unavailable_battery(void *, watchy_battery_state_t *) {
    return WATCHY_STATUS_UNSUPPORTED;
}

int main() {
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
    CHECK(mode == WATCHY_REFRESH_PARTIAL || mode == WATCHY_REFRESH_FULL);
    stop(user_data);
    unload(user_data);
    std::puts("PASS digital sample graceful service failure");
    return 0;
}
