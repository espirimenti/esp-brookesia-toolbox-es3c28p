// SPDX-License-Identifier: GPL-3.0-only

#include <memory>
#include <stdint.h>

#include "esp_brookesia.hpp"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "services/i2c_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "ToolboxApps"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x101518;
constexpr uint32_t COLOR_PANEL = 0x1B2227;
constexpr uint32_t COLOR_TEXT = 0xF2F5F7;
constexpr uint32_t COLOR_MUTED = 0x9EABB3;
constexpr uint32_t COLOR_GREEN = 0x49B982;
constexpr uint32_t COLOR_BLUE = 0x4FC3F7;

void style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 7, 0);
}

lv_obj_t *create_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, 48);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 7, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

const char *known_i2c_device(uint8_t address)
{
    switch (address) {
    case 0x18:
    case 0x19: return "ES8311 audio codec";
    case 0x20:
    case 0x21: return "PCF8574 GPIO";
    case 0x23: return "BH1750 light";
    case 0x29: return "VL53L0X distance";
    case 0x38: return "FT6336G touch";
    case 0x3C:
    case 0x3D: return "OLED display";
    case 0x40: return "INA219 / sensor";
    case 0x44: return "SHT3x sensor";
    case 0x48: return "ADS1115 / TMP102";
    case 0x50: return "EEPROM";
    case 0x57: return "MAX3010x";
    case 0x5A: return "MLX90614";
    case 0x60: return "MCP4725 DAC";
    case 0x68: return "RTC / IMU";
    case 0x69:
    case 0x6A: return "IMU sensor";
    case 0x76:
    case 0x77: return "BME/BMP280";
    default: return "Unknown device";
    }
}

bool is_onboard_i2c_device(uint8_t address)
{
    return address == 0x18 || address == 0x19 || address == 0x38;
}

} // namespace

class ToolboxSystemApp final : public phone::App {
public:
    static ToolboxSystemApp *requestInstance()
    {
        static ToolboxSystemApp instance;
        return &instance;
    }

protected:
    ToolboxSystemApp(): App("System", nullptr, true, true, true) {}

