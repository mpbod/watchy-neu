#ifndef WATCHY_SDK_HPP
#define WATCHY_SDK_HPP

#include "watchy/sdk.h"

namespace watchy {

class Canvas {
public:
    explicit Canvas(const watchy_canvas_api_v1_t *api = nullptr) noexcept : api_(api) {}

    bool valid() const noexcept { return api_ != nullptr; }
    watchy_canvas_t acquire() const noexcept {
        return (api_ != nullptr && api_->acquire != nullptr) ? api_->acquire(api_->context) : watchy_canvas_t{};
    }
    void release(const watchy_canvas_t *canvas) const noexcept {
        if (api_ != nullptr && api_->release != nullptr) {
            api_->release(api_->context, canvas);
        }
    }
    watchy_status_t request_refresh(watchy_refresh_mode_t mode) const noexcept {
        return (api_ != nullptr && api_->request_refresh != nullptr)
                   ? api_->request_refresh(api_->context, mode)
                   : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_canvas_api_v1_t *api_;
};

class Clock {
public:
    explicit Clock(const watchy_clock_api_v1_t *api = nullptr) noexcept : api_(api) {}

    watchy_status_t now(watchy_time_t *out_time) const noexcept {
        return (api_ != nullptr && api_->now != nullptr) ? api_->now(api_->context, out_time)
                                                          : WATCHY_STATUS_UNSUPPORTED;
    }
    watchy_status_t set_alarm(const watchy_time_t *alarm_time) const noexcept {
        return (api_ != nullptr && api_->set_alarm != nullptr)
                   ? api_->set_alarm(api_->context, alarm_time)
                   : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_clock_api_v1_t *api_;
};

class Input {
public:
    explicit Input(const watchy_input_api_v1_t *api = nullptr) noexcept : api_(api) {}

    bool is_pressed(watchy_button_t button) const noexcept {
        return api_ != nullptr && api_->is_pressed != nullptr && api_->is_pressed(api_->context, button);
    }

private:
    const watchy_input_api_v1_t *api_;
};

class Motion {
public:
    explicit Motion(const watchy_motion_api_v1_t *api = nullptr) noexcept : api_(api) {}

    watchy_status_t sample(watchy_motion_sample_t *out_sample) const noexcept {
        return (api_ != nullptr && api_->sample != nullptr) ? api_->sample(api_->context, out_sample)
                                                             : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_motion_api_v1_t *api_;
};

class Battery {
public:
    explicit Battery(const watchy_battery_api_v1_t *api = nullptr) noexcept : api_(api) {}

    watchy_status_t read(watchy_battery_state_t *out_state) const noexcept {
        return (api_ != nullptr && api_->read != nullptr) ? api_->read(api_->context, out_state)
                                                           : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_battery_api_v1_t *api_;
};

class Haptics {
public:
    explicit Haptics(const watchy_haptics_api_v1_t *api = nullptr) noexcept : api_(api) {}

    watchy_status_t pulse(uint16_t duration_ms, uint8_t strength) const noexcept {
        return (api_ != nullptr && api_->pulse != nullptr)
                   ? api_->pulse(api_->context, duration_ms, strength)
                   : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_haptics_api_v1_t *api_;
};

class Storage {
public:
    explicit Storage(const watchy_storage_api_v1_t *api = nullptr) noexcept : api_(api) {}

    watchy_status_t read(const char *path, void *buffer, size_t buffer_size, size_t *out_size) const noexcept {
        return (api_ != nullptr && api_->read != nullptr)
                   ? api_->read(api_->context, path, buffer, buffer_size, out_size)
                   : WATCHY_STATUS_UNSUPPORTED;
    }
    watchy_status_t write(const char *path, const void *data, size_t data_size) const noexcept {
        return (api_ != nullptr && api_->write != nullptr) ? api_->write(api_->context, path, data, data_size)
                                                            : WATCHY_STATUS_UNSUPPORTED;
    }

private:
    const watchy_storage_api_v1_t *api_;
};

class Network {
public:
    explicit Network(const watchy_network_api_v1_t *api = nullptr) noexcept : api_(api) {}

    bool connected() const noexcept {
        return api_ != nullptr && api_->connected != nullptr && api_->connected(api_->context);
    }

private:
    const watchy_network_api_v1_t *api_;
};

class Bluetooth {
public:
    explicit Bluetooth(const watchy_bluetooth_api_v1_t *api = nullptr) noexcept : api_(api) {}

    bool enabled() const noexcept {
        return api_ != nullptr && api_->enabled != nullptr && api_->enabled(api_->context);
    }

private:
    const watchy_bluetooth_api_v1_t *api_;
};

class System {
public:
    explicit System(const watchy_system_api_v1_t *api = nullptr) noexcept : api_(api) {}

    uint32_t millis() const noexcept {
        return (api_ != nullptr && api_->millis != nullptr) ? api_->millis(api_->context) : 0u;
    }
    void sleep_ms(uint32_t duration_ms) const noexcept {
        if (api_ != nullptr && api_->sleep_ms != nullptr) {
            api_->sleep_ms(api_->context, duration_ms);
        }
    }
    void log(const char *message) const noexcept {
        if (api_ != nullptr && api_->log != nullptr) {
            api_->log(api_->context, message);
        }
    }

private:
    const watchy_system_api_v1_t *api_;
};

}  // namespace watchy

#endif
