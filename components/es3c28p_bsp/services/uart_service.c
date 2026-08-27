// SPDX-License-Identifier: GPL-3.0-only

#include "services/uart_service.h"

#include "board/board.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>

#define UART_SERVICE_PORT UART_NUM_1
#define UART_SERVICE_DEFAULT_BAUD 115200
#define UART_SERVICE_DRIVER_RX_BUFFER_SIZE 4096
#define UART_SERVICE_DRIVER_TX_BUFFER_SIZE 1024
#define UART_SERVICE_QUEUE_CAPACITY 4096
#define UART_SERVICE_RX_TASK_STACK 3072
#define UART_SERVICE_MIN_BAUD 300
#define UART_SERVICE_MAX_BAUD 5000000

static const char *TAG = "uart_service";
static bool initialized;
static uint32_t current_baud = UART_SERVICE_DEFAULT_BAUD;
static SemaphoreHandle_t queue_mutex;
static uint8_t rx_queue[UART_SERVICE_QUEUE_CAPACITY];
static size_t rx_queue_start;
static size_t rx_queue_count;

static void append_received(const uint8_t *data, size_t length)
{
    if (xSemaphoreTake(queue_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < length; ++i) {
        if (rx_queue_count == UART_SERVICE_QUEUE_CAPACITY) {
            rx_queue_start = (rx_queue_start + 1) % UART_SERVICE_QUEUE_CAPACITY;
            --rx_queue_count;
        }
        const size_t write_index =
            (rx_queue_start + rx_queue_count) % UART_SERVICE_QUEUE_CAPACITY;
        rx_queue[write_index] = data[i];
        ++rx_queue_count;
    }
    xSemaphoreGive(queue_mutex);
}

static void uart_rx_task(void *argument)
{
    (void)argument;
    uint8_t data[256];
    while (true) {
        const int received = uart_read_bytes(
            UART_SERVICE_PORT, data, sizeof(data), pdMS_TO_TICKS(20));
        if (received > 0) {
            append_received(data, (size_t)received);
        }
    }
}

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
                                           UART_SERVICE_DRIVER_RX_BUFFER_SIZE,
                                           UART_SERVICE_DRIVER_TX_BUFFER_SIZE,
                                           0, NULL, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_param_config(UART_SERVICE_PORT, &config);
    if (result == ESP_OK) {
        result = uart_set_pin(UART_SERVICE_PORT, pins->uart_tx, pins->uart_rx,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (result == ESP_OK) {
        result = gpio_set_pull_mode(pins->uart_rx, GPIO_PULLUP_ONLY);
    }
    if (result != ESP_OK) {
        uart_driver_delete(UART_SERVICE_PORT);
        return result;
    }

    queue_mutex = xSemaphoreCreateMutex();
    if (queue_mutex == NULL) {
        uart_driver_delete(UART_SERVICE_PORT);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(uart_rx_task, "toolbox_uart_rx", UART_SERVICE_RX_TASK_STACK,
                    NULL, 8, NULL) != pdPASS) {
        vSemaphoreDelete(queue_mutex);
        queue_mutex = NULL;
        uart_driver_delete(UART_SERVICE_PORT);
        return ESP_ERR_NO_MEM;
    }

    initialized = true;
    ESP_LOGI(TAG, "UART1 ready: TX=%d RX=%d baud=%lu, background RX enabled",
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
    if (xSemaphoreTake(queue_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    const size_t read_size = rx_queue_count > capacity ? capacity : rx_queue_count;
    for (size_t i = 0; i < read_size; ++i) {
        data[i] = rx_queue[(rx_queue_start + i) % UART_SERVICE_QUEUE_CAPACITY];
    }
    rx_queue_start =
        (rx_queue_start + read_size) % UART_SERVICE_QUEUE_CAPACITY;
    rx_queue_count -= read_size;
    *bytes_read = read_size;
    xSemaphoreGive(queue_mutex);
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
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = uart_flush_input(UART_SERVICE_PORT);
    if (result != ESP_OK) {
        return result;
    }
    if (xSemaphoreTake(queue_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    rx_queue_start = 0;
    rx_queue_count = 0;
    xSemaphoreGive(queue_mutex);
    return ESP_OK;
}
