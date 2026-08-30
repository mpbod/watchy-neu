#ifndef WATCHY_DISPLAY_POLICY_H
#define WATCHY_DISPLAY_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_DISPLAY_READY_SETTLE_SAMPLES 2u

typedef struct {
    uint8_t consecutive_ready_samples;
} watchy_display_busy_filter_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t partial_count;
    uint32_t checksum;
    uint8_t previous_frame[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
} watchy_display_retained_state_t;

bool watchy_display_busy_observe(watchy_display_busy_filter_t *filter, bool busy_high);
bool watchy_display_retained_valid(const watchy_display_retained_state_t *state);
void watchy_display_invalidate_retained(watchy_display_retained_state_t *state);
watchy_refresh_mode_t watchy_display_prepare_refresh(const watchy_display_retained_state_t *state,
                                                     watchy_refresh_mode_t requested,
                                                     uint16_t partial_limit);
void watchy_display_commit_refresh(watchy_display_retained_state_t *state,
                                   watchy_refresh_mode_t completed,
                                   const uint8_t *framebuffer,
                                   size_t size);

#ifdef __cplusplus
}
#endif

#endif
