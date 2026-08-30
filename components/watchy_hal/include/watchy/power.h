#ifndef WATCHY_POWER_H
#define WATCHY_POWER_H

#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_RAW_WAKE_UNDEFINED = 0,
    WATCHY_RAW_WAKE_EXT0,
    WATCHY_RAW_WAKE_EXT1,
    WATCHY_RAW_WAKE_TIMER,
    WATCHY_RAW_WAKE_TOUCH,
    WATCHY_RAW_WAKE_ULP,
    WATCHY_RAW_WAKE_OTHER,
} watchy_raw_wake_cause_t;

typedef enum {
    WATCHY_WAKE_COLD = 0,
    WATCHY_WAKE_RTC,
    WATCHY_WAKE_BUTTON,
    WATCHY_WAKE_MOTION,
    WATCHY_WAKE_OTHER,
} watchy_wake_cause_t;

watchy_wake_cause_t watchy_power_map_wake(watchy_raw_wake_cause_t raw_cause,
                                          uint64_t wake_gpio_mask);
watchy_wake_cause_t watchy_power_capture_wake_cause(void);
watchy_status_t watchy_power_prepare_deep_sleep(void);
void watchy_power_enter_deep_sleep(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
