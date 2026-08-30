// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOOLBOX_ORIENTATION_0 = 0,
    TOOLBOX_ORIENTATION_90 = 90,
    TOOLBOX_ORIENTATION_180 = 180,
    TOOLBOX_ORIENTATION_270 = 270,
} toolbox_orientation_t;

typedef struct {
    uint32_t schema_version;
    toolbox_orientation_t orientation;
    uint32_t default_uart_baud;
    uint8_t brightness_percent;
    bool color_inversion;
    bool touch_feedback;
    bool restore_last_app;
} toolbox_settings_t;

esp_err_t config_service_init(void);
const toolbox_settings_t *config_service_get(void);
esp_err_t config_service_update(const toolbox_settings_t *settings);
esp_err_t config_service_reset_defaults(void);

#ifdef __cplusplus
}
#endif
