// SPDX-License-Identifier: GPL-3.0-only

#include "services/wifi_service.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <stdbool.h>

static const char *TAG = "wifi_service";
static bool initialized;
static bool started;
static volatile wifi_service_state_t scan_state = WIFI_SERVICE_STATE_IDLE;
static wifi_ap_record_t scan_results[WIFI_SERVICE_MAX_RESULTS];
static size_t scan_result_count;
static esp_err_t last_error = ESP_OK;

static void wifi_event_handler(void *argument,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)argument;
    (void)event_base;
    if (event_id != WIFI_EVENT_SCAN_DONE) {
        return;
    }

    const wifi_event_sta_scan_done_t *event =
        (const wifi_event_sta_scan_done_t *)event_data;
    if (event == NULL || event->status != 0) {
        scan_result_count = 0;
        last_error = ESP_FAIL;
        scan_state = WIFI_SERVICE_STATE_ERROR;
        return;
    }

    uint16_t count = WIFI_SERVICE_MAX_RESULTS;
    const esp_err_t result =
        esp_wifi_scan_get_ap_records(&count, scan_results);
    if (result != ESP_OK) {
        scan_result_count = 0;
        last_error = result;
        scan_state = WIFI_SERVICE_STATE_ERROR;
        return;
    }

    scan_result_count = count;
    last_error = ESP_OK;
    scan_state = WIFI_SERVICE_STATE_COMPLETE;
    ESP_LOGI(TAG, "Scan complete: %u network(s)", (unsigned)count);
}

esp_err_t wifi_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(result));
        return result;
    }

    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event_handler, NULL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        return result;
    }

    started = true;
    initialized = true;
    ESP_LOGI(TAG, "Wi-Fi scanner initialized");
    return ESP_OK;
}

esp_err_t wifi_service_start_scan(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (scan_state == WIFI_SERVICE_STATE_SCANNING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!started) {
        const esp_err_t start_result = esp_wifi_start();
        if (start_result != ESP_OK) {
            return start_result;
        }
        started = true;
    }

    wifi_scan_config_t config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 40,
                .max = 100,
            },
        },
        .home_chan_dwell_time = 0,
        .channel_bitmap = {0},
    };

    scan_result_count = 0;
    last_error = ESP_OK;
    scan_state = WIFI_SERVICE_STATE_SCANNING;
    const esp_err_t result = esp_wifi_scan_start(&config, false);
    if (result != ESP_OK) {
        last_error = result;
        scan_state = WIFI_SERVICE_STATE_ERROR;
    }
    return result;
}

esp_err_t wifi_service_stop(void)
{
    if (!initialized || !started) {
        scan_state = WIFI_SERVICE_STATE_IDLE;
        return ESP_OK;
    }
    if (scan_state == WIFI_SERVICE_STATE_SCANNING) {
        esp_wifi_scan_stop();
    }
    const esp_err_t result = esp_wifi_stop();
    if (result == ESP_OK) {
        started = false;
        scan_state = WIFI_SERVICE_STATE_IDLE;
    }
    return result;
}

wifi_service_state_t wifi_service_state(void)
{
    return scan_state;
}

const wifi_ap_record_t *wifi_service_results(void)
{
    return scan_results;
}

size_t wifi_service_result_count(void)
{
    return scan_result_count;
}

esp_err_t wifi_service_last_error(void)
{
    return last_error;
}
