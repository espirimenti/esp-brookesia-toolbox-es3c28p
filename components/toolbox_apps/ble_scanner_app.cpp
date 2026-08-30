// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <cstdio>
#include <memory>

#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "assets/toolbox_icons.h"
#include "services/ble_scan_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BleScanner"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {
namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x101518;
constexpr uint32_t COLOR_PANEL = 0x1B2227;
constexpr uint32_t COLOR_TEXT = 0xF2F5F7;
constexpr uint32_t COLOR_MUTED = 0x9EABB3;
constexpr uint32_t COLOR_GREEN = 0x49B982;
constexpr uint32_t COLOR_AMBER = 0xF6D365;
constexpr uint32_t COLOR_RED = 0xE06C75;
constexpr uint32_t COLOR_TEAL = 0x54D1C1;
constexpr int32_t NAV_SAFE_BOTTOM = 36;
constexpr uint32_t SCAN_DURATION_MS = 6000;
constexpr uint32_t UPDATE_PERIOD_MS = 200;

uint32_t rssiColor(int8_t rssi)
{
    if (rssi >= -55) {
        return COLOR_GREEN;
    }
    if (rssi >= -70) {
        return COLOR_AMBER;
    }
    return COLOR_RED;
}

void formatAddress(char *buffer, size_t capacity, const uint8_t *address)
{
    std::snprintf(buffer, capacity, "%02X:%02X:%02X:%02X:%02X:%02X",
                  address[5], address[4], address[3],
                  address[2], address[1], address[0]);
}

} // namespace

class ToolboxBleScannerApp final : public phone::App {
public:
    static ToolboxBleScannerApp *requestInstance()
    {
        static ToolboxBleScannerApp instance;
        return &instance;
    }

protected:
    ToolboxBleScannerApp(): App("BLE Scanner", &toolbox_icon_ble_scanner, true, true, true) {}

    bool run() override
    {
        const esp_err_t result = ble_scan_service_init();
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("BLE init failed: %s", esp_err_to_name(result));
            return false;
        }

        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(screen);

        const int32_t width = lv_obj_get_width(screen);
        const int32_t height =
            lv_obj_get_height(screen) - NAV_SAFE_BOTTOM;

        _scan_button = lv_button_create(screen);
        lv_obj_set_size(_scan_button, 82, 32);
        lv_obj_set_pos(_scan_button, 5, 3);
        lv_obj_set_style_radius(_scan_button, 5, 0);
        lv_obj_set_style_bg_color(_scan_button, lv_color_hex(0x247A59), 0);
        lv_obj_set_style_shadow_width(_scan_button, 0, 0);
        lv_obj_add_event_cb(_scan_button, scanCallback,
                            LV_EVENT_CLICKED, this);
        lv_obj_t *button_label = lv_label_create(_scan_button);
        lv_label_set_text(button_label, LV_SYMBOL_REFRESH " Scan");
        lv_obj_center(button_label);

        _status = lv_label_create(screen);
        lv_obj_set_width(_status, width - 102);
        lv_label_set_long_mode(_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_status, 95, 11);
        lv_label_set_text(_status, "BLE initializing");

        _progress = lv_bar_create(screen);
        lv_obj_set_size(_progress, width - 10, 6);
        lv_obj_set_pos(_progress, 5, 39);
        lv_bar_set_range(_progress, 0, 100);
        lv_bar_set_value(_progress, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_progress, lv_color_hex(0x283138), 0);
        lv_obj_set_style_bg_color(
            _progress, lv_color_hex(COLOR_TEAL), LV_PART_INDICATOR);

        _list = lv_obj_create(screen);
        lv_obj_set_size(_list, width - 10, height - 50);
        lv_obj_set_pos(_list, 5, 48);
        lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(_list, 3, 0);
        lv_obj_set_style_pad_row(_list, 3, 0);
        lv_obj_set_style_radius(_list, 4, 0);
        lv_obj_set_style_bg_color(_list, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_list, 0, 0);
        lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

        _displayed_revision = UINT32_MAX;
        _displayed_state = static_cast<ble_scan_state_t>(-1);
        _result_count = 0;
        _auto_start = true;
        _timer = lv_timer_create(timerCallback, UPDATE_PERIOD_MS, this);
        update();
        return _timer != nullptr;
    }

    bool back() override
    {
        return notifyCoreClosed();
    }

    bool close() override
    {
        if (_timer != nullptr) {
            lv_timer_pause(_timer);
        }
        ble_scan_service_cancel();
        return true;
    }

    bool cleanResource() override
    {
        _scan_button = nullptr;
        _status = nullptr;
        _progress = nullptr;
        _list = nullptr;
        _timer = nullptr;
        return true;
    }

