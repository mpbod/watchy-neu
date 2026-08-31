#ifndef WATCHY_FIRST_PARTY_FACE_H
#define WATCHY_FIRST_PARTY_FACE_H

#include <stdint.h>
#include "watchy/sdk.h"

namespace watchy_first_party {

struct fixed_text {
    char value[32];
    fixed_text() noexcept : value{} {}
    explicit fixed_text(const char *text) noexcept : value{} {
        if (text != nullptr) { uint8_t i = 0; for (; i + 1 < sizeof(value) && text[i] != '\0'; ++i) value[i] = text[i]; value[i] = '\0'; }
    }
    bool operator==(const fixed_text &other) const noexcept {
        uint8_t i = 0; while (value[i] != '\0' || other.value[i] != '\0') { if (value[i] != other.value[i]) return false; ++i; } return true;
    }
    const char *c_str() const noexcept { return value; }
};

struct watchy_face_data_t {
    watchy_time_t time;
    uint8_t battery_percent;
    bool bluetooth_enabled;
};

fixed_text format_hhmm(const watchy_time_t &time, bool twelve_hour) noexcept;
fixed_text format_date(const watchy_time_t &time) noexcept;
fixed_text format_day_month(const watchy_time_t &time) noexcept;
fixed_text format_day_month_year(const watchy_time_t &time) noexcept;
fixed_text format_weekday(const watchy_time_t &time) noexcept;
fixed_text format_percent(uint8_t percent) noexcept;
bool valid_time(const watchy_time_t &time) noexcept;
bool format_hhmm_checked(const watchy_time_t &time, bool twelve_hour, fixed_text *out) noexcept;
bool offset_time_checked(const watchy_time_t &time, int16_t utc_offset_minutes, watchy_time_t *out) noexcept;
watchy_time_t offset_time(const watchy_time_t &time, int16_t utc_offset_minutes) noexcept;
uint8_t moon_octant(const watchy_time_t &time) noexcept;

}  // namespace watchy_first_party

#endif
