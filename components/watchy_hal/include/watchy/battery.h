#ifndef WATCHY_BATTERY_H
#define WATCHY_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_BATTERY_INSTALL_SAFE_MV 3550u
#define WATCHY_BATTERY_SAMPLE_COUNT 16u

uint8_t watchy_battery_percent_from_mv(uint16_t millivolts);
bool watchy_battery_is_install_safe(uint16_t millivolts);
watchy_status_t watchy_battery_init(void);
bool watchy_battery_ready(void);
bool watchy_battery_charging_supported(void);
watchy_status_t watchy_battery_read(watchy_battery_state_t *out_state);
void watchy_battery_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
