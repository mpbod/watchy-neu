#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"

using namespace watchy_first_party;

#ifndef WATCHY_FACE_GOLDEN_DIR
#define WATCHY_FACE_GOLDEN_DIR "tests/golden/faces"
#endif

extern "C" watchy_status_t grid01_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t grid02_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t grid03_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" const watchy_package_descriptor_v1_t *grid01_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *grid02_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *grid03_package_entry(void);

static watchy_status_t render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) { return WATCHY_STATUS_OK; }
WATCHY_FIRST_PARTY_FACE_NAMED(test_package_entry, "watchy.test.face", "Test Face", 0x203u, render)

static bool black(const uint8_t *fb, int x, int y) {
    if (x < 0 || y < 0 || x >= 200 || y >= 200) return false;
    return (fb[static_cast<size_t>(y) * 25u + static_cast<size_t>(x) / 8u] &
            static_cast<uint8_t>(0x80u >> (x & 7))) == 0u;
}

static int ink(const uint8_t *fb, int left, int top, int right, int bottom) {
    int count = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) count += black(fb, x, y) ? 1 : 0;
    }
    return count;
}

static int read_pbm(const char *path, const uint8_t *fb) {
    FILE *file = std::fopen(path, "rb");
    char header[32]{};
    if (file == nullptr || std::fgets(header, sizeof(header), file) == nullptr ||
        std::strcmp(header, "P4\n") != 0 || std::fgets(header, sizeof(header), file) == nullptr ||
        std::strcmp(header, "200 200\n") != 0) {
        if (file != nullptr) std::fclose(file);
        return 1;
    }
    uint8_t expected[5000]{};
    const bool read = std::fread(expected, 1u, sizeof(expected), file) == sizeof(expected) &&
                      std::fgetc(file) == EOF;
    std::fclose(file);
    if (!read) return 1;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        if (expected[i] != static_cast<uint8_t>(~fb[i])) return 1;
    }
    return 0;
}

static void write_pbm(const char *path, const uint8_t *fb) {
    FILE *file = std::fopen(path, "wb");
    assert(file != nullptr);
    std::fputs("P4\n200 200\n", file);
    for (size_t i = 0; i < 5000u; ++i) {
        const uint8_t byte = static_cast<uint8_t>(~fb[i]);
        std::fwrite(&byte, 1u, 1u, file);
    }
    std::fclose(file);
}

struct host_fixture {
    watchy_time_t time{2026, 8, 31, 9, 41, 0, 1, 420};
    watchy_battery_state_t battery{3820, 68, false};
    bool bluetooth = false;
};

