#ifndef WATCHY_TIMEZONE_ACTION_H
#define WATCHY_TIMEZONE_ACTION_H

#include <stdint.h>

#include "watchy/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    watchy_status_t (*set_rtc_offset)(void *context, int16_t minutes);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    watchy_status_t (*read_local)(void *context, watchy_time_t *out_time);
    void *context;
} watchy_timezone_action_ops_t;

watchy_status_t watchy_timezone_action_apply(
    watchy_settings_t *settings,
    const watchy_settings_t *candidate,
    watchy_time_t *time,
    const watchy_timezone_action_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif
