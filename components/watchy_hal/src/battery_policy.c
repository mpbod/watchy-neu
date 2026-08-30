#include "watchy/battery.h"

#include <stddef.h>

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
} battery_anchor_t;

static const battery_anchor_t BATTERY_CURVE[] = {
    {3400, 0},
    {3500, 5},
    {3550, 10},
    {3600, 15},
    {3700, 30},
    {3800, 50},
    {3900, 70},
    {4000, 85},
    {4100, 95},
    {4150, 100},
};

uint8_t watchy_battery_percent_from_mv(uint16_t millivolts) {
    const size_t count = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);

    if (millivolts <= BATTERY_CURVE[0].millivolts) {
        return BATTERY_CURVE[0].percent;
    }
    for (size_t index = 1; index < count; ++index) {
        const battery_anchor_t lower = BATTERY_CURVE[index - 1];
        const battery_anchor_t upper = BATTERY_CURVE[index];
        if (millivolts <= upper.millivolts) {
            const uint32_t voltage_span = (uint32_t)upper.millivolts - lower.millivolts;
            const uint32_t percent_span = (uint32_t)upper.percent - lower.percent;
            const uint32_t offset = (uint32_t)millivolts - lower.millivolts;
            return (uint8_t)(lower.percent + (offset * percent_span) / voltage_span);
        }
    }
    return 100;
}

bool watchy_battery_is_install_safe(uint16_t millivolts) {
    return millivolts >= WATCHY_BATTERY_INSTALL_SAFE_MV;
}
