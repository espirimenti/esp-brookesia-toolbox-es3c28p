// SPDX-License-Identifier: GPL-3.0-only

#include "services/ble_scan_service.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include <string.h>

static const char *TAG = "ble_scan";
static SemaphoreHandle_t result_lock;
static bool initialized;
static ble_scan_result_t scan_results[BLE_SCAN_MAX_RESULTS];
static size_t scan_result_count;
static volatile ble_scan_state_t scan_state =
    BLE_SCAN_STATE_INITIALIZING;
static volatile uint32_t result_revision;
static volatile int last_error;
static volatile bool host_synced;
static volatile bool cancel_requested;

static bool is_connectable(uint8_t event_type)
{
    return event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
           event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
}

static ble_scan_result_t *find_result(const ble_addr_t *address)
{
    for (size_t i = 0; i < scan_result_count; i++) {
        if (scan_results[i].address_type == address->type &&
            memcmp(
                scan_results[i].address,
                address->val,
                sizeof(scan_results[i].address)) == 0) {
            return &scan_results[i];
        }
    }
    return NULL;
}

static void update_result(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields = {0};
    const int parse_result = ble_hs_adv_parse_fields(
        &fields, disc->data, disc->length_data);

    xSemaphoreTake(result_lock, portMAX_DELAY);
    ble_scan_result_t *result = find_result(&disc->addr);
    if (result == NULL) {
        if (scan_result_count >= BLE_SCAN_MAX_RESULTS) {
            xSemaphoreGive(result_lock);
            return;
        }
        result = &scan_results[scan_result_count++];
        memset(result, 0, sizeof(*result));
        memcpy(
            result->address,
            disc->addr.val,
            sizeof(result->address));
        result->address_type = disc->addr.type;
        result->event_type = disc->event_type;
    } else if (
        result->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        result->event_type = disc->event_type;
    }

    result->rssi = disc->rssi;
    result->connectable =
        result->connectable || is_connectable(disc->event_type);
    if (disc->length_data > result->data_length) {
        result->data_length = disc->length_data;
    }

    if (parse_result == 0) {
        if (fields.name != NULL && fields.name_len > 0) {
            const size_t name_length =
                fields.name_len < BLE_SCAN_NAME_CAPACITY - 1
                    ? fields.name_len
                    : BLE_SCAN_NAME_CAPACITY - 1;
            memcpy(result->name, fields.name, name_length);
            result->name[name_length] = '\0';
        }
        if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
            result->manufacturer_id =
                (uint16_t)fields.mfg_data[0] |
                ((uint16_t)fields.mfg_data[1] << 8);
            result->has_manufacturer = true;
        }
        const uint16_t service_count =
            fields.num_uuids16 + fields.num_uuids32 +
            fields.num_uuids128;
        if (service_count > result->service_count) {
            result->service_count =
                service_count > UINT8_MAX
                    ? UINT8_MAX
                    : (uint8_t)service_count;
        }
    }
    result_revision++;
    xSemaphoreGive(result_lock);
}

static int gap_event_cb(struct ble_gap_event *event, void *argument)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        update_result(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_state = cancel_requested
                         ? BLE_SCAN_STATE_READY
                         : BLE_SCAN_STATE_COMPLETE;
        cancel_requested = false;
        ESP_LOGI(
            TAG,
            "BLE scan complete: %u device(s)",
            (unsigned)scan_result_count);
        return 0;
    default:
        return 0;
    }
}

static void host_reset_cb(int reason)
{
    host_synced = false;
    last_error = reason;
    scan_state = BLE_SCAN_STATE_ERROR;
    ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
}

static void host_sync_cb(void)
{
    const int result = ble_hs_util_ensure_addr(0);
    if (result != 0) {
        last_error = result;
        scan_state = BLE_SCAN_STATE_ERROR;
        ESP_LOGE(TAG, "Unable to configure BLE address: rc=%d", result);
        return;
    }
    host_synced = true;
    scan_state = BLE_SCAN_STATE_READY;
    ESP_LOGI(TAG, "NimBLE observer ready");
}

static void host_task(void *argument)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_scan_service_init(void)
{
    if (initialized) {
        return ESP_OK;
    }
    result_lock = xSemaphoreCreateMutex();
    if (result_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result = nimble_port_init();
    if (result != ESP_OK) {
        scan_state = BLE_SCAN_STATE_ERROR;
        last_error = result;
        return result;
    }

    ble_hs_cfg.reset_cb = host_reset_cb;
    ble_hs_cfg.sync_cb = host_sync_cb;
    nimble_port_freertos_init(host_task);
    initialized = true;
    ESP_LOGI(TAG, "BLE scan service initialized");
    return ESP_OK;
}

esp_err_t ble_scan_service_start(uint32_t duration_ms)
{
    if (!host_synced ||
        (scan_state != BLE_SCAN_STATE_READY &&
         scan_state != BLE_SCAN_STATE_COMPLETE &&
         scan_state != BLE_SCAN_STATE_ERROR)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms < 100 || duration_ms > INT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t own_address_type = 0;
    int result = ble_hs_id_infer_auto(0, &own_address_type);
    if (result != 0) {
        last_error = result;
        scan_state = BLE_SCAN_STATE_ERROR;
        return ESP_FAIL;
    }

    const struct ble_gap_disc_params parameters = {
        .itvl = 0x50,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };
    xSemaphoreTake(result_lock, portMAX_DELAY);
    memset(scan_results, 0, sizeof(scan_results));
    scan_result_count = 0;
    result_revision++;
    xSemaphoreGive(result_lock);
    cancel_requested = false;
    last_error = 0;
    scan_state = BLE_SCAN_STATE_SCANNING;
    result = ble_gap_disc(
        own_address_type,
        (int32_t)duration_ms,
        &parameters,
        gap_event_cb,
        NULL);
    if (result != 0) {
        last_error = result;
        scan_state = BLE_SCAN_STATE_ERROR;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_scan_service_cancel(void)
{
    if (scan_state != BLE_SCAN_STATE_SCANNING) {
        return ESP_OK;
    }
    cancel_requested = true;
    const int result = ble_gap_disc_cancel();
    if (result != 0) {
        cancel_requested = false;
        last_error = result;
        scan_state = BLE_SCAN_STATE_ERROR;
        return ESP_FAIL;
    }
    return ESP_OK;
}

ble_scan_state_t ble_scan_service_state(void)
{
    return scan_state;
}

size_t ble_scan_service_snapshot(ble_scan_result_t *results,
                                 size_t capacity)
{
    if (results == NULL || capacity == 0 || result_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(result_lock, portMAX_DELAY);
    const size_t count =
        scan_result_count < capacity ? scan_result_count : capacity;
    memcpy(results, scan_results, count * sizeof(results[0]));
    xSemaphoreGive(result_lock);
    return count;
}

uint32_t ble_scan_service_revision(void)
{
    return result_revision;
}

int ble_scan_service_last_error(void)
{
    return last_error;
}
