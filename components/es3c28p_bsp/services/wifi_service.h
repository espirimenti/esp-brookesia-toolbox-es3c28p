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

typedef enum {
    WIFI_CONNECTION_DISCONNECTED,
    WIFI_CONNECTION_CONNECTING,
    WIFI_CONNECTION_CONNECTED,
    WIFI_CONNECTION_ERROR,
} wifi_connection_state_t;

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_start_scan(void);
esp_err_t wifi_service_connect(const char *ssid, const char *password);
esp_err_t wifi_service_disconnect(void);
esp_err_t wifi_service_stop(void);
wifi_service_state_t wifi_service_state(void);
wifi_connection_state_t wifi_service_connection_state(void);
const wifi_ap_record_t *wifi_service_results(void);
size_t wifi_service_result_count(void);
esp_err_t wifi_service_last_error(void);
const char *wifi_service_connected_ssid(void);
const char *wifi_service_ip_address(void);
esp_err_t wifi_service_connected_ap(wifi_ap_record_t *record);

#ifdef __cplusplus
}
#endif