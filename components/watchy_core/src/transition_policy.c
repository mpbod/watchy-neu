#include "watchy/transition.h"

#include <stddef.h>

#define WATCHY_TRANSITION_SAFE_BATTERY_MV 3550u
#define WATCHY_TRANSITION_REQUEST_FLAGS \
    (WATCHY_TRANSITION_HAS_RECT | WATCHY_TRANSITION_PREFER_FULL)

static bool valid_effect(watchy_transition_effect_t effect) {
    return effect >= WATCHY_TRANSITION_CUT && effect <= WATCHY_TRANSITION_SHUTTER;
}

static bool valid_direction(watchy_transition_direction_t direction) {
    return direction >= WATCHY_TRANSITION_DIRECTION_NONE &&
           direction <= WATCHY_TRANSITION_DIRECTION_DOWN;
}

static bool valid_rect(const watchy_transition_rect_t *rect) {
    int32_t right;
    int32_t bottom;

    if (rect->width <= 0 || rect->height <= 0) {
        return false;
    }

    right = (int32_t)rect->x + (int32_t)rect->width;
    bottom = (int32_t)rect->y + (int32_t)rect->height;
    return right > 0 && bottom > 0 && rect->x < WATCHY_TRANSITION_CANVAS_WIDTH &&
           rect->y < WATCHY_TRANSITION_CANVAS_HEIGHT;
}

static uint8_t write_count_for_effect(watchy_transition_effect_t effect) {
    switch (effect) {
    case WATCHY_TRANSITION_CUT:
        return 1u;
    case WATCHY_TRANSITION_FLASH:
    case WATCHY_TRANSITION_DITHER:
        return 2u;
    case WATCHY_TRANSITION_PUSH:
    case WATCHY_TRANSITION_GROW:
    case WATCHY_TRANSITION_ODOMETER:
    case WATCHY_TRANSITION_SPLIT:
        return 3u;
    case WATCHY_TRANSITION_WIPE:
        return 4u;
    case WATCHY_TRANSITION_FILL:
    case WATCHY_TRANSITION_SHUTTER:
        return 5u;
    }
    return 0u;
}

static void set_cut_plan(watchy_transition_plan_t *plan) {
    plan->effect = WATCHY_TRANSITION_CUT;
    plan->write_count = 1u;
}

static void set_default_cut_plan(watchy_transition_plan_t *plan) {
    plan->direction = WATCHY_TRANSITION_DIRECTION_NONE;
    plan->rect = (watchy_transition_rect_t){0, 0, WATCHY_TRANSITION_CANVAS_WIDTH,
                                             WATCHY_TRANSITION_CANVAS_HEIGHT};
    plan->target_full = false;
    plan->mandatory_clear = false;
    set_cut_plan(plan);
}

static void set_clear_plan(watchy_transition_plan_t *plan) {
    set_default_cut_plan(plan);
    plan->write_count = 2u;
    plan->target_full = true;
    plan->mandatory_clear = true;
}

watchy_status_t watchy_transition_validate(const watchy_transition_request_v1_t *request) {
    if (request == NULL || request->size != sizeof(*request) || !valid_effect(request->effect) ||
        !valid_direction(request->direction) || (request->flags & ~WATCHY_TRANSITION_REQUEST_FLAGS) != 0u ||
        request->reserved[0] != 0u || request->reserved[1] != 0u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }

    if ((request->flags & WATCHY_TRANSITION_HAS_RECT) != 0u && !valid_rect(&request->rect)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (request->effect == WATCHY_TRANSITION_ODOMETER &&
        (request->flags & WATCHY_TRANSITION_HAS_RECT) == 0u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_transition_plan(const watchy_transition_request_v1_t *request,
                                       const watchy_transition_policy_context_t *context,
                                       watchy_transition_plan_t *out_plan) {
    watchy_status_t status;

    if (context == NULL || out_plan == NULL || context->level > WATCHY_TRANSITION_LEVEL_OFF) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (request == NULL) {
        set_default_cut_plan(out_plan);
        if (context->clear_required) {
            set_clear_plan(out_plan);
        }
        return WATCHY_STATUS_OK;
    }
    status = watchy_transition_validate(request);
    if (status != WATCHY_STATUS_OK) {
        return status;
    }

    out_plan->effect = request->effect;
    out_plan->direction = request->direction;
    out_plan->rect = (request->flags & WATCHY_TRANSITION_HAS_RECT) != 0u
                         ? request->rect
                         : (watchy_transition_rect_t){0, 0, WATCHY_TRANSITION_CANVAS_WIDTH,
                                                       WATCHY_TRANSITION_CANVAS_HEIGHT};
    out_plan->write_count = write_count_for_effect(out_plan->effect);
    out_plan->target_full = (request->flags & WATCHY_TRANSITION_PREFER_FULL) != 0u;
    out_plan->mandatory_clear = false;

    if (context->clear_required) {
        set_clear_plan(out_plan);
        return WATCHY_STATUS_OK;
    }

    if (context->level == WATCHY_TRANSITION_LEVEL_OFF || context->safe_mode ||
        !context->attended || !context->source_valid ||
        context->battery_mv < WATCHY_TRANSITION_SAFE_BATTERY_MV) {
        set_cut_plan(out_plan);
    } else if (context->level == WATCHY_TRANSITION_LEVEL_REDUCED &&
               out_plan->effect != WATCHY_TRANSITION_CUT) {
        out_plan->effect = WATCHY_TRANSITION_FLASH;
        out_plan->write_count = 2u;
    }

    return WATCHY_STATUS_OK;
}
