#ifndef WATCHY_BUTTONS_H
#define WATCHY_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t watchy_button_mask_t;

#define WATCHY_BUTTON_COUNT 4u
#define WATCHY_BUTTON_DEBOUNCE_MS 30u

enum {
    WATCHY_BUTTON_MASK_MENU = 1u << 0,
    WATCHY_BUTTON_MASK_BACK = 1u << 1,
    WATCHY_BUTTON_MASK_DOWN = 1u << 2,
    WATCHY_BUTTON_MASK_UP = 1u << 3,
};

typedef struct {
    watchy_button_mask_t stable_mask;
    watchy_button_mask_t candidate_mask;
    watchy_button_mask_t pending_mask;
    uint32_t candidate_since_ms[WATCHY_BUTTON_COUNT];
    uint32_t debounce_ms;
} watchy_button_filter_t;

typedef struct {
    watchy_button_mask_t mask;
    uint32_t timestamp_ms;
} watchy_button_press_event_t;

void watchy_buttons_filter_init(watchy_button_filter_t *filter,
                                 watchy_button_mask_t initial_mask,
                                 uint32_t debounce_ms);
watchy_button_mask_t watchy_buttons_filter_observe(
    watchy_button_filter_t *filter,
    watchy_button_mask_t sampled_mask,
    uint32_t now_ms);

watchy_button_mask_t watchy_buttons_decode_wake_gpio(uint64_t gpio_mask);
bool watchy_buttons_is_safe_mode_chord(watchy_button_mask_t sampled_mask);
watchy_status_t watchy_buttons_init(void);
bool watchy_buttons_ready(void);
watchy_button_mask_t watchy_buttons_sample(void);
bool watchy_buttons_take_press(watchy_button_press_event_t *out_event,
                               uint32_t timeout_ms);
bool watchy_buttons_press_pending(void);
bool watchy_buttons_overflowed(void);
watchy_status_t watchy_buttons_quiesce(void);
watchy_status_t watchy_buttons_resume(void);
void watchy_buttons_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
