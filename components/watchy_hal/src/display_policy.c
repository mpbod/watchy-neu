#include "watchy/display_policy.h"

#include <string.h>

#define WATCHY_DISPLAY_RETAINED_MAGIC UINT32_C(0x57595044)
#define WATCHY_DISPLAY_RETAINED_VERSION 1u

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
        if (state != NULL) {
            state->magic = 0;
        }
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
