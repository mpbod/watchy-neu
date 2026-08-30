#include "watchy/rtc.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/buses.h"

#include "driver/gpio.h"
#include "esp_err.h"

#define PCF8563_REG_STATUS_2 0x01u
#define PCF8563_REG_SECONDS 0x02u
#define PCF8563_REG_ALARM_MINUTE 0x09u
#define PCF8563_REG_TIMER_CONTROL 0x0eu
#define PCF8563_REG_TIMER_VALUE 0x0fu

#define PCF8563_STATUS_2_TIE (1u << 0)
#define PCF8563_STATUS_2_AIE (1u << 1)
#define PCF8563_STATUS_2_TF (1u << 2)
#define PCF8563_STATUS_2_AF (1u << 3)

static bool s_ready;
static int16_t s_utc_offset_minutes;

static uint8_t to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4u) * 10u) + (value & 0x0fu));
}

static bool valid_time(const watchy_time_t *time) {
    return time != NULL && time->year >= 2000 && time->year <= 2099 && time->month >= 1 &&
           time->month <= 12 && time->day >= 1 && time->day <= 31 && time->hour <= 23 &&
           time->minute <= 59 && time->second <= 59 && time->weekday <= 6;
}

watchy_status_t watchy_rtc_init(void) {
    uint8_t seconds;
    gpio_config_t interrupt_config = {
        .pin_bit_mask = UINT64_C(1) << WATCHY_PIN_RTC_INTERRUPT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (!watchy_buses_ready() || gpio_config(&interrupt_config) != ESP_OK ||
        watchy_bus_rtc_read(PCF8563_REG_SECONDS, &seconds, 1) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return (seconds & 0x80u) == 0u ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

bool watchy_rtc_ready(void) {
    return s_ready;
}

watchy_status_t watchy_rtc_read_local(watchy_time_t *out_time) {
    uint8_t registers[7];
    if (!s_ready || out_time == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (watchy_bus_rtc_read(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK ||
        (registers[0] & 0x80u) != 0u) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    out_time->second = from_bcd(registers[0] & 0x7fu);
    out_time->minute = from_bcd(registers[1] & 0x7fu);
    out_time->hour = from_bcd(registers[2] & 0x3fu);
    out_time->day = from_bcd(registers[3] & 0x3fu);
    out_time->weekday = from_bcd(registers[4] & 0x07u);
    out_time->month = from_bcd(registers[5] & 0x1fu);
    out_time->year = (int16_t)(2000 + from_bcd(registers[6]));
    out_time->utc_offset_minutes = s_utc_offset_minutes;
    return valid_time(out_time) ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

static int64_t days_from_civil(int32_t year, uint32_t month, uint32_t day) {
    const int32_t adjusted_year = year - (month <= 2u ? 1 : 0);
    const int32_t era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    const uint32_t year_of_era = (uint32_t)(adjusted_year - era * 400);
    const uint32_t shifted_month = month > 2u ? month - 3u : month + 9u;
    const uint32_t day_of_year = (153u * shifted_month + 2u) / 5u + day - 1u;
    const uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

watchy_status_t watchy_rtc_read_unix(int64_t *out_unix_seconds) {
    watchy_time_t local;
    watchy_status_t status;
    if (out_unix_seconds == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    status = watchy_rtc_read_local(&local);
    if (status != WATCHY_STATUS_OK) {
        return status;
    }
    *out_unix_seconds = days_from_civil(local.year, local.month, local.day) * INT64_C(86400) +
                        (int64_t)local.hour * 3600 + (int64_t)local.minute * 60 + local.second -
                        (int64_t)local.utc_offset_minutes * 60;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_rtc_set_local(const watchy_time_t *time) {
    uint8_t registers[7];
    if (!s_ready || !valid_time(time)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    registers[0] = to_bcd(time->second);
    registers[1] = to_bcd(time->minute);
    registers[2] = to_bcd(time->hour);
    registers[3] = to_bcd(time->day);
    registers[4] = to_bcd(time->weekday);
    registers[5] = to_bcd(time->month);
    registers[6] = to_bcd((uint8_t)(time->year - 2000));
    if (watchy_bus_rtc_write(PCF8563_REG_SECONDS, registers, sizeof(registers)) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_utc_offset_minutes = time->utc_offset_minutes;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_rtc_set_minute_alarm(uint8_t minute) {
    uint8_t alarm[4];
    uint8_t status;
    if (!s_ready || minute > 59u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
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
    if (!s_ready || minutes == 0u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
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
