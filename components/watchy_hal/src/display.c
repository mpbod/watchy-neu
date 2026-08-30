#include "watchy/display.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/buses.h"
#include "watchy/runtime.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_DISPLAY_BUSY_TIMEOUT_MS 10000
#define WATCHY_DISPLAY_PARTIAL_LIMIT 20u

static uint8_t s_framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
static uint8_t s_previous_framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
static watchy_refresh_policy_t s_refresh_policy;
static bool s_ready;

static watchy_status_t from_esp_error(esp_err_t error) {
    return error == ESP_OK ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

static watchy_status_t send_command_with_data(uint8_t command, const uint8_t *data, size_t length) {
    esp_err_t error = watchy_bus_display_command(command);
    if (error == ESP_OK && length > 0) {
        error = watchy_bus_display_data(data, length);
    }
    return from_esp_error(error);
}

static watchy_status_t wait_ready(void) {
    const int64_t deadline = esp_timer_get_time() + WATCHY_DISPLAY_BUSY_TIMEOUT_MS * INT64_C(1000);
    while (gpio_get_level(WATCHY_PIN_DISPLAY_BUSY) == 0) {
        if (esp_timer_get_time() >= deadline) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return WATCHY_STATUS_OK;
}

static watchy_status_t set_ram_window(void) {
    static const uint8_t entry_mode[] = {0x03};
    static const uint8_t x_window[] = {0x00, 0x18};
    static const uint8_t y_window[] = {0x00, 0x00, 0xc7, 0x00};
    static const uint8_t x_counter[] = {0x00};
    static const uint8_t y_counter[] = {0x00, 0x00};

    if (send_command_with_data(0x11, entry_mode, sizeof(entry_mode)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x44, x_window, sizeof(x_window)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x45, y_window, sizeof(y_window)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x4e, x_counter, sizeof(x_counter)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x4f, y_counter, sizeof(y_counter)) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

static watchy_status_t write_ram(uint8_t command, const uint8_t *framebuffer) {
    if (set_ram_window() != WATCHY_STATUS_OK ||
        watchy_bus_display_command(command) != ESP_OK ||
        watchy_bus_display_data(framebuffer, WATCHY_DISPLAY_FRAMEBUFFER_SIZE) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_display_init(void) {
    static const uint8_t driver_output[] = {0xc7, 0x00, 0x00};
    static const uint8_t border[] = {0x05};
    static const uint8_t temperature[] = {0x80};
    gpio_config_t output_config = {
        .pin_bit_mask = (UINT64_C(1) << WATCHY_PIN_DISPLAY_DC) |
                        (UINT64_C(1) << WATCHY_PIN_DISPLAY_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t busy_config = {
        .pin_bit_mask = UINT64_C(1) << WATCHY_PIN_DISPLAY_BUSY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (s_ready) {
        return WATCHY_STATUS_OK;
    }
    if (!watchy_buses_ready() || gpio_config(&output_config) != ESP_OK ||
        gpio_config(&busy_config) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    gpio_set_level(WATCHY_PIN_DISPLAY_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(WATCHY_PIN_DISPLAY_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (watchy_bus_display_command(0x12) != ESP_OK || wait_ready() != WATCHY_STATUS_OK ||
        send_command_with_data(0x01, driver_output, sizeof(driver_output)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x3c, border, sizeof(border)) != WATCHY_STATUS_OK ||
        send_command_with_data(0x18, temperature, sizeof(temperature)) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }

    watchy_framebuffer_fill(s_framebuffer, sizeof(s_framebuffer), false);
    memcpy(s_previous_framebuffer, s_framebuffer, sizeof(s_previous_framebuffer));
    watchy_refresh_policy_reset(&s_refresh_policy, WATCHY_DISPLAY_PARTIAL_LIMIT);
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_display_ready(void) {
    return s_ready;
}

watchy_canvas_t watchy_display_acquire(void) {
    watchy_canvas_t canvas = {
        .width = WATCHY_DISPLAY_WIDTH,
        .height = WATCHY_DISPLAY_HEIGHT,
        .stride = WATCHY_DISPLAY_STRIDE,
        .rotation = 0,
        .format = WATCHY_PIXEL_MONO,
        .pixels = s_ready ? s_framebuffer : NULL,
    };
    return canvas;
}

watchy_status_t watchy_display_refresh(watchy_refresh_mode_t requested) {
    const watchy_refresh_mode_t mode = watchy_refresh_decide(&s_refresh_policy, requested);
    const uint8_t update_control = mode == WATCHY_REFRESH_FULL ? 0xf7 : 0xfc;

    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (write_ram(0x26, s_previous_framebuffer) != WATCHY_STATUS_OK ||
        write_ram(0x24, s_framebuffer) != WATCHY_STATUS_OK ||
        send_command_with_data(0x22, &update_control, 1) != WATCHY_STATUS_OK ||
        watchy_bus_display_command(0x20) != ESP_OK || wait_ready() != WATCHY_STATUS_OK ||
        write_ram(0x26, s_framebuffer) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    memcpy(s_previous_framebuffer, s_framebuffer, sizeof(s_previous_framebuffer));
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_display_power_off(void) {
    static const uint8_t power_off = 0x83;
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (send_command_with_data(0x22, &power_off, 1) != WATCHY_STATUS_OK ||
        watchy_bus_display_command(0x20) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return wait_ready();
}

watchy_status_t watchy_display_deep_sleep(void) {
    static const uint8_t check_code = 0x01;
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return send_command_with_data(0x10, &check_code, 1);
}

void watchy_display_deinit(void) {
    s_ready = false;
    gpio_set_level(WATCHY_PIN_DISPLAY_RESET, 0);
}
