#include "watchy/power.h"

#include "watchy/battery.h"
#include "watchy/board.h"
#include "watchy/buses.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_WAKE_SOURCE_TIMEOUT_MS 500
#define WATCHY_WAKE_SOURCE_SAMPLE_MS 10

static watchy_status_t s_last_prepare_status = WATCHY_STATUS_INVALID_STATE;
static bool s_prepare_attempted;
static const char *const TAG = "watchy_power";

typedef struct {
    bool buses_ready;
    bool battery_ready;
    bool display_ready;
    bool haptics_ready;
    bool motion_ready;
    bool rtc_ready;
} awake_service_state_t;

static watchy_raw_wake_cause_t raw_wake_cause(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return WATCHY_RAW_WAKE_UNDEFINED;
        case ESP_SLEEP_WAKEUP_EXT0:
            return WATCHY_RAW_WAKE_EXT0;
        case ESP_SLEEP_WAKEUP_EXT1:
            return WATCHY_RAW_WAKE_EXT1;
        case ESP_SLEEP_WAKEUP_TIMER:
            return WATCHY_RAW_WAKE_TIMER;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return WATCHY_RAW_WAKE_TOUCH;
        case ESP_SLEEP_WAKEUP_ULP:
            return WATCHY_RAW_WAKE_ULP;
        default:
            return WATCHY_RAW_WAKE_OTHER;
    }
}

watchy_wake_cause_t watchy_power_capture_wake_cause(void) {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const uint64_t gpio_mask = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
    ESP_LOGI(TAG, "wake raw=%d reset=%d ext1=0x%016llx",
             (int)cause, (int)esp_reset_reason(), (unsigned long long)gpio_mask);
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && esp_reset_reason() != ESP_RST_POWERON) {
        return WATCHY_WAKE_OTHER;
    }
    return watchy_power_map_wake(raw_wake_cause(cause), gpio_mask);
}

static void release_unused_pins(void) {
    static const gpio_num_t pins[] = {
        WATCHY_PIN_I2C_SDA,
        WATCHY_PIN_I2C_SCL,
        WATCHY_PIN_SPI_SCK,
        WATCHY_PIN_SPI_MOSI,
        WATCHY_PIN_DISPLAY_CS,
        WATCHY_PIN_DISPLAY_DC,
        WATCHY_PIN_DISPLAY_RESET,
        WATCHY_PIN_DISPLAY_BUSY,
        WATCHY_PIN_BATTERY_ADC,
        WATCHY_PIN_MOTOR,
    };
    for (size_t index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (watchy_power_release_pin_for_sleep((uint8_t)pins[index])) {
            gpio_reset_pin(pins[index]);
        }
    }
}

static bool prepare_motor_for_sleep(void) {
    return watchy_haptics_deinit() == WATCHY_STATUS_OK &&
           watchy_haptics_hold_off_for_sleep() == WATCHY_STATUS_OK;
}

static bool prepare_motion_wake_source(bool motion_wake_enabled) {
    if (!watchy_power_boot_should_initialize_motion(true,
                                                     motion_wake_enabled)) {
        return false;
    }
    if (!watchy_motion_ready() && watchy_motion_init() != WATCHY_STATUS_OK) {
        return false;
    }
    return watchy_motion_configure_wake(true) == WATCHY_STATUS_OK;
}

static awake_service_state_t capture_awake_service_state(void) {
    return (awake_service_state_t){
        .buses_ready = watchy_buses_ready(),
        .battery_ready = watchy_battery_ready(),
        .display_ready = watchy_display_ready(),
        .haptics_ready = watchy_haptics_ready(),
        .motion_ready = watchy_motion_ready(),
        .rtc_ready = watchy_rtc_ready(),
    };
}

static void teardown_awake_services(void) {
    watchy_battery_deinit();
    watchy_motion_deinit();
    watchy_display_deinit();
    watchy_rtc_deinit();
    watchy_buses_deinit();
    release_unused_pins();
}

