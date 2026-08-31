#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"

namespace watchy_first_party {
namespace {

bool leap_year(int year) noexcept {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t days_in_month(int year, uint8_t month) noexcept {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    return static_cast<uint8_t>(days[month - 1u] +
                                (month == 2u && leap_year(year) ? 1u : 0u));
}

void put_two(char *out, uint8_t value) noexcept {
    out[0] = static_cast<char>('0' + value / 10u);
    out[1] = static_cast<char>('0' + value % 10u);
}

/* Proleptic Gregorian days since 1970-01-01. */
int64_t days_since_epoch(const watchy_time_t &time) noexcept {
    int year = time.year;
    int month = time.month;
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 +
                              time.day - 1);
    const unsigned day_of_era = year_of_era * 365u + year_of_era / 4u -
                                year_of_era / 100u + day_of_year;
    return static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
}

uint8_t weekday_for(const watchy_time_t &time) noexcept {
    int64_t weekday = (days_since_epoch(time) + 4) % 7;
    if (weekday < 0) weekday += 7;
    return static_cast<uint8_t>(weekday);
}

}  // namespace

bool valid_time(const watchy_time_t &time) noexcept {
    return time.year >= 1 && time.year <= 9999 &&
           time.month >= 1 && time.month <= 12 && time.day >= 1 &&
           time.day <= days_in_month(time.year, time.month) &&
           time.hour < 24 && time.minute < 60 && time.second < 60 &&
           time.utc_offset_minutes >= -720 && time.utc_offset_minutes <= 840;
}

bool format_hhmm_checked(const watchy_time_t &time,
                         bool twelve_hour,
                         fixed_text *out) noexcept {
    if (out == nullptr || !valid_time(time)) return false;
    uint8_t hour = time.hour;
    if (twelve_hour) {
        hour = static_cast<uint8_t>(hour % 12u);
        if (hour == 0u) hour = 12u;
    }
    put_two(out->value, hour);
    out->value[2] = ':';
    put_two(out->value + 3, time.minute);
    out->value[5] = '\0';
    return true;
}

fixed_text format_hhmm(const watchy_time_t &time, bool twelve_hour) noexcept {
    fixed_text out;
    (void)format_hhmm_checked(time, twelve_hour, &out);
    return out;
}

fixed_text format_date(const watchy_time_t &time) noexcept {
    fixed_text out;
    if (!valid_time(time)) return out;
    put_two(out.value, time.month);
    out.value[2] = '/';
    put_two(out.value + 3, time.day);
    out.value[5] = '/';
    out.value[6] = static_cast<char>('0' + (time.year / 1000) % 10);
    out.value[7] = static_cast<char>('0' + (time.year / 100) % 10);
    out.value[8] = static_cast<char>('0' + (time.year / 10) % 10);
    out.value[9] = static_cast<char>('0' + time.year % 10);
    out.value[10] = '\0';
    return out;
}

fixed_text format_day_month(const watchy_time_t &time) noexcept {
    fixed_text out;
    if (!valid_time(time)) return out;
    put_two(out.value, time.day);
    out.value[2] = ' ';
#if defined(__ELF__)
    static const char *names[] __attribute__((section(".data"))) = {
#else
    static const char *const names[] = {
#endif
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    const char *month = names[time.month - 1u];
    for (uint8_t i = 0u; i < 3u; ++i) out.value[3u + i] = month[i];
    out.value[6] = '\0';
    return out;
}

fixed_text format_day_month_year(const watchy_time_t &time) noexcept {
    fixed_text out = format_day_month(time);
    if (!valid_time(time)) return out;
    out.value[6] = ' ';
    out.value[7] = static_cast<char>('0' + (time.year / 1000) % 10);
    out.value[8] = static_cast<char>('0' + (time.year / 100) % 10);
    out.value[9] = static_cast<char>('0' + (time.year / 10) % 10);
    out.value[10] = static_cast<char>('0' + time.year % 10);
    out.value[11] = '\0';
    return out;
}

fixed_text format_weekday(const watchy_time_t &time) noexcept {
#if defined(__ELF__)
    /* The Xtensa ELF package loader relocates writable .data at load time;
     * keep the weekday table out of .rodata so the pointer array is
     * relocation-safe in the package image. Mach-O host tests do not need
     * (and cannot use) this specifier. */
    static const char *names[] __attribute__((section(".data"))) = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
#else
    static const char *const names[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
#endif
    return valid_time(time) ? fixed_text(names[weekday_for(time)]) : fixed_text();
}

bool offset_time_checked(const watchy_time_t &time,
                         int16_t target_offset_minutes,
                         watchy_time_t *out) noexcept {
    if (out == nullptr || !valid_time(time) ||
        target_offset_minutes < -720 || target_offset_minutes > 840) {
        return false;
    }

    watchy_time_t result = time;
    int total_minutes = static_cast<int>(time.hour) * 60 + time.minute +
                        target_offset_minutes - time.utc_offset_minutes;
    int day_delta = 0;
    while (total_minutes < 0) {
        total_minutes += 1440;
        --day_delta;
    }
    while (total_minutes >= 1440) {
        total_minutes -= 1440;
        ++day_delta;
    }
    result.hour = static_cast<uint8_t>(total_minutes / 60);
    result.minute = static_cast<uint8_t>(total_minutes % 60);
    result.utc_offset_minutes = target_offset_minutes;

    while (day_delta < 0) {
        if (result.day == 1u) {
            if (result.month == 1u) {
                if (result.year == 1) return false;
                --result.year;
                result.month = 12u;
            } else {
                --result.month;
            }
            result.day = days_in_month(result.year, result.month);
        } else {
            --result.day;
        }
        ++day_delta;
    }
    while (day_delta > 0) {
        if (result.day == days_in_month(result.year, result.month)) {
            result.day = 1u;
            if (result.month == 12u) {
                if (result.year == 9999) return false;
                ++result.year;
                result.month = 1u;
            } else {
                ++result.month;
            }
        } else {
            ++result.day;
        }
        --day_delta;
    }
    result.weekday = weekday_for(result);
    *out = result;
    return true;
}

watchy_time_t offset_time(const watchy_time_t &time,
                          int16_t target_offset_minutes) noexcept {
    watchy_time_t out{};
    (void)offset_time_checked(time, target_offset_minutes, &out);
    return out;
}

uint8_t moon_octant(const watchy_time_t &time) noexcept {
    if (!valid_time(time)) return 0u;

    /* Approved UTC epoch: 2000-01-06 18:14. The supported calendar has fewer
     * than four million days from Unix epoch, so every intermediate below is
     * deliberately signed 32-bit. That avoids unsupported 64-bit divide/mod
     * helpers in the freestanding Xtensa package ELF. */
    constexpr int32_t epoch_day = 10962;
    constexpr int32_t cycle_seconds = 2551443;
    const int32_t day_delta = static_cast<int32_t>(days_since_epoch(time)) - epoch_day;
    int32_t reduced_day = day_delta % cycle_seconds;
    if (reduced_day < 0) reduced_day += cycle_seconds;

    /* Modular multiply by 86400 uses at most seventeen bounded iterations;
     * accumulator and addend remain below twice the lunar cycle. */
    uint32_t accumulator = 0u;
    uint32_t addend = static_cast<uint32_t>(reduced_day);
    uint32_t factor = 86400u;
    while (factor != 0u) {
        if ((factor & 1u) != 0u) {
            accumulator += addend;
            if (accumulator >= static_cast<uint32_t>(cycle_seconds)) accumulator -= cycle_seconds;
        }
        addend += addend;
        if (addend >= static_cast<uint32_t>(cycle_seconds)) addend -= cycle_seconds;
        factor >>= 1u;
    }

    int32_t phase = static_cast<int32_t>(accumulator) +
                    static_cast<int32_t>(time.hour) * 3600 +
                    static_cast<int32_t>(time.minute) * 60 + time.second -
                    static_cast<int32_t>(time.utc_offset_minutes) * 60 -
                    (18 * 3600 + 14 * 60);
    phase %= cycle_seconds;
    if (phase < 0) phase += cycle_seconds;
    return static_cast<uint8_t>((phase * 8) / cycle_seconds);
}

/* Single definition shared by every translation unit of one package image
 * (package.cpp load path and renderer.cpp render path). Marked hidden so it
 * never becomes a second exported package symbol. */
__attribute__((visibility("hidden")))
face_context &state() noexcept {
    static face_context value{nullptr, false, false, 0u};
    return value;
}

}  // namespace watchy_first_party
