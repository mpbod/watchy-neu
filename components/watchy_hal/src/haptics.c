#include "watchy/haptics.h"

#include "watchy/board.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bool s_ready;

watchy_status_t watchy_haptics_init(void) {
    gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << WATCHY_PIN_MOTOR,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_deep_sleep_hold_dis();
    if (gpio_config(&config) != ESP_OK || gpio_set_level(WATCHY_PIN_MOTOR, 0) != ESP_OK ||
        gpio_hold_dis(WATCHY_PIN_MOTOR) != ESP_OK ||
        gpio_set_level(WATCHY_PIN_MOTOR, 0) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_haptics_ready(void) {
    return s_ready;
}

watchy_status_t watchy_haptics_pulse(uint16_t duration_ms, uint8_t strength) {
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (duration_ms > WATCHY_HAPTICS_MAX_DURATION_MS) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (duration_ms == 0 || strength == 0) {
        return gpio_set_level(WATCHY_PIN_MOTOR, 0) == ESP_OK ? WATCHY_STATUS_OK
                                                             : WATCHY_STATUS_INVALID_STATE;
    }
    if (gpio_set_level(WATCHY_PIN_MOTOR, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return gpio_set_level(WATCHY_PIN_MOTOR, 0) == ESP_OK ? WATCHY_STATUS_OK
                                                         : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_haptics_deinit(void) {
    const watchy_status_t status = gpio_set_level(WATCHY_PIN_MOTOR, 0) == ESP_OK
                                       ? WATCHY_STATUS_OK
                                       : WATCHY_STATUS_INVALID_STATE;
    s_ready = false;
    return status;
}

watchy_status_t watchy_haptics_hold_off_for_sleep(void) {
    if (gpio_set_direction(WATCHY_PIN_MOTOR, GPIO_MODE_OUTPUT) != ESP_OK ||
        gpio_set_level(WATCHY_PIN_MOTOR, 0) != ESP_OK ||
        gpio_hold_en(WATCHY_PIN_MOTOR) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    gpio_deep_sleep_hold_en();
    return WATCHY_STATUS_OK;
}
