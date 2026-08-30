#include "watchy/motion.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/buses.h"

#include <cmath>
#include <memory>
#include <new>

#include "driver/gpio.h"
#include "SensorBMA423.hpp"

namespace {

std::unique_ptr<SensorBMA423> sensor;
bool steps_supported;

int16_t mps2_to_mg(float value) {
    float milligravity = value * (1000.0f / 9.80665f);
    if (milligravity > 32767.0f) {
        milligravity = 32767.0f;
    } else if (milligravity < -32768.0f) {
        milligravity = -32768.0f;
    }
    return static_cast<int16_t>(milligravity);
}

}  // namespace

extern "C" watchy_status_t watchy_motion_init(void) {
    gpio_config_t interrupt_config = {
        .pin_bit_mask = (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_1) |
                        (UINT64_C(1) << WATCHY_PIN_BMA423_INTERRUPT_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (sensor) {
        return WATCHY_STATUS_OK;
    }
    if (!watchy_buses_ready() || gpio_config(&interrupt_config) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    sensor.reset(new (std::nothrow) SensorBMA423());
    if (!sensor || !sensor->begin(watchy_bus_i2c_handle(), BMA4XX_I2C_ADDR_SDO_LOW)) {
        sensor.reset();
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!sensor->configAccelerometer(OperationMode::NORMAL,
                                     AccelFullScaleRange::FS_2G,
                                     50.0f,
                                     AccelBandwidth::OSR2_AVG2,
                                     AccelPerfMode::CIC_AVG_MODE)) {
        sensor.reset();
        return WATCHY_STATUS_INVALID_STATE;
    }
    steps_supported = sensor->enableStepCounter(true, 1, false);
    return WATCHY_STATUS_OK;
}

extern "C" bool watchy_motion_ready(void) {
    return sensor != nullptr;
}

extern "C" watchy_status_t watchy_motion_read(watchy_motion_data_t *out_data) {
    AccelerometerData data;
    if (out_data == nullptr) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!sensor) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!sensor->readData(data)) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    out_data->x_mg = mps2_to_mg(data.mps2.x);
    out_data->y_mg = mps2_to_mg(data.mps2.y);
    out_data->z_mg = mps2_to_mg(data.mps2.z);
    if (std::isfinite(data.temperature)) {
        out_data->temperature_status = WATCHY_FEATURE_SUPPORTED;
        out_data->temperature_centi_c = static_cast<int16_t>(data.temperature * 100.0f);
    } else {
        out_data->temperature_status = WATCHY_FEATURE_UNAVAILABLE;
        out_data->temperature_centi_c = 0;
    }
    if (steps_supported) {
        out_data->steps_status = WATCHY_FEATURE_SUPPORTED;
        out_data->steps = sensor->getStepCount();
    } else {
        out_data->steps_status = WATCHY_FEATURE_UNSUPPORTED;
        out_data->steps = 0;
    }
    return WATCHY_STATUS_OK;
}

extern "C" watchy_status_t watchy_motion_configure_wake(bool enable) {
    SensorBMA423::MotionAxesConfig axes(1, 1, 1);
    if (!sensor) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!sensor->setInterruptPinConfig(InterruptPinMap::PIN1, false, false, true, false) ||
        !sensor->enableAnyMotionDetection(axes, enable, enable, InterruptPinMap::PIN1)) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

extern "C" void watchy_motion_deinit(void) {
    steps_supported = false;
    sensor.reset();
}
