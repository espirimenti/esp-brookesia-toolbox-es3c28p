// SPDX-License-Identifier: GPL-3.0-only

#include "services/wifi_service.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "wifi_service";
static bool initialized;
static bool started;
static volatile wifi_service_state_t scan_state = WIFI_SERVICE_STATE_IDLE;
static volatile wifi_connection_state_t connection_state =
    WIFI_CONNECTION_DISCONNECTED;
static wifi_ap_record_t scan_results[WIFI_SERVICE_MAX_RESULTS];
static size_t scan_result_count;
static esp_err_t last_error = ESP_OK;
static bool disconnect_requested;
static char connected_ssid[33];
static char ip_address[16];

static void wifi_event_handler(void *argument,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)argument;
    (void)event_base;

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        ip_address[0] = '\0';
        if (disconnect_requested) {
            connection_state = WIFI_CONNECTION_DISCONNECTED;
            last_error = ESP_OK;
        } else {
            connection_state = WIFI_CONNECTION_ERROR;
            last_error = ESP_FAIL;
            if (event != NULL) {
                ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", event->reason);
            }
        }
        disconnect_requested = false;
        return;
    }

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

static void ip_event_handler(void *argument,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    (void)argument;
    (void)event_base;
    if (event_id != IP_EVENT_STA_GOT_IP || event_data == NULL) {
        return;
    }

    const ip_event_got_ip_t *event =
        (const ip_event_got_ip_t *)event_data;
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    connection_state = WIFI_CONNECTION_CONNECTED;
    last_error = ESP_OK;
    ESP_LOGI(TAG, "Connected to %s, IP=%s", connected_ssid, ip_address);
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
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        return result;
    }

    started = true;
    initialized = true;
    ESP_LOGI(TAG, "Wi-Fi service initialized");
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

esp_err_t wifi_service_connect(const char *ssid, const char *password)
{
    if (!initialized || ssid == NULL || password == NULL ||
        ssid[0] == '\0' || strlen(ssid) > 32 || strlen(password) > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    if (connection_state == WIFI_CONNECTION_CONNECTING ||
        connection_state == WIFI_CONNECTION_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result != ESP_OK) {
        last_error = result;
        connection_state = WIFI_CONNECTION_ERROR;
        return result;
    }

    strlcpy(connected_ssid, ssid, sizeof(connected_ssid));
    ip_address[0] = '\0';
    disconnect_requested = false;
    connection_state = WIFI_CONNECTION_CONNECTING;
    result = esp_wifi_connect();
    if (result != ESP_OK) {
        last_error = result;
        connection_state = WIFI_CONNECTION_ERROR;
    }
    return result;
}

esp_err_t wifi_service_disconnect(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (connection_state == WIFI_CONNECTION_DISCONNECTED) {
        return ESP_OK;
    }

    disconnect_requested = true;
    const esp_err_t result = esp_wifi_disconnect();
    if (result == ESP_ERR_WIFI_NOT_CONNECT) {
        disconnect_requested = false;
        connection_state = WIFI_CONNECTION_DISCONNECTED;
        ip_address[0] = '\0';
        return ESP_OK;
    }
    if (result != ESP_OK) {
        disconnect_requested = false;
        last_error = result;
    }
    return result;
}

esp_err_t wifi_service_stop(void)
{
    if (!initialized || !started) {
        scan_state = WIFI_SERVICE_STATE_IDLE;
        connection_state = WIFI_CONNECTION_DISCONNECTED;
        return ESP_OK;
    }
    if (scan_state == WIFI_SERVICE_STATE_SCANNING) {
        esp_wifi_scan_stop();
    }
    const esp_err_t result = esp_wifi_stop();
    if (result == ESP_OK) {
        started = false;
        scan_state = WIFI_SERVICE_STATE_IDLE;
        connection_state = WIFI_CONNECTION_DISCONNECTED;
        ip_address[0] = '\0';
    }
    return result;
}

wifi_service_state_t wifi_service_state(void)
{
    return scan_state;
}

wifi_connection_state_t wifi_service_connection_state(void)
{
    return connection_state;
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

const char *wifi_service_connected_ssid(void)
{
    return connected_ssid;
}

const char *wifi_service_ip_address(void)
{
    return ip_address;
}

esp_err_t wifi_service_connected_ap(wifi_ap_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (connection_state != WIFI_CONNECTION_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_sta_get_ap_info(record);
}