static bool restore_awake_services(const awake_service_state_t *state) {
    bool restored = true;

    if (state->buses_ready && watchy_buses_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    if (state->rtc_ready && watchy_rtc_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    if (state->battery_ready && watchy_battery_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    if (state->display_ready && watchy_display_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    if (state->haptics_ready && watchy_haptics_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    if (state->motion_ready && watchy_motion_init() != WATCHY_STATUS_OK) {
        restored = false;
    }
    return restored;
}

static void restore_wake_gpio_ownership(void) {
    static const gpio_num_t pins[] = {
        WATCHY_PIN_RTC_INTERRUPT,
        WATCHY_PIN_BUTTON_MENU,
        WATCHY_PIN_BUTTON_BACK,
        WATCHY_PIN_BUTTON_DOWN,
        WATCHY_PIN_BUTTON_UP,
    };
    for (size_t index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        (void)rtc_gpio_deinit(pins[index]);
    }
}

static watchy_status_t fail_sleep_preparation(
    const awake_service_state_t *awake_state,
    bool resume_buttons,
    bool restore_wake_gpios) {
    if (restore_wake_gpios) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        restore_wake_gpio_ownership();
    }
    if (resume_buttons && watchy_buttons_resume() != WATCHY_STATUS_OK) {
        ESP_LOGE(TAG, "button input resume failed after sleep preparation error");
    }
    if (!restore_awake_services(awake_state)) {
        ESP_LOGE(TAG, "awake peripheral restore failed after sleep preparation error");
    }
    s_last_prepare_status = WATCHY_STATUS_INVALID_STATE;
    return s_last_prepare_status;
}

static watchy_status_t cancel_sleep_for_input(
    const awake_service_state_t *awake_state,
    watchy_status_t quiesce_status) {
    const bool buttons_resumed = watchy_buttons_resume() == WATCHY_STATUS_OK;
    const bool services_restored = restore_awake_services(awake_state);

    if (!buttons_resumed) {
        ESP_LOGE(TAG, "button input resume failed after sleep veto");
    }
    if (!services_restored) {
        ESP_LOGE(TAG, "awake peripheral restore failed after sleep veto");
    }
    s_last_prepare_status = watchy_power_sleep_handoff_status(
        quiesce_status, true, buttons_resumed && services_restored);
    return s_last_prepare_status;
}

static bool prepare_button_wake_pins(void) {
    static const gpio_num_t pins[] = {
        WATCHY_PIN_BUTTON_MENU,
        WATCHY_PIN_BUTTON_BACK,
        WATCHY_PIN_BUTTON_DOWN,
        WATCHY_PIN_BUTTON_UP,
    };
    for (size_t index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        const gpio_num_t pin = pins[index];
        if (rtc_gpio_init(pin) != ESP_OK ||
            rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK) {
            return false;
        }
        if (watchy_power_button_needs_internal_pulldown((uint8_t)pin) &&
            (rtc_gpio_pullup_dis(pin) != ESP_OK || rtc_gpio_pulldown_en(pin) != ESP_OK)) {
            return false;
        }
    }
    return true;
}

static bool wait_for_wake_sources_inactive(bool motion_wake_enabled, bool rtc_wake_enabled) {
    watchy_wake_source_filter_t filter = {0};
    const int64_t deadline = esp_timer_get_time() + WATCHY_WAKE_SOURCE_TIMEOUT_MS * INT64_C(1000);
    for (;;) {
        uint64_t active = 0;
        static const gpio_num_t active_high_sources[] = {
            WATCHY_PIN_BUTTON_MENU,
            WATCHY_PIN_BUTTON_BACK,
            WATCHY_PIN_BUTTON_DOWN,
            WATCHY_PIN_BUTTON_UP,
        };
        for (size_t index = 0;
             index < sizeof(active_high_sources) / sizeof(active_high_sources[0]); ++index) {
            if (gpio_get_level(active_high_sources[index]) != 0) {
                active |= UINT64_C(1) << active_high_sources[index];
            }
        }
        if (motion_wake_enabled && gpio_get_level(WATCHY_PIN_BMA423_INTERRUPT_1) != 0) {
            active |= UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1;
        }
        if (rtc_wake_enabled &&
            gpio_get_level(WATCHY_PIN_RTC_INTERRUPT) == WATCHY_RTC_INTERRUPT_ACTIVE_LEVEL) {
            active |= UINT64_C(1) << WATCHY_PIN_RTC_INTERRUPT;
        }
        if (watchy_power_wake_sources_observe(&filter, active)) {
            return true;
        }
        if (esp_timer_get_time() >= deadline) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(WATCHY_WAKE_SOURCE_SAMPLE_MS));
    }
}

watchy_status_t watchy_power_prepare_deep_sleep_with_motion(bool timer_configured,
                                                            bool motion_wake_enabled) {
    const awake_service_state_t awake_state = capture_awake_service_state();
    bool sleep_veto = false;
    watchy_status_t quiesce_status;
    uint64_t ext1_mask = (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU) |
                         (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                         (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN) |
                         (UINT64_C(1) << WATCHY_PIN_BUTTON_UP);
    if (motion_wake_enabled) {
        ext1_mask |= UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1;
    }
    watchy_sleep_requirements_t requirements = {
        .timer_configured = timer_configured,
        .radios_stopped = watchy_radios_stop_all() == WATCHY_STATUS_OK,
        .motor_off = prepare_motor_for_sleep(),
        .display_hibernated = watchy_display_ready() &&
                              watchy_display_power_off() == WATCHY_STATUS_OK &&
                              watchy_display_deep_sleep() == WATCHY_STATUS_OK,
        .rtc_source_cleared = watchy_rtc_ready() &&
                              watchy_rtc_clear_interrupt_flags() == WATCHY_STATUS_OK,
        .motion_wake_enabled = motion_wake_enabled,
        .motion_source_configured = prepare_motion_wake_source(motion_wake_enabled),
    };
    s_prepare_attempted = true;

    teardown_awake_services();
    requirements.sources_inactive = wait_for_wake_sources_inactive(motion_wake_enabled, true);
    /* Wake APIs are applied after pin release; mark them provisionally for prerequisite gating. */
    requirements.ext0_configured = true;
    requirements.ext1_configured = true;
    requirements.buttons_quiesced = true;
    if (!watchy_power_sleep_allowed(&requirements)) {
        return fail_sleep_preparation(&awake_state, false, false);
    }

    quiesce_status = watchy_buttons_quiesce_for_sleep(&sleep_veto);
    requirements.buttons_quiesced = quiesce_status == WATCHY_STATUS_OK;
    if (quiesce_status != WATCHY_STATUS_OK) {
        return fail_sleep_preparation(&awake_state, true, false);
    }
    if (sleep_veto) {
        return cancel_sleep_for_input(&awake_state, quiesce_status);
    }

    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_init(WATCHY_PIN_RTC_INTERRUPT);
    rtc_gpio_set_direction(WATCHY_PIN_RTC_INTERRUPT, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(WATCHY_PIN_RTC_INTERRUPT);
    rtc_gpio_pulldown_dis(WATCHY_PIN_RTC_INTERRUPT);
    requirements.ext0_configured =
        esp_sleep_enable_ext0_wakeup(WATCHY_PIN_RTC_INTERRUPT,
                                     WATCHY_RTC_INTERRUPT_ACTIVE_LEVEL) == ESP_OK;
    requirements.ext1_configured =
        prepare_button_wake_pins() &&
        esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH) == ESP_OK;
    if (!watchy_power_sleep_allowed(&requirements)) {
        return fail_sleep_preparation(&awake_state, true, true);
    }
    s_last_prepare_status = WATCHY_STATUS_OK;
    return s_last_prepare_status;
}

watchy_status_t watchy_power_prepare_deep_sleep(bool timer_configured) {
    return watchy_power_prepare_deep_sleep_with_motion(timer_configured, true);
}

watchy_status_t watchy_power_prepare_button_only_sleep(void) {
    const awake_service_state_t awake_state = capture_awake_service_state();
    bool sleep_veto = false;
    watchy_status_t quiesce_status;
    const uint64_t button_mask = (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU) |
                                 (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                                 (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN) |
                                 (UINT64_C(1) << WATCHY_PIN_BUTTON_UP);
    watchy_sleep_requirements_t requirements = {
        .radios_stopped = watchy_radios_stop_all() == WATCHY_STATUS_OK,
        .motor_off = prepare_motor_for_sleep(),
        .display_hibernated = watchy_display_ready() &&
                              watchy_display_power_off() == WATCHY_STATUS_OK &&
                              watchy_display_deep_sleep() == WATCHY_STATUS_OK,
        .ext1_configured = true,
        .button_only = true,
    };
    s_prepare_attempted = true;

    teardown_awake_services();
    requirements.sources_inactive = wait_for_wake_sources_inactive(false, false);
    requirements.buttons_quiesced = true;
    if (!watchy_power_sleep_allowed(&requirements)) {
        return fail_sleep_preparation(&awake_state, false, false);
    }

    quiesce_status = watchy_buttons_quiesce_for_sleep(&sleep_veto);
    requirements.buttons_quiesced = quiesce_status == WATCHY_STATUS_OK;
    if (quiesce_status != WATCHY_STATUS_OK) {
        return fail_sleep_preparation(&awake_state, true, false);
    }
    if (sleep_veto) {
        return cancel_sleep_for_input(&awake_state, quiesce_status);
    }

    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    requirements.ext1_configured =
        prepare_button_wake_pins() &&
        esp_sleep_enable_ext1_wakeup(button_mask, ESP_EXT1_WAKEUP_ANY_HIGH) == ESP_OK;
    if (!watchy_power_sleep_allowed(&requirements)) {
        return fail_sleep_preparation(&awake_state, true, true);
    }
    s_last_prepare_status = WATCHY_STATUS_OK;
    return s_last_prepare_status;
}

watchy_status_t watchy_power_last_prepare_status(void) {
    return s_last_prepare_status;
}

bool watchy_power_prepare_attempted(void) {
    return s_prepare_attempted;
}

void watchy_power_enter_deep_sleep(void) {
    esp_deep_sleep_start();
    for (;;) {
    }
}
