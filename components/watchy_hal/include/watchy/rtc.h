#ifndef WATCHY_RTC_H
#define WATCHY_RTC_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

watchy_status_t watchy_rtc_init(void);
bool watchy_rtc_ready(void);
watchy_status_t watchy_rtc_read_local(watchy_time_t *out_time);
watchy_status_t watchy_rtc_read_unix(int64_t *out_unix_seconds);
watchy_status_t watchy_rtc_set_local(const watchy_time_t *time);
watchy_status_t watchy_rtc_set_minute_alarm(uint8_t minute);
watchy_status_t watchy_rtc_set_minute_timer(uint8_t minutes);
watchy_status_t watchy_rtc_clear_interrupt_flags(void);
void watchy_rtc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
