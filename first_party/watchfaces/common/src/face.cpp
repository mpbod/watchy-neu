#include "watchy_first_party/face.h"

namespace watchy_first_party {
namespace {
bool leap(int y) noexcept { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
uint8_t days_in_month(int y, uint8_t m) noexcept { static const uint8_t d[] = {31,28,31,30,31,30,31,31,30,31,30,31}; return d[m - 1] + (m == 2 && leap(y)); }
void put2(char *out, uint8_t v) noexcept { out[0] = static_cast<char>('0' + v / 10); out[1] = static_cast<char>('0' + v % 10); }
int64_t days_since_epoch(const watchy_time_t &t) noexcept {
    int y = t.year, m = t.month; y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.day - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}
}

fixed_text format_hhmm(const watchy_time_t &time, bool twelve_hour) noexcept {
    fixed_text result; uint8_t hour = time.hour;
    if (twelve_hour) { hour = static_cast<uint8_t>(hour % 12u); if (hour == 0) hour = 12; }
    put2(result.value, hour); result.value[2] = ':'; put2(result.value + 3, time.minute); result.value[5] = '\0'; return result;
}

fixed_text format_date(const watchy_time_t &time) noexcept {
    fixed_text result; result.value[0] = static_cast<char>('0' + (time.month / 10)); result.value[1] = static_cast<char>('0' + (time.month % 10)); result.value[2] = '/';
    put2(result.value + 3, time.day); result.value[5] = '/'; result.value[6] = static_cast<char>('0' + ((time.year / 1000) % 10)); result.value[7] = static_cast<char>('0' + ((time.year / 100) % 10)); result.value[8] = static_cast<char>('0' + ((time.year / 10) % 10)); result.value[9] = static_cast<char>('0' + (time.year % 10)); result.value[10] = '\0'; return result;
}

watchy_time_t offset_time(const watchy_time_t &time, int16_t utc_offset_minutes) noexcept {
    watchy_time_t result = time; int total = static_cast<int>(time.hour) * 60 + time.minute + utc_offset_minutes - time.utc_offset_minutes;
    while (total < 0) { total += 1440; if (result.day == 1) { if (result.month == 1) { --result.year; result.month = 12; } else --result.month; result.day = days_in_month(result.year, result.month); } else --result.day; }
    while (total >= 1440) { total -= 1440; if (result.day == days_in_month(result.year, result.month)) { result.day = 1; if (result.month == 12) { ++result.year; result.month = 1; } else ++result.month; } else ++result.day; }
    result.hour = static_cast<uint8_t>(total / 60); result.minute = static_cast<uint8_t>(total % 60); result.utc_offset_minutes = utc_offset_minutes; return result;
}

uint8_t moon_octant(const watchy_time_t &time) noexcept {
    constexpr int64_t epoch_days = 10957; // 2000-01-06
    const int64_t days = days_since_epoch(time); const int64_t seconds = (days - epoch_days) * 86400 + time.hour * 3600 + time.minute * 60 + time.second - 65640;
    int64_t phase = seconds % 2551443; if (phase < 0) phase += 2551443; return static_cast<uint8_t>((phase * 8) / 2551443);
}
}  // namespace watchy_first_party
