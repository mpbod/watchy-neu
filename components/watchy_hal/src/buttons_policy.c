#include "watchy/buttons.h"

#include "watchy/board.h"

#include <stddef.h>

void watchy_buttons_filter_init(watchy_button_filter_t *filter,
                                watchy_button_mask_t initial_mask,
                                uint32_t debounce_ms) {
    if (filter == NULL) {
        return;
    }

    filter->stable_mask = initial_mask;
    filter->candidate_mask = initial_mask;
    filter->pending_mask = 0u;
    for (size_t index = 0u; index < WATCHY_BUTTON_COUNT; ++index) {
        filter->candidate_since_ms[index] = 0u;
    }
    filter->debounce_ms = debounce_ms;
}

watchy_button_mask_t watchy_buttons_filter_observe(
    watchy_button_filter_t *filter,
    watchy_button_mask_t sampled_mask,
    uint32_t now_ms) {
    static const watchy_button_mask_t button_bits[WATCHY_BUTTON_COUNT] = {
        WATCHY_BUTTON_MASK_MENU,
        WATCHY_BUTTON_MASK_BACK,
        WATCHY_BUTTON_MASK_DOWN,
        WATCHY_BUTTON_MASK_UP,
    };
    watchy_button_mask_t emitted_mask = 0u;

    if (filter == NULL || filter->debounce_ms == 0u) {
        return 0u;
    }

    for (size_t index = 0u; index < WATCHY_BUTTON_COUNT; ++index) {
        const watchy_button_mask_t bit = button_bits[index];
        const bool sampled_active = (sampled_mask & bit) != 0u;
        const bool candidate_active = (filter->candidate_mask & bit) != 0u;

        if (sampled_active != candidate_active) {
            if (sampled_active) {
                filter->candidate_mask |= bit;
            } else {
                filter->candidate_mask &= ~bit;
            }
            filter->candidate_since_ms[index] = now_ms;
            filter->pending_mask |= bit;
        }

        if ((filter->pending_mask & bit) != 0u &&
            (uint32_t)(now_ms - filter->candidate_since_ms[index]) >= filter->debounce_ms) {
            const bool was_active = (filter->stable_mask & bit) != 0u;
            const bool is_active = (filter->candidate_mask & bit) != 0u;

            if (is_active) {
                filter->stable_mask |= bit;
            } else {
                filter->stable_mask &= ~bit;
            }
            filter->pending_mask &= ~bit;
            if (!was_active && is_active) {
                emitted_mask |= bit;
            }
        }
    }
    return emitted_mask;
}

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
