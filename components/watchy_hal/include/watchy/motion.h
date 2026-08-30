#ifndef WATCHY_MOTION_H
#define WATCHY_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_FEATURE_UNAVAILABLE = 0,
    WATCHY_FEATURE_SUPPORTED = 1,
    WATCHY_FEATURE_UNSUPPORTED = 2,
} watchy_feature_status_t;

typedef struct {
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
    watchy_feature_status_t temperature_status;
    int16_t temperature_centi_c;
    watchy_feature_status_t steps_status;
    uint32_t steps;
} watchy_motion_data_t;

watchy_status_t watchy_motion_init(void);
bool watchy_motion_ready(void);
watchy_status_t watchy_motion_read(watchy_motion_data_t *out_data);
watchy_status_t watchy_motion_configure_wake(bool enable);
void watchy_motion_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
