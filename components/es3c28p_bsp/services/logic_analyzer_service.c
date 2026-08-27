// SPDX-License-Identifier: GPL-3.0-only

#include "services/logic_analyzer_service.h"

#include "board/board.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "services/gpio_service.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include <stdbool.h>
#include <string.h>

#define CAPTURE_TASK_STACK_SIZE 3072
#define CAPTURE_TASK_PRIORITY (configMAX_PRIORITIES - 2)
#define TRIGGER_POLL_MS 10
#define MIN_SAMPLE_RATE_HZ 1000
#define MAX_SAMPLE_RATE_HZ 1000000

static const char *TAG = "logic_analyzer";
static const gpio_num_t CAPTURE_PINS[] = {
    BOARD_EXPAND_GPIO_2,
    BOARD_EXPAND_GPIO_3,
    BOARD_EXPAND_GPIO_14,
    BOARD_EXPAND_GPIO_21,
};

static uint8_t capture_data[LOGIC_ANALYZER_SAMPLE_CAPACITY];
static volatile logic_analyzer_state_t capture_state =
    LOGIC_ANALYZER_STATE_IDLE;
static volatile bool cancel_requested;
static size_t captured_sample_count;
static uint32_t captured_sample_rate_hz;
static logic_analyzer_config_t pending_config;
static SemaphoreHandle_t trigger_semaphore;
static portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized;

static bool is_capture_pin(gpio_num_t pin)
{
    for (size_t i = 0; i < LOGIC_ANALYZER_CHANNEL_COUNT; i++) {
        if (CAPTURE_PINS[i] == pin) {
            return true;
        }
    }
    return false;
}

static inline uint8_t pack_gpio_levels(uint32_t levels)
{
    return (uint8_t)(
        (((levels >> BOARD_EXPAND_GPIO_2) & 1U) << 0) |
        (((levels >> BOARD_EXPAND_GPIO_3) & 1U) << 1) |
        (((levels >> BOARD_EXPAND_GPIO_14) & 1U) << 2) |
        (((levels >> BOARD_EXPAND_GPIO_21) & 1U) << 3));
}

static void IRAM_ATTR trigger_isr(void *argument)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(trigger_semaphore, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t wait_for_trigger(const logic_analyzer_config_t *config)
{
    while (xSemaphoreTake(trigger_semaphore, 0) == pdTRUE) {
    }

    const gpio_int_type_t interrupt_type =
        config->trigger_edge == LOGIC_ANALYZER_TRIGGER_RISING
            ? GPIO_INTR_POSEDGE
            : GPIO_INTR_NEGEDGE;
    ESP_RETURN_ON_ERROR(
        gpio_set_intr_type(config->trigger_pin, interrupt_type), TAG,
        "Failed to configure trigger edge");
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(config->trigger_pin, trigger_isr, NULL), TAG,
        "Failed to add trigger handler");
    ESP_RETURN_ON_ERROR(
        gpio_intr_enable(config->trigger_pin), TAG,
        "Failed to enable trigger");

    const int64_t deadline_us =
        esp_timer_get_time() + ((int64_t)config->trigger_timeout_ms * 1000);
    bool triggered = false;
    while (!cancel_requested && esp_timer_get_time() < deadline_us) {
        if (xSemaphoreTake(
                trigger_semaphore, pdMS_TO_TICKS(TRIGGER_POLL_MS)) == pdTRUE) {
            triggered = true;
            break;
        }
    }

    gpio_intr_disable(config->trigger_pin);
    gpio_isr_handler_remove(config->trigger_pin);
    if (!triggered && !cancel_requested) {
        ESP_LOGI(TAG, "Trigger timeout; starting automatic capture");
    }
    return ESP_OK;
}

static void capture_task(void *argument)
{
    const logic_analyzer_config_t config = pending_config;

    if (config.trigger_edge != LOGIC_ANALYZER_TRIGGER_NONE) {
        const esp_err_t result = wait_for_trigger(&config);
        if (result != ESP_OK) {
            capture_state = LOGIC_ANALYZER_STATE_ERROR;
            vTaskDelete(NULL);
            return;
        }
    }

    if (cancel_requested) {
        capture_state = LOGIC_ANALYZER_STATE_IDLE;
        vTaskDelete(NULL);
        return;
    }

    capture_state = LOGIC_ANALYZER_STATE_CAPTURING;
    captured_sample_count = 0;

    const uint32_t cpu_frequency_hz =
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U;
    const uint32_t cycles_per_sample =
        cpu_frequency_hz / config.sample_rate_hz;
    uint32_t next_sample_cycle = esp_cpu_get_cycle_count();

    for (size_t i = 0; i < LOGIC_ANALYZER_SAMPLE_CAPACITY; i++) {
        while ((int32_t)(esp_cpu_get_cycle_count() - next_sample_cycle) < 0) {
        }
        if (cancel_requested) {
            capture_state = LOGIC_ANALYZER_STATE_IDLE;
            vTaskDelete(NULL);
            return;
        }

        capture_data[i] = pack_gpio_levels(REG_READ(GPIO_IN_REG));
        captured_sample_count = i + 1;
        next_sample_cycle += cycles_per_sample;
    }

    portENTER_CRITICAL(&capture_lock);
    captured_sample_rate_hz = config.sample_rate_hz;
    capture_state = LOGIC_ANALYZER_STATE_COMPLETE;
    portEXIT_CRITICAL(&capture_lock);
    ESP_LOGI(TAG, "Captured %u samples at %lu Hz",
             (unsigned)captured_sample_count,
             (unsigned long)captured_sample_rate_hz);
    vTaskDelete(NULL);
}

