#include "services/i2c_service.h"

#include "board/board.h"
#include "esp_log.h"

#define I2C_SERVICE_PORT I2C_NUM_0
#define I2C_SERVICE_FREQ_HZ 400000

static const char *TAG = "i2c_service";
static i2c_master_bus_handle_t bus_handle;

esp_err_t i2c_service_init(void)
{
    const board_pins_t *pins = board_pins();
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_SERVICE_PORT,
        .sda_io_num = pins->touch_sda,
        .scl_io_num = pins->touch_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus_handle));
    ESP_LOGI(TAG, "I2C service initialized on SDA=%d SCL=%d",
             pins->touch_sda,
             pins->touch_scl);
    return ESP_OK;
}

esp_err_t i2c_service_add_device(uint16_t address,
                                 uint32_t scl_speed_hz,
                                 i2c_master_dev_handle_t *device)
{
    if (bus_handle == NULL || device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz == 0 ? I2C_SERVICE_FREQ_HZ : scl_speed_hz,
    };
    return i2c_master_bus_add_device(bus_handle, &config, device);
}

esp_err_t i2c_service_probe(uint16_t address, int timeout_ms)
{
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(bus_handle, address, timeout_ms);
}

i2c_master_bus_handle_t i2c_service_bus_handle(void)
{
    return bus_handle;
}
