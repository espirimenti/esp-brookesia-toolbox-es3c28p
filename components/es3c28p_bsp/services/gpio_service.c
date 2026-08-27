// SPDX-License-Identifier: GPL-3.0-only

#include "services/gpio_service.h"

#include "board/board.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdbool.h>

#define GPIO_PWM_MODE LEDC_LOW_SPEED_MODE
#define GPIO_PWM_TIMER LEDC_TIMER_1
#define GPIO_PWM_CHANNEL LEDC_CHANNEL_1
#define GPIO_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define GPIO_PWM_MAX_DUTY 1023
#define GPIO_PWM_MAX_FREQUENCY_HZ 50000

static const char *TAG = "gpio_service";
static const gpio_num_t EXPANSION_PINS[] = {
    BOARD_EXPAND_GPIO_2,
    BOARD_EXPAND_GPIO_3,
    BOARD_EXPAND_GPIO_14,
    BOARD_EXPAND_GPIO_21,
};
static gpio_service_mode_t pin_modes[
    sizeof(EXPANSION_PINS) / sizeof(EXPANSION_PINS[0])];
static gpio_num_t active_pwm_pin = GPIO_NUM_NC;
static gpio_num_t active_pulse_pin = GPIO_NUM_NC;
static esp_timer_handle_t pulse_timer;
static bool initialized;

