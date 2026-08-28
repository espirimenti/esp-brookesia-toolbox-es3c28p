// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SERVICE_MAX_RESULTS 48

typedef enum {
    WIFI_SERVICE_STATE_IDLE,
    WIFI_SERVICE_STATE_SCANNING,
    WIFI_SERVICE_STATE_COMPLETE,
    WIFI_SERVICE_STATE_ERROR,
} wifi_service_state_t;

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_start_scan(void);
esp_err_t wifi_service_stop(void);
wifi_service_state_t wifi_service_state(void);
const wifi_ap_record_t *wifi_service_results(void);
size_t wifi_service_result_count(void);
esp_err_t wifi_service_last_error(void);

#ifdef __cplusplus
}
#endif
