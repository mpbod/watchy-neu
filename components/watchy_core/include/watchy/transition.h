#ifndef WATCHY_TRANSITION_H
#define WATCHY_TRANSITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_TRANSITION_CANVAS_WIDTH 200
#define WATCHY_TRANSITION_CANVAS_HEIGHT 200
#define WATCHY_TRANSITION_FRAME_BYTES 5000u

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

typedef watchy_status_t (*watchy_transition_write_fn)(void *context,
                                                       const uint8_t *frame,
                                                       watchy_refresh_mode_t mode);
typedef bool (*watchy_transition_cancel_fn)(void *context);
typedef void (*watchy_transition_feed_fn)(void *context);

typedef enum {
    WATCHY_TRANSITION_FAILURE_NONE = 0,
    WATCHY_TRANSITION_FAILURE_COMPOSE,
    WATCHY_TRANSITION_FAILURE_WRITE,
} watchy_transition_failure_t;

typedef struct {
    uint8_t writes_completed;
    bool completed;
    bool cancelled;
    bool source_valid;
    bool last_frame_is_target;
    watchy_transition_failure_t failure_cause;
} watchy_transition_result_t;

watchy_status_t watchy_transition_validate(const watchy_transition_request_v1_t *request);
watchy_status_t watchy_transition_plan(const watchy_transition_request_v1_t *request,
                                       const watchy_transition_policy_context_t *context,
                                       watchy_transition_plan_t *out_plan);
watchy_status_t watchy_transition_compose_frame(const watchy_transition_plan_t *plan,
                                                uint8_t frame_index,
                                                const uint8_t *source,
                                                const uint8_t *target,
                                                uint8_t *out,
                                                size_t size);
watchy_status_t watchy_transition_resolve_plan(const watchy_transition_plan_t *plan,
                                              const uint8_t *source,
                                              const uint8_t *target,
                                              uint8_t *scratch,
                                              size_t size,
                                              watchy_transition_plan_t *out_plan);
watchy_status_t watchy_transition_execute(const watchy_transition_plan_t *plan,
                                          const uint8_t *source,
                                          const uint8_t *target,
                                          uint8_t *scratch,
                                          size_t size,
                                          watchy_transition_write_fn write,
                                          watchy_transition_cancel_fn cancel,
                                          watchy_transition_feed_fn feed,
                                          void *context,
                                          watchy_transition_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
