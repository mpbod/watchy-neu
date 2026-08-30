#include "watchy/power.h"

#include "watchy/battery.h"
#include "watchy/board.h"
#include "watchy/buses.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

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
        gpio_reset_pin(pins[index]);
    }
}

watchy_status_t watchy_power_prepare_deep_sleep(void) {
    const uint64_t ext1_mask = (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU) |
                               (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                               (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN) |
                               (UINT64_C(1) << WATCHY_PIN_BUTTON_UP) |
                               (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1) |
                               (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_2);
    watchy_status_t result = WATCHY_STATUS_OK;

    watchy_radios_stop_all();
    watchy_haptics_deinit();
    if (watchy_display_ready()) {
        if (watchy_display_power_off() != WATCHY_STATUS_OK ||
            watchy_display_deep_sleep() != WATCHY_STATUS_OK) {
            result = WATCHY_STATUS_INVALID_STATE;
        }
    }
    if (watchy_rtc_ready() && watchy_rtc_clear_interrupt_flags() != WATCHY_STATUS_OK) {
        result = WATCHY_STATUS_INVALID_STATE;
    }
    if (watchy_motion_ready() && watchy_motion_configure_wake(true) != WATCHY_STATUS_OK) {
        result = WATCHY_STATUS_INVALID_STATE;
    }

    watchy_battery_deinit();
    watchy_motion_deinit();
    watchy_display_deinit();
    watchy_rtc_deinit();
    watchy_buses_deinit();
    release_unused_pins();

    rtc_gpio_init(WATCHY_PIN_RTC_INTERRUPT);
    rtc_gpio_set_direction(WATCHY_PIN_RTC_INTERRUPT, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(WATCHY_PIN_RTC_INTERRUPT);
    rtc_gpio_pulldown_dis(WATCHY_PIN_RTC_INTERRUPT);
    if (esp_sleep_enable_ext0_wakeup(WATCHY_PIN_RTC_INTERRUPT,
                                     WATCHY_RTC_INTERRUPT_ACTIVE_LEVEL) != ESP_OK ||
        esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return result;
}

void watchy_power_enter_deep_sleep(void) {
    esp_deep_sleep_start();
    for (;;) {
    }
}