static watchy_status_t fixture_now(void *context, watchy_time_t *out) {
    if (context == nullptr || out == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *out = static_cast<host_fixture *>(context)->time;
    return WATCHY_STATUS_OK;
}

static watchy_status_t fixture_battery(void *context, watchy_battery_state_t *out) {
    if (context == nullptr || out == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *out = static_cast<host_fixture *>(context)->battery;
    return WATCHY_STATUS_OK;
}

static bool fixture_bt(void *context) {
    return context != nullptr && static_cast<host_fixture *>(context)->bluetooth;
}

static watchy_host_caps_v1_t fixture_caps(host_fixture *fixture) {
    static watchy_clock_api_v1_t clock{};
    static watchy_battery_api_v1_t battery{};
    static watchy_bluetooth_api_v1_t bluetooth{};
    clock = {fixture, fixture_now, nullptr};
    battery = {fixture, fixture_battery};
    bluetooth = {fixture, fixture_bt, nullptr, nullptr, nullptr};
    watchy_host_caps_v1_t caps{};
    caps.abi = {1u, 2u};
    caps.size = sizeof(caps);
    caps.clock = &clock;
    caps.battery = &battery;
    caps.bluetooth = &bluetooth;
    return caps;
}

static int test_grid(const watchy_package_descriptor_v1_t *descriptor,
                     watchy_status_t (*renderer)(void *, watchy_canvas_t *, watchy_refresh_mode_t *),
                     const char *golden,
                     uint32_t capabilities,
                     int face) {
    assert(descriptor != nullptr);
    assert(descriptor->size == sizeof(*descriptor));
    assert(descriptor->metadata.abi.major == 1u && descriptor->metadata.abi.minor == 2u);
    assert(descriptor->metadata.version != nullptr && std::strcmp(descriptor->metadata.version, "1.0.0") == 0);
    assert(descriptor->metadata.flags == capabilities);
    host_fixture fixture;
    watchy_host_caps_v1_t caps = fixture_caps(&fixture);
    void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    uint8_t framebuffer[5000];
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    watchy_canvas_t canvas{200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, framebuffer};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    assert(ink(framebuffer, 0, 0, 199, 199) > 80);
    if (face == 1) {
        assert(ink(framebuffer, 0, 0, 199, 22) > 5);
        assert(ink(framebuffer, 8, 154, 191, 198) > 50);
    } else if (face == 2) {
        assert(ink(framebuffer, 0, 0, 31, 199) > 50);
        assert(ink(framebuffer, 40, 28, 190, 92) > 20);
    } else {
        assert(ink(framebuffer, 38, 0, 195, 82) > 80);
        assert(ink(framebuffer, 8, 88, 192, 190) > 40);
    }
    if (std::getenv("WATCHY_UPDATE_GOLDENS") != nullptr) {
        write_pbm(golden, framebuffer);
    } else {
        assert(read_pbm(golden, framebuffer) == 0);
    }
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_PARTIAL);
    fixture.time.hour = 10u;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    descriptor->callbacks.on_stop(user);
    descriptor->callbacks.on_unload(user);
    return 0;
}

int main() {
    watchy_time_t value{2026, 8, 31, 9, 41, 0, 1, 420};
    assert(format_hhmm(value, true) == fixed_text("09:41"));
    assert(format_hhmm(value, false) == fixed_text("09:41"));
    assert(format_hhmm(watchy_time_t{2026, 8, 31, 0, 0, 0, 1, 420}, true) == fixed_text("12:00"));
    assert(format_hhmm(watchy_time_t{2026, 8, 31, 12, 0, 0, 1, 420}, true) == fixed_text("12:00"));
    assert(offset_time(value, 540).hour == 11);
    assert(moon_octant(value) < 8u);
    assert(moon_octant(watchy_time_t{2000, 1, 6, 18, 14, 0, 4, 0}) == 0u);
    assert(moon_octant(watchy_time_t{2000, 1, 6, 18, 13, 0, 4, 0}) == 7u);
    assert(moon_octant(watchy_time_t{2000, 1, 7, 0, 0, 0, 5, 0}) == 0u);
    assert(moon_octant(watchy_time_t{2000, 1, 7, 0, 0, 0, 5, 60}) == 0u);
    assert(format_weekday(value) == fixed_text("MON"));
    assert(format_weekday(watchy_time_t{2000, 1, 1, 0, 0, 0, 6, 0}) == fixed_text("SAT"));
    assert(format_weekday(watchy_time_t{2000, 2, 29, 0, 0, 0, 2, 0}) == fixed_text("TUE"));
    watchy_time_t rollover = offset_time(value, -720);
    assert(rollover.day == 30 && rollover.month == 8 && rollover.hour == 14);
    assert(!valid_time(watchy_time_t{2025, 2, 29, 0, 0, 0, 0, 0}));
    assert(valid_time(watchy_time_t{2024, 2, 29, 23, 59, 59, 4, 840}));
    assert(!valid_time(watchy_time_t{1900, 2, 29, 0, 0, 0, 0, 0}));
    assert(!valid_time(watchy_time_t{2026, 1, 1, 0, 0, 0, 0, 841}));
    fixed_text unchanged("KEEP");
    assert(!format_hhmm_checked(watchy_time_t{2025, 2, 29, 0, 0, 0, 0, 0}, true, &unchanged));
    assert(unchanged == fixed_text("KEEP"));
    watchy_time_t unchanged_time{2026, 8, 31, 9, 41, 0, 1, 420};
    watchy_time_t invalid{2025, 2, 29, 0, 0, 0, 0, 0};
    assert(!offset_time_checked(invalid, 0, &unchanged_time));
    assert(unchanged_time.day == 31 && unchanged_time.hour == 9);
    assert(offset_time(watchy_time_t{2026, 1, 1, 0, 15, 0, 4, 0}, -720).year == 2025);
    assert(offset_time(watchy_time_t{2025, 12, 31, 23, 45, 0, 3, 0}, 840).year == 2026);
    const watchy_package_descriptor_v1_t *descriptor = test_package_entry();
    assert(descriptor->metadata.abi.major == 1 && descriptor->metadata.abi.minor == 2);
    watchy_host_caps_v1_t caps{}; void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK && host(user) == &caps);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_render(user, nullptr, nullptr) == WATCHY_STATUS_OK);
    descriptor->callbacks.on_stop(user); descriptor->callbacks.on_unload(user);
    assert(host(user) == nullptr);
    void *second_user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &second_user) == WATCHY_STATUS_OK);
    descriptor->callbacks.on_unload(second_user);
    const watchy_face_data_t data{};
    assert(data.time.year == 0);
    assert(std::strcmp(grid01_package_entry()->metadata.identifier, "watchy.firstparty.grid01") == 0);
    assert(std::strcmp(grid01_package_entry()->metadata.name, "Grid 01") == 0);
    assert(std::strcmp(grid02_package_entry()->metadata.identifier, "watchy.firstparty.grid02") == 0);
    assert(std::strcmp(grid03_package_entry()->metadata.identifier, "watchy.firstparty.grid03") == 0);
    assert(test_grid(grid01_package_entry(), grid01_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-01.pbm", 787u, 1) == 0);
    assert(test_grid(grid02_package_entry(), grid02_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-02.pbm", 515u, 2) == 0);
    assert(test_grid(grid03_package_entry(), grid03_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-03.pbm", 515u, 3) == 0);
    return 0;
}
