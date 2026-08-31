#include <cassert>
#include <cstddef>
#include <cstring>

#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"

using namespace watchy_first_party;

static watchy_status_t render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) { return WATCHY_STATUS_OK; }
WATCHY_FIRST_PARTY_FACE("watchy.test.face", "Test Face", 0x203u, render)

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
    const watchy_package_descriptor_v1_t *descriptor = watchy_package_entry();
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
    return 0;
}
