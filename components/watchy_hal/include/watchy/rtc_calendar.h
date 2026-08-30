#ifndef WATCHY_RTC_CALENDAR_H
#define WATCHY_RTC_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

bool watchy_calendar_valid(const watchy_time_t *time);
bool watchy_rtc_initial_clock_ready(const uint8_t registers[7]);
watchy_status_t watchy_calendar_to_unix(const watchy_time_t *time, int64_t *out_unix_seconds);
watchy_status_t watchy_calendar_from_unix(int64_t unix_seconds,
                                          int16_t utc_offset_minutes,
                                          watchy_time_t *out_time);
watchy_status_t watchy_pcf8563_decode(const uint8_t registers[7], watchy_time_t *out_utc);
watchy_status_t watchy_pcf8563_encode(const watchy_time_t *utc, uint8_t registers[7]);
watchy_status_t watchy_pcf8563_alarm_encode(const watchy_time_t *time, uint8_t registers[4]);

#ifdef __cplusplus
}
#endif

#endif
