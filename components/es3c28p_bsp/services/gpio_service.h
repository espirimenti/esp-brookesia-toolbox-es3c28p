#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_SERVICE_MODE_INPUT,
    GPIO_SERVICE_MODE_INPUT_PULL_UP,
    GPIO_SERVICE_MODE_INPUT_PULL_DOWN,
    GPIO_SERVICE_MODE_OUTPUT,
} gpio_service_mode_t;

esp_err_t gpio_service_init(void);
esp_err_t gpio_service_configure(gpio_num_t pin, gpio_service_mode_t mode);
esp_err_t gpio_service_get_mode(gpio_num_t pin, gpio_service_mode_t *mode);
esp_err_t gpio_service_read(gpio_num_t pin, bool *high);
esp_err_t gpio_service_write(gpio_num_t pin, bool high);
esp_err_t gpio_service_pwm_start(gpio_num_t pin, uint32_t frequency_hz,
                                 uint8_t duty_percent);
esp_err_t gpio_service_pulse(gpio_num_t pin, uint32_t duration_us);
esp_err_t gpio_service_generator_stop(void);
esp_err_t gpio_service_reset_all(void);

#ifdef __cplusplus
}
#endif
