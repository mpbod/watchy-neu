#include "watchy/battery.h"

#include "watchy/board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#define WATCHY_BATTERY_ADC_CHANNEL ADC_CHANNEL_6
#define WATCHY_BATTERY_ADC_ATTENUATION ADC_ATTEN_DB_12
#define WATCHY_BATTERY_DIVIDER_MULTIPLIER 2u

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;

watchy_status_t watchy_battery_init(void) {
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = WATCHY_BATTERY_ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_line_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .atten = WATCHY_BATTERY_ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = 1100,
    };

    if (s_adc != NULL) {
        return WATCHY_STATUS_OK;
    }
    if (adc_oneshot_new_unit(&unit_config, &s_adc) != ESP_OK ||
        adc_oneshot_config_channel(s_adc, WATCHY_BATTERY_ADC_CHANNEL, &channel_config) != ESP_OK ||
        adc_cali_create_scheme_line_fitting(&calibration_config, &s_calibration) != ESP_OK) {
        watchy_battery_deinit();
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return WATCHY_STATUS_OK;
}

bool watchy_battery_ready(void) {
    return s_adc != NULL && s_calibration != NULL;
}

bool watchy_battery_charging_supported(void) {
    return false;
}

watchy_status_t watchy_battery_read(watchy_battery_state_t *out_state) {
    uint32_t raw_total = 0;
    int calibrated_mv;
    if (!watchy_battery_ready() || out_state == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    for (uint32_t sample = 0; sample < WATCHY_BATTERY_SAMPLE_COUNT; ++sample) {
        int raw;
        if (adc_oneshot_read(s_adc, WATCHY_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        raw_total += (uint32_t)raw;
    }
    if (adc_cali_raw_to_voltage(s_calibration,
                                (int)(raw_total / WATCHY_BATTERY_SAMPLE_COUNT),
                                &calibrated_mv) != ESP_OK || calibrated_mv < 0) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    uint32_t battery_mv = (uint32_t)calibrated_mv * WATCHY_BATTERY_DIVIDER_MULTIPLIER;
    if (battery_mv > UINT16_MAX) {
        battery_mv = UINT16_MAX;
    }
    out_state->millivolts = (uint16_t)battery_mv;
    out_state->percent = watchy_battery_percent_from_mv(out_state->millivolts);
    out_state->charging = false;
    return WATCHY_STATUS_OK;
}

void watchy_battery_deinit(void) {
    if (s_calibration != NULL) {
        adc_cali_delete_scheme_line_fitting(s_calibration);
        s_calibration = NULL;
    }
    if (s_adc != NULL) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
}
