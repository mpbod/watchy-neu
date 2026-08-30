#ifndef WATCHY_BUS_INTERNAL_H
#define WATCHY_BUS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

i2c_master_bus_handle_t watchy_bus_i2c_handle(void);
esp_err_t watchy_bus_rtc_read(uint8_t reg, uint8_t *data, size_t length);
esp_err_t watchy_bus_rtc_write(uint8_t reg, const uint8_t *data, size_t length);
esp_err_t watchy_bus_display_command(uint8_t command);
esp_err_t watchy_bus_display_data(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