private:
    void startScan()
    {
        const esp_err_t result =
            ble_scan_service_start(SCAN_DURATION_MS);
        if (result != ESP_OK) {
            lv_label_set_text(_status, "BLE not ready");
            ESP_UTILS_LOGW("Scan start failed: %s",
                           esp_err_to_name(result));
            return;
        }

        _auto_start = false;
        _scan_started_us = esp_timer_get_time();
        _displayed_revision = UINT32_MAX;
        _result_count = 0;
        lv_obj_clean(_list);
        lv_bar_set_value(_progress, 0, LV_ANIM_OFF);
        lv_label_set_text(_status, "Scanning...");
        lv_obj_add_state(_scan_button, LV_STATE_DISABLED);
    }

    void rebuildResults()
    {
        _result_count = ble_scan_service_snapshot(
            _results, BLE_SCAN_MAX_RESULTS);
        std::sort(_results, _results + _result_count,
                  [](const ble_scan_result_t &a,
                     const ble_scan_result_t &b) {
                      return a.rssi > b.rssi;
                  });

        lv_obj_clean(_list);
        for (size_t i = 0; i < _result_count; ++i) {
            addResult(_results[i]);
        }

        if (_result_count == 0 &&
            ble_scan_service_state() == BLE_SCAN_STATE_COMPLETE) {
            lv_obj_t *empty = lv_label_create(_list);
            lv_label_set_text(empty, "No BLE advertisements found");
            lv_obj_set_width(empty, LV_PCT(100));
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_MUTED), 0);
        }
    }

    void addResult(const ble_scan_result_t &result)
    {
        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 52);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, result.name[0] != 0
            ? result.name : "(unnamed)");
        lv_obj_set_width(name, 214);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(name, 6, 4);

        lv_obj_t *rssi = lv_label_create(row);
        lv_obj_set_width(rssi, 72);
        lv_obj_set_style_text_align(rssi, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            rssi, lv_color_hex(rssiColor(result.rssi)), 0);
        lv_label_set_text_fmt(rssi, "%d dBm", result.rssi);
        lv_obj_align(rssi, LV_ALIGN_TOP_RIGHT, -6, 4);

        char address[18];
        formatAddress(address, sizeof(address), result.address);
        lv_obj_t *address_label = lv_label_create(row);
        lv_label_set_text(address_label, address);
        lv_obj_set_style_text_color(
            address_label, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(address_label, 6, 28);

        lv_obj_t *details = lv_label_create(row);
        lv_obj_set_width(details, 102);
        lv_obj_set_style_text_align(details, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(details, lv_color_hex(COLOR_TEAL), 0);
        if (result.has_manufacturer) {
            lv_label_set_text_fmt(
                details, "MFG %04X", result.manufacturer_id);
        } else if (result.service_count > 0) {
            lv_label_set_text_fmt(
                details, "%u svc", result.service_count);
        } else {
            lv_label_set_text(
                details, result.connectable ? "Connectable" : "Beacon");
        }
        lv_obj_align(details, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    }

    void update()
    {
        ble_scan_state_t state = ble_scan_service_state();
        if (_auto_start &&
            (state == BLE_SCAN_STATE_READY ||
             state == BLE_SCAN_STATE_COMPLETE)) {
            startScan();
            state = ble_scan_service_state();
        }

        const uint32_t revision = ble_scan_service_revision();
        if (revision != _displayed_revision) {
            _displayed_revision = revision;
            rebuildResults();
        }

        if (state == BLE_SCAN_STATE_SCANNING) {
            const int64_t elapsed_ms =
                (esp_timer_get_time() - _scan_started_us) / 1000;
            const int32_t progress =
                elapsed_ms >= SCAN_DURATION_MS
                    ? 99
                    : static_cast<int32_t>(
                          elapsed_ms * 100 / SCAN_DURATION_MS);
            lv_bar_set_value(_progress, progress, LV_ANIM_OFF);
            lv_label_set_text_fmt(_status, "Scanning: %u",
                static_cast<unsigned>(_result_count));
        }

        if (state == _displayed_state) {
            return;
        }
        _displayed_state = state;
        switch (state) {
        case BLE_SCAN_STATE_COMPLETE:
            rebuildResults();
            lv_bar_set_value(_progress, 100, LV_ANIM_OFF);
            lv_label_set_text_fmt(_status, "%u devices",
                static_cast<unsigned>(_result_count));
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            break;
        case BLE_SCAN_STATE_READY:
            lv_label_set_text(_status, "Ready");
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            break;
        case BLE_SCAN_STATE_ERROR:
            lv_label_set_text_fmt(_status, "BLE error %d",
                                  ble_scan_service_last_error());
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            break;
        case BLE_SCAN_STATE_INITIALIZING:
            lv_label_set_text(_status, "BLE initializing");
            lv_obj_add_state(_scan_button, LV_STATE_DISABLED);
            break;
        case BLE_SCAN_STATE_SCANNING:
        default:
            lv_obj_add_state(_scan_button, LV_STATE_DISABLED);
            break;
        }
    }

    static ToolboxBleScannerApp *app(lv_event_t *event)
    {
        return static_cast<ToolboxBleScannerApp *>(
            lv_event_get_user_data(event));
    }

    static void scanCallback(lv_event_t *event)
    {
        app(event)->startScan();
    }

    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxBleScannerApp *>(
            lv_timer_get_user_data(timer))->update();
    }

    lv_obj_t *_scan_button = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_progress = nullptr;
    lv_obj_t *_list = nullptr;
    lv_timer_t *_timer = nullptr;
    ble_scan_result_t _results[BLE_SCAN_MAX_RESULTS]{};
    size_t _result_count = 0;
    uint32_t _displayed_revision = UINT32_MAX;
    ble_scan_state_t _displayed_state =
        static_cast<ble_scan_state_t>(-1);
    int64_t _scan_started_us = 0;
    bool _auto_start = true;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    base::App, ToolboxBleScannerApp, "BLE Scanner", []() {
        return std::shared_ptr<ToolboxBleScannerApp>(
            ToolboxBleScannerApp::requestInstance(),
            [](ToolboxBleScannerApp *) {});
    })

} // namespace esp_brookesia::apps
