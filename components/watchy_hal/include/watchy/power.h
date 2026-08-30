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

typedef struct {
    bool timer_configured;
    bool radios_stopped;
    bool motor_off;
    bool display_hibernated;
    bool rtc_source_cleared;
    bool motion_source_configured;
    bool ext0_configured;
    bool ext1_configured;
    bool sources_inactive;
} watchy_sleep_requirements_t;

typedef struct {
    uint8_t consecutive_inactive_samples;
} watchy_wake_source_filter_t;

watchy_wake_cause_t watchy_power_map_wake(watchy_raw_wake_cause_t raw_cause,
                                          uint64_t wake_gpio_mask);
watchy_wake_cause_t watchy_power_capture_wake_cause(void);
bool watchy_power_sleep_allowed(const watchy_sleep_requirements_t *requirements);
bool watchy_power_wake_sources_observe(watchy_wake_source_filter_t *filter,
                                       uint64_t active_sources);
bool watchy_power_release_pin_for_sleep(uint8_t pin);
watchy_status_t watchy_power_prepare_deep_sleep(bool timer_configured);
watchy_status_t watchy_power_prepare_deep_sleep_with_motion(bool timer_configured,
                                                            bool motion_wake_enabled);
bool watchy_power_prepare_attempted(void);
watchy_status_t watchy_power_last_prepare_status(void);
void watchy_power_enter_deep_sleep(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
