#include "watchy/transition.h"

static watchy_refresh_mode_t frame_mode(const watchy_transition_plan_t *plan,
                                        uint8_t frame_index) {
    if (plan->mandatory_clear ||
        (frame_index + 1u == plan->write_count && plan->target_full)) {
        return WATCHY_REFRESH_FULL;
    }
    return WATCHY_REFRESH_PARTIAL;
}

watchy_status_t watchy_transition_execute(const watchy_transition_plan_t *plan,
                                          const uint8_t *source,
                                          const uint8_t *target,
                                          uint8_t *scratch,
                                          size_t size,
                                          watchy_transition_write_fn write,
                                          watchy_transition_cancel_fn cancel,
                                          watchy_transition_feed_fn feed,
                                          void *context,
                                          watchy_transition_result_t *out_result) {
    if (out_result == NULL || write == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }

    *out_result = (watchy_transition_result_t){0};
    if (plan == NULL || plan->write_count == 0u) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    for (uint8_t frame = 0u; frame < plan->write_count; ++frame) {
        const watchy_status_t status =
            watchy_transition_compose_frame(plan, frame, source, target, scratch, size);

        if (status != WATCHY_STATUS_OK) {
            out_result->failure_cause = WATCHY_TRANSITION_FAILURE_COMPOSE;
            return WATCHY_STATUS_INVALID_STATE;
        }
        if (write(context, scratch, frame_mode(plan, frame)) != WATCHY_STATUS_OK) {
            out_result->failure_cause = WATCHY_TRANSITION_FAILURE_WRITE;
            return WATCHY_STATUS_INVALID_STATE;
        }
        ++out_result->writes_completed;
        if (feed != NULL) {
            feed(context);
        }
        if (!plan->mandatory_clear && frame + 1u < plan->write_count && cancel != NULL &&
            cancel(context)) {
            out_result->cancelled = true;
            out_result->source_valid = true;
            return WATCHY_STATUS_OK;
        }
    }

    out_result->completed = true;
    out_result->source_valid = true;
    out_result->last_frame_is_target = true;
    return WATCHY_STATUS_OK;
}
