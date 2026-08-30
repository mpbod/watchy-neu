#include "watchy/buses.h"

#include "bus_internal.h"
#include "watchy/board.h"
#include "watchy/display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define WATCHY_I2C_CLOCK_HZ 400000u
#define WATCHY_SPI_CLOCK_HZ 20000000
#define WATCHY_PCF8563_ADDRESS 0x51u

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_rtc_device;
static spi_device_handle_t s_display_device;
static bool s_ready;

static void deinit_partial(void) {
    if (s_display_device != NULL) {
        spi_bus_remove_device(s_display_device);
        s_display_device = NULL;
    }
    spi_bus_free(SPI2_HOST);
    if (s_rtc_device != NULL) {
        i2c_master_bus_rm_device(s_rtc_device);
        s_rtc_device = NULL;
    }
    if (s_i2c_bus != NULL) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

watchy_status_t watchy_buses_init(void) {
    esp_err_t error;
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = WATCHY_PIN_I2C_SDA,
        .scl_io_num = WATCHY_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
        .flags.allow_pd = false,
    };
    i2c_device_config_t rtc_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = WATCHY_PCF8563_ADDRESS,
        .scl_speed_hz = WATCHY_I2C_CLOCK_HZ,
        .scl_wait_us = 0,
        .flags.disable_ack_check = false,
    };
    spi_bus_config_t spi_config = {
        .mosi_io_num = WATCHY_PIN_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = WATCHY_PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = WATCHY_DISPLAY_FRAMEBUFFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = 0,
    };
    spi_device_interface_config_t display_config = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = WATCHY_SPI_CLOCK_HZ,
        .input_delay_ns = 0,
        .spics_io_num = WATCHY_PIN_DISPLAY_CS,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    if (s_ready) {
        return WATCHY_STATUS_OK;
    }
    error = i2c_new_master_bus(&i2c_config, &s_i2c_bus);
    if (error != ESP_OK) {
        deinit_partial();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = i2c_master_bus_add_device(s_i2c_bus, &rtc_config, &s_rtc_device);
    if (error != ESP_OK) {
        deinit_partial();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = spi_bus_initialize(SPI2_HOST, &spi_config, SPI_DMA_CH_AUTO);
    if (error != ESP_OK) {
        deinit_partial();
        return WATCHY_STATUS_INVALID_STATE;
    }
    error = spi_bus_add_device(SPI2_HOST, &display_config, &s_display_device);
    if (error != ESP_OK) {
        deinit_partial();
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_buses_ready(void) {
    return s_ready;
}

i2c_master_bus_handle_t watchy_bus_i2c_handle(void) {
    return s_i2c_bus;
}

esp_err_t watchy_bus_rtc_read(uint8_t reg, uint8_t *data, size_t length) {
    if (!s_ready || data == NULL || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_rtc_device, &reg, 1, data, length, 1000);
}

esp_err_t watchy_bus_rtc_write(uint8_t reg, const uint8_t *data, size_t length) {
    uint8_t buffer[8];

    if (!s_ready || data == NULL || length == 0 || length > sizeof(buffer) - 1u) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = reg;
    memcpy(&buffer[1], data, length);
    return i2c_master_transmit(s_rtc_device, buffer, length + 1u, 1000);
}

static esp_err_t display_transmit(const uint8_t *data, size_t length) {
    spi_transaction_t transaction = {
        .flags = 0,
        .cmd = 0,
        .addr = 0,
        .length = length * 8u,
        .rxlength = 0,
        .user = NULL,
        .tx_buffer = data,
        .rx_buffer = NULL,
    };
    if (!s_ready || data == NULL || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return spi_device_polling_transmit(s_display_device, &transaction);
}

esp_err_t watchy_bus_display_command(uint8_t command) {
    gpio_set_level(WATCHY_PIN_DISPLAY_DC, 0);
    return display_transmit(&command, 1);
}

esp_err_t watchy_bus_display_data(const uint8_t *data, size_t length) {
    gpio_set_level(WATCHY_PIN_DISPLAY_DC, 1);
    return display_transmit(data, length);
}

void watchy_buses_deinit(void) {
    s_ready = false;
    deinit_partial();
}
