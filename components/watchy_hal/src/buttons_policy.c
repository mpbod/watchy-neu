#include "watchy/buttons.h"

#include "watchy/board.h"

watchy_button_mask_t watchy_buttons_decode_wake_gpio(uint64_t gpio_mask) {
    watchy_button_mask_t buttons = 0;

    if ((gpio_mask & (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU)) != 0u) {
        buttons |= WATCHY_BUTTON_MASK_MENU;
    }
    if ((gpio_mask & (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK)) != 0u) {
        buttons |= WATCHY_BUTTON_MASK_BACK;
    }
    if ((gpio_mask & (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN)) != 0u) {
        buttons |= WATCHY_BUTTON_MASK_DOWN;
    }
    if ((gpio_mask & (UINT64_C(1) << WATCHY_PIN_BUTTON_UP)) != 0u) {
        buttons |= WATCHY_BUTTON_MASK_UP;
    }
    return buttons;
}

bool watchy_buttons_is_safe_mode_chord(watchy_button_mask_t sampled_mask) {
    const watchy_button_mask_t chord = WATCHY_BUTTON_MASK_BACK | WATCHY_BUTTON_MASK_DOWN;
    return (sampled_mask & chord) == chord;
}