esp_err_t logic_analyzer_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_service_init(), TAG,
                        "GPIO service init failed");

    trigger_semaphore = xSemaphoreCreateBinary();
    if (trigger_semaphore == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t isr_result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_result != ESP_OK && isr_result != ESP_ERR_INVALID_STATE) {
        return isr_result;
    }

    initialized = true;
    ESP_LOGI(TAG, "Logic analyzer service initialized");
    return ESP_OK;
}

esp_err_t logic_analyzer_service_start(
    const logic_analyzer_config_t *config)
{
    if (config == NULL ||
        config->sample_rate_hz < MIN_SAMPLE_RATE_HZ ||
        config->sample_rate_hz > MAX_SAMPLE_RATE_HZ ||
        (config->trigger_edge != LOGIC_ANALYZER_TRIGGER_NONE &&
         !is_capture_pin(config->trigger_pin))) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&capture_lock);
    const bool busy =
        capture_state == LOGIC_ANALYZER_STATE_WAITING_TRIGGER ||
        capture_state == LOGIC_ANALYZER_STATE_CAPTURING;
    if (!busy) {
        capture_state = LOGIC_ANALYZER_STATE_CAPTURING;
    }
    portEXIT_CRITICAL(&capture_lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = gpio_service_generator_stop();
    if (result != ESP_OK) {
        capture_state = LOGIC_ANALYZER_STATE_ERROR;
        return result;
    }
    for (size_t i = 0; i < LOGIC_ANALYZER_CHANNEL_COUNT; i++) {
        result = gpio_service_configure(
            CAPTURE_PINS[i], GPIO_SERVICE_MODE_INPUT);
        if (result != ESP_OK) {
            capture_state = LOGIC_ANALYZER_STATE_ERROR;
            return result;
        }
    }

    pending_config = *config;
    cancel_requested = false;
    captured_sample_count = 0;
    capture_state =
        config->trigger_edge == LOGIC_ANALYZER_TRIGGER_NONE
            ? LOGIC_ANALYZER_STATE_CAPTURING
            : LOGIC_ANALYZER_STATE_WAITING_TRIGGER;

    const BaseType_t task_result = xTaskCreatePinnedToCore(
        capture_task,
        "logic_capture",
        CAPTURE_TASK_STACK_SIZE,
        NULL,
        CAPTURE_TASK_PRIORITY,
        NULL,
        1);
    if (task_result != pdPASS) {
        capture_state = LOGIC_ANALYZER_STATE_ERROR;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t logic_analyzer_service_cancel(void)
{
    cancel_requested = true;
    if (trigger_semaphore != NULL) {
        xSemaphoreGive(trigger_semaphore);
    }
    if (capture_state != LOGIC_ANALYZER_STATE_WAITING_TRIGGER &&
        capture_state != LOGIC_ANALYZER_STATE_CAPTURING) {
        capture_state = LOGIC_ANALYZER_STATE_IDLE;
    }
    return ESP_OK;
}

logic_analyzer_state_t logic_analyzer_service_state(void)
{
    return capture_state;
}

const uint8_t *logic_analyzer_service_data(void)
{
    return capture_data;
}

size_t logic_analyzer_service_sample_count(void)
{
    return captured_sample_count;
}

uint32_t logic_analyzer_service_sample_rate_hz(void)
{
    return captured_sample_rate_hz;
}

esp_err_t logic_analyzer_service_snapshot(uint8_t *data,
                                          size_t capacity,
                                          size_t *sample_count,
                                          uint32_t *sample_rate_hz)
{
    if (data == NULL || capacity == 0 || sample_count == NULL ||
        sample_rate_hz == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&capture_lock);
    if (capture_state != LOGIC_ANALYZER_STATE_COMPLETE) {
        portEXIT_CRITICAL(&capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const size_t count =
        captured_sample_count < capacity
            ? captured_sample_count
            : capacity;
    memcpy(data, capture_data, count);
    *sample_count = count;
    *sample_rate_hz = captured_sample_rate_hz;
    portEXIT_CRITICAL(&capture_lock);
    return ESP_OK;
}