    bool run() override
    {
        lv_obj_t *screen = lv_screen_active();
        style_screen(screen);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(screen, 5, 0);

        lv_obj_t *device_card = create_card(screen, "ESP32-S3");
        _device = lv_label_create(device_card);
        lv_obj_set_style_text_color(_device, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_align(_device, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t *memory_card = create_card(screen, "Memory");
        _memory = lv_label_create(memory_card);
        lv_obj_set_style_text_color(_memory, lv_color_hex(COLOR_GREEN), 0);
        lv_obj_align(_memory, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t *uptime_card = create_card(screen, "Uptime");
        _uptime = lv_label_create(uptime_card);
        lv_obj_set_style_text_color(_uptime, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_align(_uptime, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        update();
        _timer = lv_timer_create(timerCallback, 1000, this);
        return _timer != nullptr;
    }

    bool back() override
    {
        return notifyCoreClosed();
    }

    bool cleanResource() override
    {
        _timer = nullptr;
        _device = nullptr;
        _memory = nullptr;
        _uptime = nullptr;
        return true;
    }

private:
    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxSystemApp *>(lv_timer_get_user_data(timer))->update();
    }

    void update()
    {
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        lv_label_set_text_fmt(_device, "%u cores, rev %u", chip.cores, chip.revision);

        const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        lv_label_set_text_fmt(_memory, "RAM %u KB   PSRAM %u KB",
                              static_cast<unsigned>(internal_free / 1024),
                              static_cast<unsigned>(psram_free / 1024));

        const uint64_t seconds = static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
        lv_label_set_text_fmt(_uptime, "%02llu:%02llu:%02llu",
                              static_cast<unsigned long long>(seconds / 3600ULL),
                              static_cast<unsigned long long>((seconds / 60ULL) % 60ULL),
                              static_cast<unsigned long long>(seconds % 60ULL));
    }

    lv_obj_t *_device = nullptr;
    lv_obj_t *_memory = nullptr;
    lv_obj_t *_uptime = nullptr;
    lv_timer_t *_timer = nullptr;
};

class ToolboxI2cApp final : public phone::App {
public:
    static ToolboxI2cApp *requestInstance()
    {
        static ToolboxI2cApp instance;
        return &instance;
    }

protected:
    ToolboxI2cApp(): App("I2C Scanner", nullptr, true, true, true) {}

    bool run() override
    {
        lv_obj_t *screen = lv_screen_active();
        style_screen(screen);

        _scan_button = lv_button_create(screen);
        lv_obj_set_size(_scan_button, 82, 32);
        lv_obj_align(_scan_button, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(_scan_button, lv_color_hex(0x247A59), 0);
        lv_obj_set_style_radius(_scan_button, 5, 0);
        lv_obj_add_event_cb(_scan_button, scanButtonCallback, LV_EVENT_CLICKED, this);
        lv_obj_t *button_label = lv_label_create(_scan_button);
        lv_label_set_text(button_label, LV_SYMBOL_REFRESH " Scan");
        lv_obj_center(button_label);

        _status = lv_label_create(screen);
        lv_label_set_text(_status, "Ready");
        lv_obj_set_width(_status, 180);
        lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_align(_status, LV_ALIGN_TOP_RIGHT, 0, 8);

        _progress = lv_bar_create(screen);
        lv_obj_set_size(_progress, LV_PCT(100), 6);
        lv_obj_align(_progress, LV_ALIGN_TOP_MID, 0, 39);
        lv_bar_set_range(_progress, 0, 100);
        lv_obj_set_style_bg_color(_progress, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);

        _results = lv_obj_create(screen);
        lv_obj_set_size(_results, LV_PCT(100), 116);
        lv_obj_align(_results, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(_results, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_results, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(_results, 5, 0);
        lv_obj_set_style_pad_row(_results, 4, 0);
        lv_obj_set_style_radius(_results, 5, 0);
        lv_obj_set_style_bg_color(_results, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_border_width(_results, 0, 0);

        _timer = lv_timer_create(scanTimerCallback, 4, this);
        if (_timer == nullptr) {
            return false;
        }
        lv_timer_pause(_timer);
        startScan();
        return true;
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
        return true;
    }

    bool cleanResource() override
    {
        _timer = nullptr;
        _scan_button = nullptr;
        _status = nullptr;
        _progress = nullptr;
        _results = nullptr;
        return true;
    }

private:
    static constexpr uint8_t FIRST_ADDRESS = 0x03;
    static constexpr uint8_t LAST_ADDRESS = 0x77;

    static void scanButtonCallback(lv_event_t *event)
    {
        static_cast<ToolboxI2cApp *>(lv_event_get_user_data(event))->startScan();
    }

    static void scanTimerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxI2cApp *>(lv_timer_get_user_data(timer))->scanNext();
    }

    void startScan()
    {
        lv_obj_clean(_results);
        lv_obj_add_state(_scan_button, LV_STATE_DISABLED);
        lv_label_set_text(_status, "Scanning...");
        lv_bar_set_value(_progress, 0, LV_ANIM_OFF);
        _address = FIRST_ADDRESS;
        _found = 0;
        _onboard = 0;
        _errors = 0;
        lv_timer_reset(_timer);
        lv_timer_resume(_timer);
    }

    void addResult(uint8_t address)
    {
        lv_obj_t *row = lv_obj_create(_results);
        lv_obj_set_size(row, LV_PCT(100), 34);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x252E34), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *address_label = lv_label_create(row);
        lv_label_set_text_fmt(address_label, "0x%02X", address);
        lv_obj_set_style_text_color(address_label, lv_color_hex(COLOR_GREEN), 0);
        lv_obj_align(address_label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *name_label = lv_label_create(row);
        lv_label_set_text(name_label, known_i2c_device(address));
        lv_obj_set_width(name_label, 205);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_align(name_label, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    void scanNext()
    {
        if (_address > LAST_ADDRESS) {
            lv_timer_pause(_timer);
            lv_obj_clear_state(_scan_button, LV_STATE_DISABLED);
            lv_bar_set_value(_progress, 100, LV_ANIM_OFF);
            lv_label_set_text_fmt(_status, "%u external, %u onboard", _found, _onboard);
            if (_found == 0) {
                lv_obj_t *empty = lv_label_create(_results);
                lv_label_set_text(empty, _errors == 0 ? "No external devices" : "Scan completed with errors");
                lv_obj_set_style_text_color(empty, lv_color_hex(COLOR_MUTED), 0);
            }
            return;
        }

        const esp_err_t result = i2c_service_probe(_address, 8);
        if (result == ESP_OK) {
            if (is_onboard_i2c_device(_address)) {
                ++_onboard;
            } else {
                addResult(_address);
                ++_found;
            }
        } else if (result != ESP_ERR_NOT_FOUND && result != ESP_ERR_TIMEOUT) {
            ++_errors;
        }

        const int32_t progress = ((_address - FIRST_ADDRESS + 1) * 100) /
                                 (LAST_ADDRESS - FIRST_ADDRESS + 1);
        lv_bar_set_value(_progress, progress, LV_ANIM_OFF);
        if ((_address & 0x07) == 0) {
            lv_label_set_text_fmt(_status, "Scanning 0x%02X", _address);
        }
        ++_address;
    }

    lv_obj_t *_scan_button = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_progress = nullptr;
    lv_obj_t *_results = nullptr;
    lv_timer_t *_timer = nullptr;
    uint8_t _address = FIRST_ADDRESS;
    uint8_t _found = 0;
    uint8_t _onboard = 0;
    uint8_t _errors = 0;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(base::App, ToolboxSystemApp, "System", []() {
    return std::shared_ptr<ToolboxSystemApp>(ToolboxSystemApp::requestInstance(), [](ToolboxSystemApp *) {});
})

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(base::App, ToolboxI2cApp, "I2C Scanner", []() {
    return std::shared_ptr<ToolboxI2cApp>(ToolboxI2cApp::requestInstance(), [](ToolboxI2cApp *) {});
})

} // namespace esp_brookesia::apps
