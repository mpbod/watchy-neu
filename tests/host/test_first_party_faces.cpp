#include <cassert>
#include <cstring>

#include "watchy_first_party/face.h"

using namespace watchy_first_party;

int main() {
    watchy_time_t value{2026, 8, 31, 9, 41, 0, 1, 420};
    assert(format_hhmm(value, true) == fixed_text("09:41"));
    assert(offset_time(value, 540).hour == 11);
    assert(moon_octant(value) < 8u);
    const watchy_face_data_t data{};
    assert(data.time.year == 0);
    return 0;
}
