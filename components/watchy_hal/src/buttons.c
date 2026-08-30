#include "watchy/buttons.h"

#include "watchy/board.h"

#include "driver/gpio.h"

static bool s_ready;

watchy_status_t watchy_buttons_init(void) {
    const uint64_t mask = (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_UP);
    gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&config) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_buttons_ready(void) {
    return s_ready;
}

watchy_button_mask_t watchy_buttons_sample(void) {
    watchy_button_mask_t mask = 0;
    if (!s_ready) {
        return 0;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_MENU) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_MENU;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_BACK) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_BACK;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_DOWN) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_DOWN;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_UP) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_UP;
    }
    return mask;
}

void watchy_buttons_deinit(void) {
    s_ready = false;
}
