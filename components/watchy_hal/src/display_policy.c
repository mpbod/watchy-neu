#include "watchy/display_policy.h"

#include <string.h>

#define WATCHY_DISPLAY_RETAINED_MAGIC UINT32_C(0x57595044)
#define WATCHY_DISPLAY_RETAINED_VERSION 1u

typedef struct {
    const watchy_display_transition_io_t *io;
    uint8_t write_count;
    uint8_t writes_completed;
} display_plan_execution_t;

static uint32_t retained_checksum(const watchy_display_retained_state_t *state) {
    uint32_t checksum = UINT32_C(2166136261);
    checksum = (checksum ^ (uint8_t)state->partial_count) * UINT32_C(16777619);
    checksum = (checksum ^ (uint8_t)(state->partial_count >> 8u)) * UINT32_C(16777619);
    for (size_t index = 0; index < sizeof(state->previous_frame); ++index) {
        checksum = (checksum ^ state->previous_frame[index]) * UINT32_C(16777619);
    }
    return checksum;
}

bool watchy_display_busy_observe(watchy_display_busy_filter_t *filter, bool busy_high) {
    if (filter == NULL) {
        return false;
    }
    if (busy_high) {
        filter->consecutive_ready_samples = 0;
        return false;
    }
    if (filter->consecutive_ready_samples < WATCHY_DISPLAY_READY_SETTLE_SAMPLES) {
        ++filter->consecutive_ready_samples;
    }
    return filter->consecutive_ready_samples >= WATCHY_DISPLAY_READY_SETTLE_SAMPLES;
}

bool watchy_display_retained_valid(const watchy_display_retained_state_t *state) {
    return state != NULL && state->magic == WATCHY_DISPLAY_RETAINED_MAGIC &&
           state->version == WATCHY_DISPLAY_RETAINED_VERSION &&
           state->checksum == retained_checksum(state);
}

void watchy_display_invalidate_retained(watchy_display_retained_state_t *state) {
    if (state != NULL) {
        state->magic = 0u;
    }
}

watchy_refresh_mode_t watchy_display_prepare_refresh(const watchy_display_retained_state_t *state,
                                                     watchy_refresh_mode_t requested,
                                                     uint16_t partial_limit) {
    if (!watchy_display_retained_valid(state) || requested == WATCHY_REFRESH_FULL ||
        partial_limit == 0u || (uint32_t)state->partial_count + 1u >= partial_limit) {
        return WATCHY_REFRESH_FULL;
    }
    return WATCHY_REFRESH_PARTIAL;
}

void watchy_display_commit_refresh(watchy_display_retained_state_t *state,
                                   watchy_refresh_mode_t completed,
                                   const uint8_t *framebuffer,
                                   size_t size) {
    if (state == NULL || framebuffer == NULL || size != sizeof(state->previous_frame)) {
        watchy_display_invalidate_retained(state);
        return;
    }
    if (completed == WATCHY_REFRESH_FULL || !watchy_display_retained_valid(state)) {
        state->partial_count = 0;
    } else if (state->partial_count != UINT16_MAX) {
        ++state->partial_count;
    }
    memcpy(state->previous_frame, framebuffer, sizeof(state->previous_frame));
    state->magic = WATCHY_DISPLAY_RETAINED_MAGIC;
    state->version = WATCHY_DISPLAY_RETAINED_VERSION;
    state->checksum = retained_checksum(state);
}

static watchy_status_t execute_physical_write(void *context,
                                              const uint8_t *frame,
                                              watchy_refresh_mode_t requested) {
    display_plan_execution_t *execution = (display_plan_execution_t *)context;
    const watchy_display_transition_io_t *io;
    watchy_refresh_mode_t completed;

    if (execution == NULL || execution->io == NULL || frame == NULL ||
        execution->writes_completed >= execution->write_count) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    io = execution->io;
    if (io->target_requested == WATCHY_REFRESH_FULL &&
        execution->writes_completed + 1u == execution->write_count) {
        requested = WATCHY_REFRESH_FULL;
    }
    completed = watchy_display_prepare_refresh(io->retained, requested, io->partial_limit);
    if (io->physical_write(io->context, frame, completed) != WATCHY_STATUS_OK) {
        watchy_display_invalidate_retained(io->retained);
        return WATCHY_STATUS_INVALID_STATE;
    }
    watchy_display_commit_refresh(io->retained, completed, frame,
                                  WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    ++execution->writes_completed;
    return WATCHY_STATUS_OK;
}

static bool execute_cancel(void *context) {
    const display_plan_execution_t *execution = (const display_plan_execution_t *)context;

    return execution != NULL && execution->io != NULL && execution->io->cancel != NULL &&
           execution->io->cancel(execution->io->context);
}

static void execute_feed(void *context) {
    const display_plan_execution_t *execution = (const display_plan_execution_t *)context;

    if (execution != NULL && execution->io != NULL && execution->io->feed != NULL) {
        execution->io->feed(execution->io->context);
    }
}

watchy_status_t watchy_display_execute_plan(const watchy_transition_plan_t *plan,
                                            const uint8_t *fallback_source,
                                            const uint8_t *target,
                                            uint8_t *source_snapshot,
                                            uint8_t *scratch,
                                            size_t size,
                                            const watchy_display_transition_io_t *io,
                                            watchy_transition_result_t *out_result) {
    display_plan_execution_t execution;
    const uint8_t *source;

    if (out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_transition_result_t){0};
    if (plan == NULL || fallback_source == NULL || target == NULL || source_snapshot == NULL ||
        scratch == NULL || size != WATCHY_DISPLAY_FRAMEBUFFER_SIZE ||
        source_snapshot == target || source_snapshot == scratch || target == scratch ||
        io == NULL || io->retained == NULL || io->partial_limit == 0u ||
        (io->target_requested != WATCHY_REFRESH_PARTIAL &&
         io->target_requested != WATCHY_REFRESH_FULL) ||
        io->physical_write == NULL || source_snapshot == io->retained->previous_frame) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }

    source = watchy_display_retained_valid(io->retained) ? io->retained->previous_frame
                                                         : fallback_source;
    if (source != source_snapshot) {
        memcpy(source_snapshot, source, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    }
    execution = (display_plan_execution_t){
        .io = io,
        .write_count = plan->write_count,
    };
    return watchy_transition_execute(plan, source_snapshot, target, scratch, size,
                                     execute_physical_write, execute_cancel, execute_feed,
                                     &execution, out_result);
}
