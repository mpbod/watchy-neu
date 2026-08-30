#ifndef WATCHY_BUTTONS_H
#define WATCHY_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t watchy_button_mask_t;

enum {
    WATCHY_BUTTON_MASK_MENU = 1u << 0,
    WATCHY_BUTTON_MASK_BACK = 1u << 1,
    WATCHY_BUTTON_MASK_DOWN = 1u << 2,
    WATCHY_BUTTON_MASK_UP = 1u << 3,
};

watchy_button_mask_t watchy_buttons_decode_wake_gpio(uint64_t gpio_mask);
bool watchy_buttons_is_safe_mode_chord(watchy_button_mask_t sampled_mask);
watchy_status_t watchy_buttons_init(void);
bool watchy_buttons_ready(void);
watchy_button_mask_t watchy_buttons_sample(void);
void watchy_buttons_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