static int pin_index(gpio_num_t pin)
{
    for (size_t i = 0; i < sizeof(EXPANSION_PINS) / sizeof(EXPANSION_PINS[0]); ++i) {
        if (EXPANSION_PINS[i] == pin) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t configure_pin(gpio_num_t pin, gpio_service_mode_t mode)
{
    const int index = pin_index(pin);
    if (index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    switch (mode) {
    case GPIO_SERVICE_MODE_INPUT:
        break;
    case GPIO_SERVICE_MODE_INPUT_PULL_UP:
        config.pull_up_en = GPIO_PULLUP_ENABLE;
        break;
    case GPIO_SERVICE_MODE_INPUT_PULL_DOWN:
        config.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    case GPIO_SERVICE_MODE_OUTPUT:
        config.mode = GPIO_MODE_INPUT_OUTPUT;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }
    if (mode == GPIO_SERVICE_MODE_OUTPUT) {
        result = gpio_set_level(pin, 0);
        if (result != ESP_OK) {
            return result;
        }
    }
    pin_modes[index] = mode;
    return ESP_OK;
}

static void pulse_timer_callback(void *argument)
{
    (void)argument;
    const gpio_num_t pin = active_pulse_pin;
    if (pin == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(pin, 0);
    configure_pin(pin, GPIO_SERVICE_MODE_INPUT);
    active_pulse_pin = GPIO_NUM_NC;
}

esp_err_t gpio_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    for (size_t i = 0; i < sizeof(EXPANSION_PINS) / sizeof(EXPANSION_PINS[0]); ++i) {
        ESP_RETURN_ON_ERROR(configure_pin(EXPANSION_PINS[i],
                                          GPIO_SERVICE_MODE_INPUT),
                            TAG, "GPIO input init failed");
    }

    const esp_timer_create_args_t timer_config = {
        .callback = pulse_timer_callback,
        .name = "gpio_pulse",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_config, &pulse_timer),
                        TAG, "Pulse timer init failed");
    initialized = true;
    ESP_LOGI(TAG, "Expansion GPIO ready: 2, 3, 14, 21");
    return ESP_OK;
}

esp_err_t gpio_service_configure(gpio_num_t pin, gpio_service_mode_t mode)
{
    if (!initialized || pin_index(pin) < 0) {
        return initialized ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    if (pin == active_pwm_pin || pin == active_pulse_pin) {
        ESP_RETURN_ON_ERROR(gpio_service_generator_stop(), TAG,
                            "Generator stop failed");
    }
    return configure_pin(pin, mode);
}

esp_err_t gpio_service_get_mode(gpio_num_t pin, gpio_service_mode_t *mode)
{
    const int index = pin_index(pin);
    if (!initialized || index < 0 || mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *mode = pin_modes[index];
    return ESP_OK;
}

esp_err_t gpio_service_read(gpio_num_t pin, bool *high)
{
    if (!initialized || pin_index(pin) < 0 || high == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *high = gpio_get_level(pin) != 0;
    return ESP_OK;
}

esp_err_t gpio_service_write(gpio_num_t pin, bool high)
{
    const int index = pin_index(pin);
    if (!initialized || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pin_modes[index] != GPIO_SERVICE_MODE_OUTPUT ||
        pin == active_pwm_pin || pin == active_pulse_pin) {
        return ESP_ERR_INVALID_STATE;
    }
    return gpio_set_level(pin, high ? 1 : 0);
}

esp_err_t gpio_service_generator_stop(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (active_pwm_pin != GPIO_NUM_NC) {
        const gpio_num_t pin = active_pwm_pin;
        active_pwm_pin = GPIO_NUM_NC;
        ESP_RETURN_ON_ERROR(ledc_stop(GPIO_PWM_MODE, GPIO_PWM_CHANNEL, 0),
                            TAG, "PWM stop failed");
        ESP_RETURN_ON_ERROR(configure_pin(pin, GPIO_SERVICE_MODE_INPUT),
                            TAG, "PWM pin reset failed");
    }
    if (active_pulse_pin != GPIO_NUM_NC) {
        const gpio_num_t pin = active_pulse_pin;
        active_pulse_pin = GPIO_NUM_NC;
        const esp_err_t stop_result = esp_timer_stop(pulse_timer);
        if (stop_result != ESP_OK && stop_result != ESP_ERR_INVALID_STATE) {
            return stop_result;
        }
        gpio_set_level(pin, 0);
        ESP_RETURN_ON_ERROR(configure_pin(pin, GPIO_SERVICE_MODE_INPUT),
                            TAG, "Pulse pin reset failed");
    }
    return ESP_OK;
}

esp_err_t gpio_service_pwm_start(gpio_num_t pin, uint32_t frequency_hz,
                                 uint8_t duty_percent)
{
    if (!initialized || pin_index(pin) < 0 || frequency_hz == 0 ||
        frequency_hz > GPIO_PWM_MAX_FREQUENCY_HZ || duty_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(gpio_service_generator_stop(), TAG,
                        "Previous generator stop failed");

    const ledc_timer_config_t timer_config = {
        .speed_mode = GPIO_PWM_MODE,
        .duty_resolution = GPIO_PWM_RESOLUTION,
        .timer_num = GPIO_PWM_TIMER,
        .freq_hz = frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "PWM timer config failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = pin,
        .speed_mode = GPIO_PWM_MODE,
        .channel = GPIO_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = GPIO_PWM_TIMER,
        .duty = ((uint32_t)GPIO_PWM_MAX_DUTY * duty_percent) / 100,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "PWM channel config failed");

    pin_modes[pin_index(pin)] = GPIO_SERVICE_MODE_OUTPUT;
    active_pwm_pin = pin;
    return ESP_OK;
}

esp_err_t gpio_service_pulse(gpio_num_t pin, uint32_t duration_us)
{
    if (!initialized || pin_index(pin) < 0 || duration_us == 0 ||
        duration_us > 1000000) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(gpio_service_generator_stop(), TAG,
                        "Previous generator stop failed");
    ESP_RETURN_ON_ERROR(configure_pin(pin, GPIO_SERVICE_MODE_OUTPUT), TAG,
                        "Pulse output config failed");
    active_pulse_pin = pin;
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1), TAG, "Pulse high failed");
    const esp_err_t result = esp_timer_start_once(pulse_timer, duration_us);
    if (result != ESP_OK) {
        active_pulse_pin = GPIO_NUM_NC;
        gpio_set_level(pin, 0);
        configure_pin(pin, GPIO_SERVICE_MODE_INPUT);
    }
    return result;
}

esp_err_t gpio_service_reset_all(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(gpio_service_generator_stop(), TAG,
                        "Generator stop failed");
    for (size_t i = 0; i < sizeof(EXPANSION_PINS) / sizeof(EXPANSION_PINS[0]); ++i) {
        ESP_RETURN_ON_ERROR(configure_pin(EXPANSION_PINS[i],
                                          GPIO_SERVICE_MODE_INPUT),
                            TAG, "GPIO reset failed");
    }
    return ESP_OK;
}
