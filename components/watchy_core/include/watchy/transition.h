#ifndef WATCHY_TRANSITION_H
#define WATCHY_TRANSITION_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_TRANSITION_CANVAS_WIDTH 200
#define WATCHY_TRANSITION_CANVAS_HEIGHT 200

typedef enum {
    WATCHY_TRANSITION_LEVEL_FULL = 0,
    WATCHY_TRANSITION_LEVEL_REDUCED = 1,
    WATCHY_TRANSITION_LEVEL_OFF = 2,
} watchy_transition_level_t;

typedef struct {
    watchy_transition_level_t level;
    bool attended;
    bool safe_mode;
    bool source_valid;
    bool clear_required;
    uint16_t battery_mv;
} watchy_transition_policy_context_t;

typedef struct {
    watchy_transition_effect_t effect;
    watchy_transition_direction_t direction;
    watchy_transition_rect_t rect;
    uint8_t write_count;
    bool target_full;
    bool mandatory_clear;
} watchy_transition_plan_t;

watchy_status_t watchy_transition_validate(const watchy_transition_request_v1_t *request);
watchy_status_t watchy_transition_plan(const watchy_transition_request_v1_t *request,
                                       const watchy_transition_policy_context_t *context,
                                       watchy_transition_plan_t *out_plan);

#ifdef __cplusplus
}
#endif

#endif
