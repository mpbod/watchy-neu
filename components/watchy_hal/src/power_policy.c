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
    if (raw_cause == WATCHY_RAW_WAKE_TIMER) {
        return WATCHY_WAKE_TIMER;
    }
    return WATCHY_WAKE_OTHER;
}

bool watchy_power_sleep_allowed(const watchy_sleep_requirements_t *requirements) {
    if (requirements == NULL || !requirements->radios_stopped || !requirements->motor_off ||
        !requirements->display_hibernated || !requirements->buttons_quiesced ||
        !requirements->ext1_configured || !requirements->sources_inactive) {
        return false;
    }
    if (requirements->button_only) {
        return !requirements->timer_configured && !requirements->rtc_source_cleared &&
               !requirements->motion_source_configured && !requirements->ext0_configured;
    }
    return requirements->timer_configured && requirements->rtc_source_cleared &&
           (!requirements->motion_wake_enabled || requirements->motion_source_configured) &&
           requirements->ext0_configured;
}

bool watchy_power_safe_mode_chord_allowed(watchy_wake_cause_t wake_cause) {
    return wake_cause == WATCHY_WAKE_COLD || wake_cause == WATCHY_WAKE_OTHER;
}

bool watchy_power_boot_should_initialize_motion(bool timer_configured,
                                                 bool motion_wake_enabled) {
    return timer_configured && motion_wake_enabled;
}

watchy_status_t watchy_power_sleep_handoff_status(
    watchy_status_t quiesce_status,
    bool sleep_veto,
    bool awake_services_restored) {
    if (quiesce_status != WATCHY_STATUS_OK ||
        (sleep_veto && !awake_services_restored)) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return sleep_veto ? WATCHY_STATUS_CANCELLED : WATCHY_STATUS_OK;
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

bool watchy_power_button_needs_internal_pulldown(uint8_t pin) {
    return pin == WATCHY_PIN_BUTTON_MENU || pin == WATCHY_PIN_BUTTON_BACK ||
           pin == WATCHY_PIN_BUTTON_DOWN;
}

bool watchy_power_release_pin_for_sleep(uint8_t pin) {
    return pin != WATCHY_PIN_MOTOR && pin != WATCHY_PIN_BATTERY_ADC;
}
