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

typedef struct {
    watchy_display_retained_state_t *retained;
    uint16_t partial_limit;
    watchy_refresh_mode_t target_requested;
    watchy_transition_write_fn physical_write;
    watchy_transition_cancel_fn cancel;
    watchy_transition_feed_fn feed;
    void *context;
} watchy_display_transition_io_t;

typedef struct {
    watchy_status_t status;
    watchy_button_mask_t cancelled_buttons;
    bool force_cut;
} watchy_display_execution_outcome_t;

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
bool watchy_display_plan_requires_clear(const watchy_display_retained_state_t *state,
                                        const watchy_transition_plan_t *plan,
                                        watchy_refresh_mode_t target_requested,
                                        uint16_t partial_limit);
watchy_status_t watchy_display_execution_status(watchy_status_t execution_status,
                                                const watchy_transition_result_t *result,
                                                bool watchdog_feed_failed);
watchy_display_execution_outcome_t watchy_display_finalize_execution(
    watchy_status_t execution_status,
    const watchy_transition_result_t *result,
    bool watchdog_feed_failed,
    watchy_status_t watchdog_end_status,
    watchy_button_mask_t cancelled_buttons);
watchy_button_mask_t watchy_display_new_button_mask(watchy_button_mask_t baseline,
                                                    watchy_button_mask_t current);
watchy_status_t watchy_display_execute_plan(const watchy_transition_plan_t *plan,
                                            const uint8_t *fallback_source,
                                            const uint8_t *target,
                                            uint8_t *source_snapshot,
                                            uint8_t *scratch,
                                            size_t size,
                                            const watchy_display_transition_io_t *io,
                                            watchy_transition_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
