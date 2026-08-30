#ifndef WATCHY_HAPTICS_H
#define WATCHY_HAPTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_HAPTICS_MAX_DURATION_MS 1000u

watchy_status_t watchy_haptics_init(void);
bool watchy_haptics_ready(void);
watchy_status_t watchy_haptics_pulse(uint16_t duration_ms, uint8_t strength);
void watchy_haptics_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
