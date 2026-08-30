#include "watchy/rtc_calendar.h"

#include <stddef.h>

static bool leap_year(int32_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static uint8_t days_in_month(int32_t year, uint8_t month) {
    static const uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1u || month > 12u) {
        return 0;
    }
    return month == 2u && leap_year(year) ? 29u : DAYS[month - 1u];
}

static bool valid_bcd(uint8_t value) {
    return (value & 0x0fu) <= 9u && ((value >> 4u) & 0x0fu) <= 9u;
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4u) * 10u) + (value & 0x0fu));
}

static uint8_t to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

bool watchy_calendar_valid(const watchy_time_t *time) {
    return time != NULL && time->year >= 1900 && time->year <= 2099 &&
           time->month >= 1u && time->month <= 12u && time->day >= 1u &&
           time->day <= days_in_month(time->year, time->month) && time->hour <= 23u &&
           time->minute <= 59u && time->second <= 59u && time->weekday <= 6u &&
           time->utc_offset_minutes >= -1439 && time->utc_offset_minutes <= 1439;
}

static int64_t days_from_civil(int32_t year, uint32_t month, uint32_t day) {
    const int32_t adjusted_year = year - (month <= 2u ? 1 : 0);
    const int32_t era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    const uint32_t year_of_era = (uint32_t)(adjusted_year - era * 400);
    const uint32_t shifted_month = month > 2u ? month - 3u : month + 9u;
    const uint32_t day_of_year = (153u * shifted_month + 2u) / 5u + day - 1u;
    const uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u - year_of_era / 100u +
                                day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

watchy_status_t watchy_calendar_to_unix(const watchy_time_t *time, int64_t *out_unix_seconds) {
    if (time == NULL || out_unix_seconds == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!watchy_calendar_valid(time)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_unix_seconds = days_from_civil(time->year, time->month, time->day) * INT64_C(86400) +
                        (int64_t)time->hour * 3600 + (int64_t)time->minute * 60 + time->second -
                        (int64_t)time->utc_offset_minutes * 60;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_calendar_from_unix(int64_t unix_seconds,
                                          int16_t utc_offset_minutes,
                                          watchy_time_t *out_time) {
    int64_t local_seconds;
    int64_t days;
    int64_t seconds_of_day;
    int64_t z;
    int64_t era;
    uint32_t day_of_era;
    uint32_t year_of_era;
    int32_t year;
    uint32_t day_of_year;
    uint32_t month_prime;
    uint32_t month;

    if (out_time == NULL || utc_offset_minutes < -1439 || utc_offset_minutes > 1439) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    local_seconds = unix_seconds + (int64_t)utc_offset_minutes * 60;
    days = local_seconds / INT64_C(86400);
    seconds_of_day = local_seconds % INT64_C(86400);
    if (seconds_of_day < 0) {
        seconds_of_day += INT64_C(86400);
        --days;
    }
    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    day_of_era = (uint32_t)(z - era * 146097);
    year_of_era = (day_of_era - day_of_era / 1460u + day_of_era / 36524u -
                   day_of_era / 146096u) /
                  365u;
    year = (int32_t)year_of_era + (int32_t)era * 400;
    day_of_year = day_of_era - (365u * year_of_era + year_of_era / 4u -
                                year_of_era / 100u);
    month_prime = (5u * day_of_year + 2u) / 153u;
    out_time->day = (uint8_t)(day_of_year - (153u * month_prime + 2u) / 5u + 1u);
    month = month_prime < 10u ? month_prime + 3u : month_prime - 9u;
    year += month <= 2u ? 1 : 0;
    if (year < 1900 || year > 2099) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    out_time->year = (int16_t)year;
    out_time->month = (uint8_t)month;
    out_time->hour = (uint8_t)(seconds_of_day / 3600);
    out_time->minute = (uint8_t)((seconds_of_day % 3600) / 60);
    out_time->second = (uint8_t)(seconds_of_day % 60);
    out_time->weekday = (uint8_t)((days + 4) % 7);
    if ((int8_t)out_time->weekday < 0) {
        out_time->weekday = (uint8_t)(out_time->weekday + 7u);
    }
    out_time->utc_offset_minutes = utc_offset_minutes;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_pcf8563_decode(const uint8_t registers[7], watchy_time_t *out_utc) {
    const uint8_t second = registers == NULL ? 0xffu : (uint8_t)(registers[0] & 0x7fu);
    const uint8_t minute = registers == NULL ? 0xffu : (uint8_t)(registers[1] & 0x7fu);
    const uint8_t hour = registers == NULL ? 0xffu : (uint8_t)(registers[2] & 0x3fu);
    const uint8_t day = registers == NULL ? 0xffu : (uint8_t)(registers[3] & 0x3fu);
    const uint8_t weekday = registers == NULL ? 0xffu : (uint8_t)(registers[4] & 0x07u);
    const uint8_t month = registers == NULL ? 0xffu : (uint8_t)(registers[5] & 0x1fu);
    const uint8_t year = registers == NULL ? 0xffu : registers[6];

    if (registers == NULL || out_utc == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if ((registers[0] & 0x80u) != 0u || !valid_bcd(second) || !valid_bcd(minute) ||
        !valid_bcd(hour) || !valid_bcd(day) || !valid_bcd(weekday) || !valid_bcd(month) ||
        !valid_bcd(year)) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_utc = (watchy_time_t){
        .year = (int16_t)(((registers[5] & 0x80u) != 0u ? 1900 : 2000) + from_bcd(year)),
        .month = from_bcd(month),
        .day = from_bcd(day),
        .hour = from_bcd(hour),
        .minute = from_bcd(minute),
        .second = from_bcd(second),
        .weekday = from_bcd(weekday),
        .utc_offset_minutes = 0,
    };
    return watchy_calendar_valid(out_utc) ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_pcf8563_encode(const watchy_time_t *utc, uint8_t registers[7]) {
    if (utc == NULL || registers == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!watchy_calendar_valid(utc) || utc->utc_offset_minutes != 0) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    registers[0] = to_bcd(utc->second);
    registers[1] = to_bcd(utc->minute);
    registers[2] = to_bcd(utc->hour);
    registers[3] = to_bcd(utc->day);
    registers[4] = to_bcd(utc->weekday);
    registers[5] = (uint8_t)(to_bcd(utc->month) | (utc->year < 2000 ? 0x80u : 0u));
    registers[6] = to_bcd((uint8_t)(utc->year % 100));
    return WATCHY_STATUS_OK;
}
