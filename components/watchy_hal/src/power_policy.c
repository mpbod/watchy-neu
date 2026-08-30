#include "watchy/power.h"

#include "watchy/board.h"
#include "watchy/buttons.h"

#include <stddef.h>

watchy_wake_cause_t watchy_power_map_wake(watchy_raw_wake_cause_t raw_cause,
                                          uint64_t wake_gpio_mask) {
    const uint64_t motion_mask = UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1;

    if (raw_cause == WATCHY_RAW_WAKE_UNDEFINED) {
        return WATCHY_WAKE_COLD;
    }
    if (raw_cause == WATCHY_RAW_WAKE_EXT0) {
        return WATCHY_WAKE_RTC;
    }
    if (raw_cause == WATCHY_RAW_WAKE_EXT1) {
        if (watchy_buttons_decode_wake_gpio(wake_gpio_mask) != 0u) {
            return WATCHY_WAKE_BUTTON;
        }
        if ((wake_gpio_mask & motion_mask) != 0u) {
            return WATCHY_WAKE_MOTION;
        }
    }
    return WATCHY_WAKE_OTHER;
}

bool watchy_power_sleep_allowed(const watchy_sleep_requirements_t *requirements) {
    return requirements != NULL && requirements->timer_configured && requirements->radios_stopped &&
           requirements->motor_off && requirements->display_hibernated &&
           requirements->rtc_source_cleared && requirements->motion_source_configured &&
           requirements->ext0_configured && requirements->ext1_configured &&
           requirements->sources_inactive;
}

bool watchy_power_wake_sources_observe(watchy_wake_source_filter_t *filter,
                                       uint64_t active_sources) {
    if (filter == NULL) {
        return false;
    }
    if (active_sources != 0u) {
        filter->consecutive_inactive_samples = 0;
        return false;
    }
    if (filter->consecutive_inactive_samples < 3u) {
        ++filter->consecutive_inactive_samples;
    }
    return filter->consecutive_inactive_samples >= 3u;
}
