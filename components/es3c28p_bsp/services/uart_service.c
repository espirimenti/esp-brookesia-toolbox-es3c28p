// SPDX-License-Identifier: GPL-3.0-only

#include "services/uart_service.h"

#include "board/board.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <stdbool.h>

#define UART_SERVICE_PORT UART_NUM_1
#define UART_SERVICE_DEFAULT_BAUD 115200
#define UART_SERVICE_RX_BUFFER_SIZE 4096
#define UART_SERVICE_TX_BUFFER_SIZE 1024
#define UART_SERVICE_MIN_BAUD 300
#define UART_SERVICE_MAX_BAUD 5000000

static const char *TAG = "uart_service";
static bool initialized;
static uint32_t current_baud = UART_SERVICE_DEFAULT_BAUD;

esp_err_t uart_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    const board_pins_t *pins = board_pins();
    const uart_config_t config = {
        .baud_rate = (int)current_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(UART_SERVICE_PORT,
                                           UART_SERVICE_RX_BUFFER_SIZE,
                                           UART_SERVICE_TX_BUFFER_SIZE,
                                           0, NULL, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_param_config(UART_SERVICE_PORT, &config);
    if (result == ESP_OK) {
        result = uart_set_pin(UART_SERVICE_PORT, pins->uart_tx, pins->uart_rx,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (result != ESP_OK) {
        uart_driver_delete(UART_SERVICE_PORT);
        return result;
    }

    initialized = true;
    ESP_LOGI(TAG, "UART1 ready: TX=%d RX=%d baud=%lu",
             pins->uart_tx, pins->uart_rx, (unsigned long)current_baud);
    return ESP_OK;
}

esp_err_t uart_service_set_baud(uint32_t baud_rate)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (baud_rate < UART_SERVICE_MIN_BAUD || baud_rate > UART_SERVICE_MAX_BAUD) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = uart_set_baudrate(UART_SERVICE_PORT, baud_rate);
    if (result == ESP_OK) {
        current_baud = baud_rate;
    }
    return result;
}

uint32_t uart_service_get_baud(void)
{
    return current_baud;
}

esp_err_t uart_service_read(uint8_t *data, size_t capacity, size_t *bytes_read)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || capacity == 0 || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int received = uart_read_bytes(UART_SERVICE_PORT, data, capacity, 0);
    if (received < 0) {
        *bytes_read = 0;
        return ESP_FAIL;
    }
    *bytes_read = (size_t)received;
    return ESP_OK;
}

esp_err_t uart_service_write(const uint8_t *data, size_t length, size_t *bytes_written)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || length == 0 || bytes_written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = uart_write_bytes(UART_SERVICE_PORT, data, length);
    if (written < 0) {
        *bytes_written = 0;
        return ESP_FAIL;
    }
    *bytes_written = (size_t)written;
    return ESP_OK;
}

esp_err_t uart_service_clear_rx(void)
{
    return initialized ? uart_flush_input(UART_SERVICE_PORT) : ESP_ERR_INVALID_STATE;
}
