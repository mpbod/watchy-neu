#include "watchy/display.h"
#include "watchy/display_policy.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/buses.h"
#include "watchy/buttons.h"
#include "watchy/watchdog.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_DISPLAY_BUSY_TIMEOUT_MS 10000
#define WATCHY_DISPLAY_PARTIAL_LIMIT 20u

static uint8_t s_framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
static uint8_t s_target_framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
static uint8_t s_cold_previous_framebuffer[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
static RTC_DATA_ATTR watchy_display_retained_state_t s_retained;
static bool s_ready;
static bool s_hibernated;
static bool s_presenting;
static uint16_t s_partial_limit = WATCHY_DISPLAY_PARTIAL_LIMIT;
static watchy_transition_level_t s_transition_level = WATCHY_TRANSITION_LEVEL_FULL;
static bool s_transition_attended;
static bool s_transition_safe_mode;
static uint16_t s_transition_battery_mv;
static watchy_button_mask_t s_cancelled_buttons;
static bool s_force_cut;

typedef struct {
    watchy_button_mask_t baseline_buttons;
    watchy_button_mask_t cancelled_buttons;
    watchy_watchdog_scope_t *watchdog;
    bool watchdog_feed_failed;
} watchy_display_hardware_context_t;

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
    watchy_display_busy_filter_t filter = {0};
    const int64_t deadline = esp_timer_get_time() + WATCHY_DISPLAY_BUSY_TIMEOUT_MS * INT64_C(1000);
    for (;;) {
        if (watchy_display_busy_observe(&filter,
                                        gpio_get_level(WATCHY_PIN_DISPLAY_BUSY) != 0)) {
            return WATCHY_STATUS_OK;
        }
        if (esp_timer_get_time() >= deadline) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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

static watchy_status_t refresh_current(watchy_refresh_mode_t completed) {
    const bool retained_valid = watchy_display_retained_valid(&s_retained);
    const uint8_t update_control = completed == WATCHY_REFRESH_FULL ? 0xf7 : 0xfc;
    const uint8_t *previous = retained_valid ? s_retained.previous_frame
                                             : s_cold_previous_framebuffer;

    if (write_ram(0x26, previous) != WATCHY_STATUS_OK ||
        write_ram(0x24, s_framebuffer) != WATCHY_STATUS_OK ||
        send_command_with_data(0x22, &update_control, 1) != WATCHY_STATUS_OK ||
        watchy_bus_display_command(0x20) != ESP_OK || wait_ready() != WATCHY_STATUS_OK ||
        write_ram(0x26, s_framebuffer) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

static watchy_status_t transition_write(void *context,
                                        const uint8_t *frame,
                                        watchy_refresh_mode_t completed) {
    (void)context;

    if (frame == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (frame != s_framebuffer) {
        memcpy(s_framebuffer, frame, sizeof(s_framebuffer));
    }
    return refresh_current(completed);
}

static bool transition_cancel(void *context) {
    watchy_display_hardware_context_t *hardware =
        (watchy_display_hardware_context_t *)context;
    const watchy_button_mask_t current = watchy_buttons_sample();

    if (hardware == NULL) {
        return false;
    }
    hardware->cancelled_buttons |=
        watchy_display_new_button_mask(hardware->baseline_buttons, current);
    return hardware->watchdog_feed_failed || hardware->cancelled_buttons != 0u;
}

static void transition_feed_watchdog(void *context) {
    watchy_display_hardware_context_t *hardware =
        (watchy_display_hardware_context_t *)context;

    if (hardware != NULL &&
        (hardware->watchdog == NULL ||
         watchy_watchdog_scope_feed(hardware->watchdog) != WATCHY_STATUS_OK)) {
        hardware->watchdog_feed_failed = true;
    }
}

static watchy_watchdog_membership_t task_watchdog_status(void *context) {
    const esp_err_t status = esp_task_wdt_status(NULL);
    (void)context;
    if (status == ESP_OK) {
        return WATCHY_WATCHDOG_ENROLLED;
    }
    return status == ESP_ERR_NOT_FOUND ? WATCHY_WATCHDOG_NOT_ENROLLED
                                       : WATCHY_WATCHDOG_MEMBERSHIP_ERROR;
}

static bool task_watchdog_enroll(void *context) {
    (void)context;
    return esp_task_wdt_add(NULL) == ESP_OK;
}

static bool task_watchdog_feed(void *context) {
    (void)context;
    return esp_task_wdt_reset() == ESP_OK;
}

static bool task_watchdog_unenroll(void *context) {
    (void)context;
    return esp_task_wdt_delete(NULL) == ESP_OK;
}

static const watchy_watchdog_ops_t s_task_watchdog_ops = {
    .status = task_watchdog_status,
    .enroll = task_watchdog_enroll,
    .feed = task_watchdog_feed,
    .unenroll = task_watchdog_unenroll,
};

static bool valid_transition_level(watchy_transition_level_t level) {
    return level == WATCHY_TRANSITION_LEVEL_FULL ||
           level == WATCHY_TRANSITION_LEVEL_REDUCED || level == WATCHY_TRANSITION_LEVEL_OFF;
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
    s_hibernated = false;
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
    if (!watchy_display_retained_valid(&s_retained)) {
        watchy_framebuffer_fill(s_cold_previous_framebuffer,
                                sizeof(s_cold_previous_framebuffer), false);
    }
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_display_ready(void) {
    return s_ready;
}

watchy_status_t watchy_display_set_partial_limit(uint16_t partial_limit) {
    if (partial_limit == 0u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    s_partial_limit = partial_limit;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_display_set_transition_policy(watchy_transition_level_t level,
                                                     bool attended,
                                                     bool safe_mode,
                                                     uint16_t battery_mv) {
    if (!valid_transition_level(level)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    s_transition_level = level;
    s_transition_attended = attended;
    s_transition_safe_mode = safe_mode;
    s_transition_battery_mv = battery_mv;
    return WATCHY_STATUS_OK;
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

watchy_status_t watchy_display_present(watchy_refresh_mode_t requested,
                                       const watchy_transition_request_v1_t *request) {
    const bool source_valid = watchy_display_retained_valid(&s_retained);
    watchy_transition_policy_context_t policy = {
        .level = s_transition_level,
        .attended = s_transition_attended,
        .safe_mode = s_transition_safe_mode,
        .source_valid = source_valid,
        .clear_required = source_valid && requested == WATCHY_REFRESH_PARTIAL &&
                          watchy_display_prepare_refresh(&s_retained, requested,
                                                         s_partial_limit) == WATCHY_REFRESH_FULL,
        .battery_mv = s_transition_battery_mv,
    };
    watchy_transition_plan_t plan;
    watchy_transition_result_t result;
    watchy_display_hardware_context_t hardware;
    watchy_watchdog_scope_t watchdog = {0};
    watchy_display_transition_io_t io;
    watchy_status_t status;

    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (requested != WATCHY_REFRESH_PARTIAL && requested != WATCHY_REFRESH_FULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (s_presenting) {
        return WATCHY_STATUS_BUSY;
    }
    if (watchy_transition_plan(s_force_cut ? NULL : request, &policy, &plan) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (watchy_watchdog_scope_begin(&watchdog, &s_task_watchdog_ops) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }

    memcpy(s_target_framebuffer, s_framebuffer, sizeof(s_target_framebuffer));
    s_force_cut = false;
    hardware = (watchy_display_hardware_context_t){
        .baseline_buttons = watchy_buttons_sample(),
        .watchdog = &watchdog,
    };
    io = (watchy_display_transition_io_t){
        .retained = &s_retained,
        .partial_limit = s_partial_limit,
        .target_requested = requested,
        .physical_write = transition_write,
        .cancel = transition_cancel,
        .feed = transition_feed_watchdog,
        .context = &hardware,
    };
    s_presenting = true;
    status = watchy_display_execute_plan(&plan, s_cold_previous_framebuffer,
                                         s_target_framebuffer, s_cold_previous_framebuffer,
                                         s_framebuffer, sizeof(s_framebuffer), &io, &result);
    s_presenting = false;

    status = watchy_display_execution_status(status, &result,
                                             hardware.watchdog_feed_failed);
    if (status == WATCHY_STATUS_CANCELLED) {
        if (hardware.cancelled_buttons == 0u) {
            status = WATCHY_STATUS_INVALID_STATE;
        } else {
            s_cancelled_buttons |= hardware.cancelled_buttons;
            s_force_cut = true;
        }
    }
    if (watchy_watchdog_scope_end(&watchdog) != WATCHY_STATUS_OK) {
        status = WATCHY_STATUS_INVALID_STATE;
    }
    return status;
}

bool watchy_display_take_cancelled_buttons(watchy_button_mask_t *out_buttons) {
    if (out_buttons == NULL) {
        return false;
    }
    *out_buttons = s_cancelled_buttons;
    s_cancelled_buttons = 0u;
    return *out_buttons != 0u;
}

watchy_status_t watchy_display_refresh(watchy_refresh_mode_t requested) {
    return watchy_display_present(requested, NULL);
}

void watchy_display_invalidate_previous(void) {
    watchy_display_invalidate_retained(&s_retained);
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
    const watchy_status_t status = send_command_with_data(0x10, &check_code, 1);
    if (status == WATCHY_STATUS_OK) {
        s_hibernated = true;
    }
    return status;
}

void watchy_display_deinit(void) {
    s_ready = false;
    if (s_hibernated) {
        gpio_set_direction(WATCHY_PIN_DISPLAY_RESET, GPIO_MODE_INPUT);
        gpio_set_direction(WATCHY_PIN_DISPLAY_DC, GPIO_MODE_INPUT);
    } else {
        gpio_set_level(WATCHY_PIN_DISPLAY_RESET, 0);
    }
}
