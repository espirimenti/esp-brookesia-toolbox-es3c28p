// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SCAN_MAX_RESULTS 48
#define BLE_SCAN_NAME_CAPACITY 32

typedef enum {
    BLE_SCAN_STATE_INITIALIZING,
    BLE_SCAN_STATE_READY,
    BLE_SCAN_STATE_SCANNING,
    BLE_SCAN_STATE_COMPLETE,
    BLE_SCAN_STATE_ERROR,
} ble_scan_state_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    uint8_t event_type;
    int8_t rssi;
    char name[BLE_SCAN_NAME_CAPACITY];
    bool connectable;
    bool has_manufacturer;
    uint16_t manufacturer_id;
    uint8_t service_count;
    uint8_t data_length;
} ble_scan_result_t;

esp_err_t ble_scan_service_init(void);
esp_err_t ble_scan_service_start(uint32_t duration_ms);
esp_err_t ble_scan_service_cancel(void);
ble_scan_state_t ble_scan_service_state(void);
size_t ble_scan_service_snapshot(ble_scan_result_t *results,
                                 size_t capacity);
uint32_t ble_scan_service_revision(void);
int ble_scan_service_last_error(void);

#ifdef __cplusplus
}
#endif
