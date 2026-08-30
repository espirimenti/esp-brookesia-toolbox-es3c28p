// SPDX-License-Identifier: GPL-3.0-only
#include "services/config_service.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#define CONFIG_SCHEMA_VERSION 1
#define CONFIG_NVS_NAMESPACE "toolbox"
#define CONFIG_NVS_KEY "settings"

static const char *TAG = "config";
static nvs_handle_t settings_nvs_handle;
static bool initialized;

static const toolbox_settings_t DEFAULT_SETTINGS = {
    .schema_version = CONFIG_SCHEMA_VERSION,
    .orientation = TOOLBOX_ORIENTATION_0,
    .default_uart_baud = 115200,
    .brightness_percent = 100,
    .color_inversion = true,
    .touch_feedback = false,
    .restore_last_app = true,
};

static toolbox_settings_t current_settings;

static bool orientation_is_valid(toolbox_orientation_t orientation)
{
    return orientation == TOOLBOX_ORIENTATION_0 ||
           orientation == TOOLBOX_ORIENTATION_90 ||
           orientation == TOOLBOX_ORIENTATION_180 ||
           orientation == TOOLBOX_ORIENTATION_270;
}

static bool settings_are_valid(const toolbox_settings_t *settings)
{
    return settings != NULL &&
           settings->schema_version == CONFIG_SCHEMA_VERSION &&
           orientation_is_valid(settings->orientation) &&
           settings->brightness_percent <= 100 &&
           settings->default_uart_baud > 0;
}

static esp_err_t save_settings(void)
{
    ESP_ERROR_CHECK(nvs_set_blob(
        settings_nvs_handle, CONFIG_NVS_KEY, &current_settings, sizeof(current_settings)));
    return nvs_commit(settings_nvs_handle);
}

esp_err_t config_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(
        nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &settings_nvs_handle));

    size_t settings_size = sizeof(current_settings);
    result = nvs_get_blob(
        settings_nvs_handle, CONFIG_NVS_KEY, &current_settings, &settings_size);
    if (result == ESP_ERR_NVS_NOT_FOUND ||
        result == ESP_ERR_NVS_INVALID_LENGTH ||
        settings_size != sizeof(current_settings) ||
        !settings_are_valid(&current_settings)) {
        current_settings = DEFAULT_SETTINGS;
        ESP_ERROR_CHECK(save_settings());
        ESP_LOGI(TAG, "Default settings stored");
    } else {
        ESP_ERROR_CHECK(result);
        ESP_LOGI(TAG, "Settings loaded from NVS");
    }

    initialized = true;
    ESP_LOGI(TAG,
             "orientation=%d brightness=%u%% inversion=%d uart=%lu",
             current_settings.orientation,
             current_settings.brightness_percent,
             current_settings.color_inversion,
             (unsigned long)current_settings.default_uart_baud);
    return ESP_OK;
}

const toolbox_settings_t *config_service_get(void)
{
    return initialized ? &current_settings : NULL;
}

esp_err_t config_service_update(const toolbox_settings_t *settings)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!settings_are_valid(settings)) {
        return ESP_ERR_INVALID_ARG;
    }

    current_settings = *settings;
    return save_settings();
}

esp_err_t config_service_reset_defaults(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    current_settings = DEFAULT_SETTINGS;
    return save_settings();
}
