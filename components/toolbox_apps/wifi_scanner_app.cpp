// SPDX-License-Identifier: GPL-3.0-only

#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "assets/toolbox_icons.h"
#include "services/wifi_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WifiScanner"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {
namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x101518;
constexpr uint32_t COLOR_PANEL = 0x1B2227;
constexpr uint32_t COLOR_CONTROL = 0x2B353C;
constexpr uint32_t COLOR_TEXT = 0xF2F5F7;
constexpr uint32_t COLOR_MUTED = 0x9EABB3;
constexpr uint32_t COLOR_GREEN = 0x49B982;
constexpr uint32_t COLOR_AMBER = 0xF6D365;
constexpr uint32_t COLOR_RED = 0xE06C75;
constexpr uint32_t COLOR_BLUE = 0x4FC3F7;
constexpr int32_t NAV_SAFE_BOTTOM = 36;
constexpr size_t CHANNEL_COUNT = 14;

const char *authName(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "Open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "Enterprise";
    }
}

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

void drawLine(lv_layer_t *layer, lv_color_t color, int32_t width,
              int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

void prepareTab(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

} // namespace

class ToolboxWifiScannerApp final : public phone::App {
public:
    static ToolboxWifiScannerApp *requestInstance()
    {
        static ToolboxWifiScannerApp instance;
        return &instance;
    }

protected:
    ToolboxWifiScannerApp(): App("Wi-Fi Scanner", &toolbox_icon_wifi_scanner, true, true, true) {}

    bool run() override
    {
        const esp_err_t result = wifi_service_init();
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("Wi-Fi init failed: %s", esp_err_to_name(result));
            return false;
        }

        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(screen);

        _tabs = lv_tabview_create(screen);
        lv_obj_set_size(_tabs, lv_obj_get_width(screen),
                        lv_obj_get_height(screen) - NAV_SAFE_BOTTOM);
        lv_obj_set_pos(_tabs, 0, 0);
        lv_tabview_set_tab_bar_position(_tabs, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(_tabs, 28);
        lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(_tabs),
                                  lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_text_color(lv_tabview_get_tab_bar(_tabs),
                                    lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(lv_tabview_get_content(_tabs),
                                  lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_pad_all(lv_tabview_get_content(_tabs), 0, 0);

        lv_obj_t *networks = lv_tabview_add_tab(_tabs, "Networks");
        lv_obj_t *channels = lv_tabview_add_tab(_tabs, "Channels");
        prepareTab(networks);
        prepareTab(channels);
        lv_obj_update_layout(_tabs);

        buildNetworks(networks);
        buildChannels(channels);

        _previous_state = WIFI_SERVICE_STATE_ERROR;
        _timer = lv_timer_create(timerCallback, 100, this);
        updateState();
        startScan();
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
        const esp_err_t result = wifi_service_stop();
        if (result != ESP_OK) {
            ESP_UTILS_LOGW("Wi-Fi stop failed: %s", esp_err_to_name(result));
        }
        return true;
    }

    bool cleanResource() override
    {
        _tabs = nullptr;
        _scan_button = nullptr;
        _status = nullptr;
        _list = nullptr;
        _chart = nullptr;
        _chart_summary = nullptr;
        _timer = nullptr;
        return true;
    }

private:
    void buildNetworks(lv_obj_t *tab)
    {
        const int32_t width = lv_obj_get_width(tab);
        const int32_t height = lv_obj_get_height(tab);

        _scan_button = lv_button_create(tab);
        lv_obj_set_size(_scan_button, 76, 31);
        lv_obj_set_pos(_scan_button, 4, 3);
        lv_obj_set_style_radius(_scan_button, 5, 0);
        lv_obj_set_style_bg_color(_scan_button, lv_color_hex(0x247A59), 0);
        lv_obj_set_style_shadow_width(_scan_button, 0, 0);
        lv_obj_add_event_cb(_scan_button, scanCallback, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_label_create(_scan_button);
        lv_label_set_text(label, LV_SYMBOL_REFRESH " Scan");
        lv_obj_center(label);

        _status = lv_label_create(tab);
        lv_obj_set_width(_status, width - 92);
        lv_label_set_long_mode(_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_status, 86, 10);
        lv_label_set_text(_status, "Ready");

        _list = lv_obj_create(tab);
        lv_obj_set_size(_list, width - 8, height - 41);
        lv_obj_set_pos(_list, 4, 38);
        lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(_list, 3, 0);
        lv_obj_set_style_pad_row(_list, 3, 0);
        lv_obj_set_style_bg_color(_list, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_list, 0, 0);
        lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);
    }

    void buildChannels(lv_obj_t *tab)
    {
        const int32_t width = lv_obj_get_width(tab);
        const int32_t height = lv_obj_get_height(tab);

        _chart = lv_obj_create(tab);
        lv_obj_set_size(_chart, width - 8, height - 8);
        lv_obj_set_pos(_chart, 4, 4);
        lv_obj_set_style_radius(_chart, 4, 0);
        lv_obj_set_style_bg_color(_chart, lv_color_hex(0x0B0F11), 0);
        lv_obj_set_style_bg_opa(_chart, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_chart, lv_color_hex(COLOR_CONTROL), 0);
        lv_obj_set_style_border_width(_chart, 1, 0);
        lv_obj_set_style_pad_all(_chart, 0, 0);
        lv_obj_clear_flag(_chart, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(_chart, chartDrawCallback,
                            LV_EVENT_DRAW_MAIN_END, this);
        lv_obj_update_layout(_chart);

        _chart_summary = lv_label_create(_chart);
        lv_obj_set_width(_chart_summary, LV_PCT(100));
        lv_obj_set_style_text_align(_chart_summary, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(_chart_summary, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_chart_summary, 0, 3);
        lv_label_set_text(_chart_summary, "Scan to view channels");

        const int32_t left = 22;
        const int32_t usable = lv_obj_get_width(_chart) - left - 4;
        const int32_t slot = usable / static_cast<int32_t>(CHANNEL_COUNT);
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            lv_obj_t *channel = lv_label_create(_chart);
            lv_obj_set_width(channel, slot);
            lv_obj_set_style_text_align(channel, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(channel, lv_color_hex(COLOR_MUTED), 0);
            lv_label_set_text_fmt(channel, "%u", static_cast<unsigned>(i + 1));
            lv_obj_set_pos(channel, left + static_cast<int32_t>(i) * slot,
                           lv_obj_get_height(_chart) - 17);
        }
    }

    void startScan()
    {
        lv_obj_clean(_list);
        std::memset(_channel_counts, 0, sizeof(_channel_counts));
        lv_obj_invalidate(_chart);
        lv_label_set_text(_chart_summary, "Scanning channels...");
        lv_label_set_text(_status, "Scanning...");
        lv_obj_add_state(_scan_button, LV_STATE_DISABLED);

        const esp_err_t result = wifi_service_start_scan();
        if (result != ESP_OK) {
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            lv_label_set_text(_status, "Scan error");
            lv_label_set_text(_chart_summary, "Scan error");
            ESP_UTILS_LOGE("Scan start failed: %s", esp_err_to_name(result));
        }
    }

    void rebuildResults()
    {
        lv_obj_clean(_list);
        std::memset(_channel_counts, 0, sizeof(_channel_counts));

        const wifi_ap_record_t *results = wifi_service_results();
        const size_t count = wifi_service_result_count();
        for (size_t i = 0; i < count; ++i) {
            addNetwork(results[i]);
            if (results[i].primary >= 1 &&
                results[i].primary <= CHANNEL_COUNT) {
                ++_channel_counts[results[i].primary - 1];
            }
        }

        if (count == 0) {
            lv_obj_t *empty = lv_label_create(_list);
            lv_label_set_text(empty, "No Wi-Fi networks found");
            lv_obj_set_width(empty, LV_PCT(100));
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_MUTED), 0);
        }

        uint8_t busiest_channel = 0;
        uint8_t busiest_count = 0;
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            if (_channel_counts[i] > busiest_count) {
                busiest_count = _channel_counts[i];
                busiest_channel = static_cast<uint8_t>(i + 1);
            }
        }
        if (busiest_channel == 0) {
            lv_label_set_text(_chart_summary, "No 2.4 GHz networks");
        } else {
            lv_label_set_text_fmt(_chart_summary, "%u APs | busiest CH %u (%u)",
                static_cast<unsigned>(count),
                static_cast<unsigned>(busiest_channel),
                static_cast<unsigned>(busiest_count));
        }
        lv_obj_invalidate(_chart);
    }

    void addNetwork(const wifi_ap_record_t &network)
    {
        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 43);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ssid = lv_label_create(row);
        lv_label_set_text(ssid, network.ssid[0] != 0
            ? reinterpret_cast<const char *>(network.ssid)
            : "<hidden>");
        lv_obj_set_width(ssid, 220);
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(ssid, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(ssid, 6, 3);

        lv_obj_t *rssi = lv_label_create(row);
        lv_obj_set_width(rssi, 72);
        lv_obj_set_style_text_align(rssi, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            rssi, lv_color_hex(rssiColor(network.rssi)), 0);
        lv_label_set_text_fmt(rssi, "%d dBm", network.rssi);
        lv_obj_align(rssi, LV_ALIGN_TOP_RIGHT, -6, 3);

        lv_obj_t *details = lv_label_create(row);
        lv_label_set_text_fmt(details, "CH %u   %s",
                              network.primary, authName(network.authmode));
        lv_obj_set_width(details, LV_PCT(96));
        lv_label_set_long_mode(details, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(details, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(details, 6, 23);
    }

    void updateState()
    {
        const wifi_service_state_t state = wifi_service_state();
        if (state == _previous_state) {
            return;
        }
        _previous_state = state;

        switch (state) {
        case WIFI_SERVICE_STATE_SCANNING:
            lv_label_set_text(_status, "Scanning...");
            lv_obj_add_state(_scan_button, LV_STATE_DISABLED);
            break;
        case WIFI_SERVICE_STATE_COMPLETE:
            rebuildResults();
            lv_label_set_text_fmt(_status, "%u networks",
                static_cast<unsigned>(wifi_service_result_count()));
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            break;
        case WIFI_SERVICE_STATE_ERROR:
            lv_label_set_text(_status, "Scan error");
            lv_label_set_text(_chart_summary, "Scan error");
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            ESP_UTILS_LOGE("Scan failed: %s",
                esp_err_to_name(wifi_service_last_error()));
            break;
        default:
            lv_label_set_text(_status, "Ready");
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            break;
        }
    }

    void drawChart(lv_event_t *event)
    {
        lv_layer_t *layer = lv_event_get_layer(event);
        lv_area_t area;
        lv_obj_get_coords(_chart, &area);

        const int32_t left = area.x1 + 22;
        const int32_t right = area.x2 - 4;
        const int32_t top = area.y1 + 24;
        const int32_t bottom = area.y2 - 20;
        const int32_t slot =
            (right - left + 1) / static_cast<int32_t>(CHANNEL_COUNT);

        drawLine(layer, lv_color_hex(COLOR_CONTROL), 1,
                 left, bottom, right, bottom);
        drawLine(layer, lv_color_hex(0x20282D), 1,
                 left, top, right, top);

        uint8_t maximum = 1;
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            if (_channel_counts[i] > maximum) {
                maximum = _channel_counts[i];
            }
        }

        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            const int32_t x =
                left + static_cast<int32_t>(i) * slot + slot / 2;
            const int32_t bar_height =
                (_channel_counts[i] * (bottom - top - 2)) / maximum;
            const int32_t y = bottom - bar_height;
            const uint32_t color =
                _channel_counts[i] == maximum && maximum > 0
                    ? COLOR_AMBER
                    : COLOR_BLUE;
            if (_channel_counts[i] > 0) {
                drawLine(layer, lv_color_hex(color), slot - 5,
                         x, bottom - 1, x, y);
            }
        }
    }

    static ToolboxWifiScannerApp *app(lv_event_t *event)
    {
        return static_cast<ToolboxWifiScannerApp *>(
            lv_event_get_user_data(event));
    }

    static void scanCallback(lv_event_t *event)
    {
        app(event)->startScan();
    }

    static void chartDrawCallback(lv_event_t *event)
    {
        app(event)->drawChart(event);
    }

    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxWifiScannerApp *>(
            lv_timer_get_user_data(timer))->updateState();
    }

    lv_obj_t *_tabs = nullptr;
    lv_obj_t *_scan_button = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_list = nullptr;
    lv_obj_t *_chart = nullptr;
    lv_obj_t *_chart_summary = nullptr;
    lv_timer_t *_timer = nullptr;
    wifi_service_state_t _previous_state = WIFI_SERVICE_STATE_ERROR;
    uint8_t _channel_counts[CHANNEL_COUNT]{};
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    base::App, ToolboxWifiScannerApp, "Wi-Fi Scanner", []() {
        return std::shared_ptr<ToolboxWifiScannerApp>(
            ToolboxWifiScannerApp::requestInstance(),
            [](ToolboxWifiScannerApp *) {});
    })

} // namespace esp_brookesia::apps
