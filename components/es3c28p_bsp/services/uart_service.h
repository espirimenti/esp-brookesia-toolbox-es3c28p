#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uart_service_init(void);
esp_err_t uart_service_set_baud(uint32_t baud_rate);
uint32_t uart_service_get_baud(void);
esp_err_t uart_service_read(uint8_t *data, size_t capacity, size_t *bytes_read);
esp_err_t uart_service_write(const uint8_t *data, size_t length, size_t *bytes_written);
esp_err_t uart_service_clear_rx(void);

#ifdef __cplusplus
}
#endif
