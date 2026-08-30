#include "watchy/rtc.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/buses.h"
#include "watchy/rtc_calendar.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "nvs.h"

#define PCF8563_REG_STATUS_2 0x01u
#define PCF8563_REG_SECONDS 0x02u
#define PCF8563_REG_ALARM_MINUTE 0x09u
#define PCF8563_REG_TIMER_CONTROL 0x0eu
#define PCF8563_REG_TIMER_VALUE 0x0fu

#define PCF8563_STATUS_2_TIE (1u << 0)
#define PCF8563_STATUS_2_AIE (1u << 1)
#define PCF8563_STATUS_2_TF (1u << 2)
#define PCF8563_STATUS_2_AF (1u << 3)
#define WATCHY_TIME_NVS_NAMESPACE "watchy_time"
#define WATCHY_TIME_NVS_UTC_OFFSET "utc_offset"

static bool s_ready;
static int16_t s_utc_offset_minutes;

static uint8_t to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

static watchy_status_t restore_utc_offset(void) {
    nvs_handle_t handle = 0;
    int16_t offset = 0;
    esp_err_t error = nvs_open(WATCHY_TIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        s_utc_offset_minutes = 0;
        return WATCHY_STATUS_OK;
    }
    if (error != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = nvs_get_i16(handle, WATCHY_TIME_NVS_UTC_OFFSET, &offset);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        s_utc_offset_minutes = 0;
        return WATCHY_STATUS_OK;
    }
    if (error != ESP_OK || offset < -1439 || offset > 1439) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_utc_offset_minutes = offset;
    return WATCHY_STATUS_OK;
}

static watchy_status_t persist_utc_offset(int16_t offset) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WATCHY_TIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_i16(handle, WATCHY_TIME_NVS_UTC_OFFSET, offset);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (error == ESP_OK || handle != 0) {
        nvs_close(handle);
    }
    return error == ESP_OK ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_rtc_init(void) {
    uint8_t registers[7];
    watchy_time_t utc;
    gpio_config_t interrupt_config = {
        .pin_bit_mask = UINT64_C(1) << WATCHY_PIN_RTC_INTERRUPT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (!watchy_buses_ready() || gpio_config(&interrupt_config) != ESP_OK ||
        watchy_bus_rtc_read(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK ||
        restore_utc_offset() != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return watchy_pcf8563_decode(registers, &utc);
}

bool watchy_rtc_ready(void) {
    return s_ready;
}

watchy_status_t watchy_rtc_read_local(watchy_time_t *out_time) {
    uint8_t registers[7];
    watchy_time_t utc;
    int64_t unix_seconds;
    if (out_time == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (watchy_bus_rtc_read(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK ||
        watchy_pcf8563_decode(registers, &utc) != WATCHY_STATUS_OK ||
        watchy_calendar_to_unix(&utc, &unix_seconds) != WATCHY_STATUS_OK ||
        watchy_calendar_from_unix(unix_seconds, s_utc_offset_minutes, out_time) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_rtc_read_unix(int64_t *out_unix_seconds) {
    uint8_t registers[7];
    watchy_time_t utc;
    if (out_unix_seconds == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (watchy_bus_rtc_read(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK ||
        watchy_pcf8563_decode(registers, &utc) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return watchy_calendar_to_unix(&utc, out_unix_seconds);
}

watchy_status_t watchy_rtc_set_local(const watchy_time_t *time) {
    uint8_t registers[7];
    int64_t unix_seconds;
    watchy_time_t utc;
    if (time == NULL || !watchy_calendar_valid(time)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (watchy_calendar_to_unix(time, &unix_seconds) != WATCHY_STATUS_OK ||
        watchy_calendar_from_unix(unix_seconds, 0, &utc) != WATCHY_STATUS_OK ||
        watchy_pcf8563_encode(&utc, registers) != WATCHY_STATUS_OK ||
        persist_utc_offset(time->utc_offset_minutes) != WATCHY_STATUS_OK ||
        watchy_bus_rtc_write(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_utc_offset_minutes = time->utc_offset_minutes;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_rtc_set_minute_alarm(uint8_t minute) {
    uint8_t alarm[4];
    uint8_t status;
    if (minute > 59u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    alarm[0] = to_bcd(minute);
    alarm[1] = 0x80;
    alarm[2] = 0x80;
    alarm[3] = 0x80;
    if (watchy_bus_rtc_write(PCF8563_REG_ALARM_MINUTE, alarm, sizeof(alarm)) != ESP_OK ||
        watchy_bus_rtc_read(PCF8563_REG_STATUS_2, &status, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    status = (uint8_t)((status | PCF8563_STATUS_2_AIE) & ~PCF8563_STATUS_2_AF);
    return watchy_bus_rtc_write(PCF8563_REG_STATUS_2, &status, 1) == ESP_OK
               ? WATCHY_STATUS_OK
               : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_rtc_set_minute_timer(uint8_t minutes) {
    uint8_t status;
    const uint8_t timer_control = 0x83;
    if (minutes == 0u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (watchy_bus_rtc_read(PCF8563_REG_STATUS_2, &status, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    status = (uint8_t)((status | PCF8563_STATUS_2_TIE) & ~PCF8563_STATUS_2_TF);
    if (watchy_bus_rtc_write(PCF8563_REG_TIMER_VALUE, &minutes, 1) != ESP_OK ||
        watchy_bus_rtc_write(PCF8563_REG_TIMER_CONTROL, &timer_control, 1) != ESP_OK ||
        watchy_bus_rtc_write(PCF8563_REG_STATUS_2, &status, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_rtc_clear_interrupt_flags(void) {
    uint8_t status;
    if (!s_ready || watchy_bus_rtc_read(PCF8563_REG_STATUS_2, &status, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    status &= (uint8_t)~(PCF8563_STATUS_2_AF | PCF8563_STATUS_2_TF);
    return watchy_bus_rtc_write(PCF8563_REG_STATUS_2, &status, 1) == ESP_OK
               ? WATCHY_STATUS_OK
               : WATCHY_STATUS_INVALID_STATE;
}

void watchy_rtc_deinit(void) {
    s_ready = false;
}
