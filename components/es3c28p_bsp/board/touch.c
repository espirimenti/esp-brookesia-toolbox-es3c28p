#include "board/board.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/i2c_service.h"

#include <string.h>

#define FT6336_ADDRESS 0x38
#define FT6336_I2C_FREQ_HZ 400000
#define FT6336_TIMEOUT_MS 20

#define FT6336_REG_TOUCH_STATUS 0x02
#define FT6336_REG_CHIP_ID 0xA3
#define FT6336_REG_FIRMWARE_ID 0xA6
#define FT6336_REG_VENDOR_ID 0xA8

static const char *TAG = "touch";
static i2c_master_dev_handle_t touch_device;

static esp_err_t touch_read_registers(uint8_t register_address, uint8_t *data, size_t length)
{
    if (touch_device == NULL || data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(
        touch_device, &register_address, 1, data, length, FT6336_TIMEOUT_MS);
}

esp_err_t board_touch_init(void)
{
    const board_pins_t *pins = board_pins();

    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << pins->touch_reset,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&reset_config));

    const gpio_config_t interrupt_config = {
        .pin_bit_mask = 1ULL << pins->touch_interrupt,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&interrupt_config));

    ESP_ERROR_CHECK(gpio_set_level(pins->touch_reset, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(pins->touch_reset, 1));
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_RETURN_ON_ERROR(i2c_service_probe(FT6336_ADDRESS, 100), TAG,
                        "FT6336G not found at 0x%02X", FT6336_ADDRESS);
    ESP_ERROR_CHECK(
        i2c_service_add_device(FT6336_ADDRESS, FT6336_I2C_FREQ_HZ, &touch_device));

    uint8_t chip_id = 0;
    uint8_t firmware_id = 0;
    uint8_t vendor_id = 0;
    ESP_ERROR_CHECK(touch_read_registers(FT6336_REG_CHIP_ID, &chip_id, 1));
    ESP_ERROR_CHECK(touch_read_registers(FT6336_REG_FIRMWARE_ID, &firmware_id, 1));
    ESP_ERROR_CHECK(touch_read_registers(FT6336_REG_VENDOR_ID, &vendor_id, 1));

    ESP_LOGI(TAG,
             "FT6336G ready at 0x%02X: chip=0x%02X firmware=0x%02X vendor=0x%02X",
             FT6336_ADDRESS,
             chip_id,
             firmware_id,
             vendor_id);
    return ESP_OK;
}

esp_err_t board_touch_read(board_touch_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[5] = {0};
    ESP_RETURN_ON_ERROR(
        touch_read_registers(FT6336_REG_TOUCH_STATUS, data, sizeof(data)), TAG, "Touch read");

    memset(state, 0, sizeof(*state));
    state->touch_count = data[0] & 0x0F;
    if (state->touch_count == 0 || state->touch_count > 2) {
        return ESP_OK;
    }

    state->pressed = true;
    state->x = (uint16_t)(((data[1] & 0x0F) << 8) | data[2]);
    state->y = (uint16_t)(((data[3] & 0x0F) << 8) | data[4]);
    return ESP_OK;
}
