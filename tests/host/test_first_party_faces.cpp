#include <cassert>
#include <cstring>

#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"

using namespace watchy_first_party;

static watchy_status_t render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) { return WATCHY_STATUS_OK; }
WATCHY_FIRST_PARTY_FACE("watchy.test.face", "Test Face", 0x203u, render)

int main() {
    watchy_time_t value{2026, 8, 31, 9, 41, 0, 1, 420};
    assert(format_hhmm(value, true) == fixed_text("09:41"));
    assert(offset_time(value, 540).hour == 11);
    assert(moon_octant(value) < 8u);
    assert(moon_octant(watchy_time_t{2000, 1, 6, 18, 14, 0, 4, 0}) == 0u);
    assert(format_weekday(value) == fixed_text("MON"));
    watchy_time_t rollover = offset_time(value, -720);
    assert(rollover.day == 30 && rollover.month == 8 && rollover.hour == 14);
    assert(!valid_time(watchy_time_t{2025, 2, 29, 0, 0, 0, 0, 0}));
    const watchy_package_descriptor_v1_t *descriptor = watchy_package_entry();
    assert(descriptor->metadata.abi.major == 1 && descriptor->metadata.abi.minor == 2);
    watchy_host_caps_v1_t caps{}; void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK && host(user) == &caps);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_render(user, nullptr, nullptr) == WATCHY_STATUS_OK);
    descriptor->callbacks.on_stop(user); descriptor->callbacks.on_unload(user);
    const watchy_face_data_t data{};
    assert(data.time.year == 0);
    return 0;
}
