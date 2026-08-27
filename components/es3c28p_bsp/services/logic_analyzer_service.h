// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGIC_ANALYZER_CHANNEL_COUNT 4
#define LOGIC_ANALYZER_SAMPLE_CAPACITY 1024

typedef enum {
    LOGIC_ANALYZER_TRIGGER_NONE,
    LOGIC_ANALYZER_TRIGGER_RISING,
    LOGIC_ANALYZER_TRIGGER_FALLING,
} logic_analyzer_trigger_edge_t;

typedef enum {
    LOGIC_ANALYZER_STATE_IDLE,
    LOGIC_ANALYZER_STATE_WAITING_TRIGGER,
    LOGIC_ANALYZER_STATE_CAPTURING,
    LOGIC_ANALYZER_STATE_COMPLETE,
    LOGIC_ANALYZER_STATE_ERROR,
} logic_analyzer_state_t;

typedef struct {
    uint32_t sample_rate_hz;
    gpio_num_t trigger_pin;
    logic_analyzer_trigger_edge_t trigger_edge;
    uint32_t trigger_timeout_ms;
} logic_analyzer_config_t;

esp_err_t logic_analyzer_service_init(void);
esp_err_t logic_analyzer_service_start(
    const logic_analyzer_config_t *config);
esp_err_t logic_analyzer_service_cancel(void);
logic_analyzer_state_t logic_analyzer_service_state(void);
const uint8_t *logic_analyzer_service_data(void);
size_t logic_analyzer_service_sample_count(void);
uint32_t logic_analyzer_service_sample_rate_hz(void);
esp_err_t logic_analyzer_service_snapshot(uint8_t *data,
                                          size_t capacity,
                                          size_t *sample_count,
                                          uint32_t *sample_rate_hz);

#ifdef __cplusplus
}
#endif
