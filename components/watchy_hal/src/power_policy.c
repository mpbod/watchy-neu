#include "watchy/power.h"

#include "watchy/board.h"
#include "watchy/buttons.h"

watchy_wake_cause_t watchy_power_map_wake(watchy_raw_wake_cause_t raw_cause,
                                          uint64_t wake_gpio_mask) {
    const uint64_t motion_mask = (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1) |
                                 (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_2);

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
