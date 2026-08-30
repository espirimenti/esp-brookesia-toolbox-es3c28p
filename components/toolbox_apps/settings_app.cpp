// SPDX-License-Identifier: GPL-3.0-only

#include <memory>

#include "board/board.h"
#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "assets/toolbox_icons.h"
#include "services/config_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Settings"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {
namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x101518;
constexpr uint32_t COLOR_PANEL = 0x1B2227;
constexpr uint32_t COLOR_TEXT = 0xF2F5F7;
constexpr uint32_t COLOR_MUTED = 0x9EABB3;
constexpr uint32_t COLOR_TEAL = 0x54D1C1;
constexpr uint32_t COLOR_AMBER = 0xF6D365;

lv_obj_t *createButton(lv_obj_t *parent, const char *text,
                       int32_t width, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 31);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void stylePanel(lv_obj_t *panel)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

} // namespace

class ToolboxSettingsApp final : public phone::App {
public:
    static ToolboxSettingsApp *requestInstance()
    {
        static ToolboxSettingsApp instance;
        return &instance;
    }

protected:
    ToolboxSettingsApp(): App("Settings", &toolbox_icon_settings, true, true, true) {}

    bool run() override
    {
        const esp_err_t init_result = config_service_init();
        if (init_result != ESP_OK) {
            ESP_UTILS_LOGE("Config init failed: %s",
                           esp_err_to_name(init_result));
            return false;
        }

        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(screen);

        const int32_t width = lv_obj_get_width(screen);

        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, "Display");
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(title, 6, 5);

        lv_obj_t *brightness_panel = lv_obj_create(screen);
        lv_obj_set_size(brightness_panel, width - 8, 61);
        lv_obj_set_pos(brightness_panel, 4, 24);
        stylePanel(brightness_panel);

        lv_obj_t *brightness_title = lv_label_create(brightness_panel);
        lv_label_set_text(brightness_title, "Brightness");
        lv_obj_set_style_text_color(
            brightness_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

        _brightness_value = lv_label_create(brightness_panel);
        lv_obj_set_width(_brightness_value, 52);
        lv_obj_set_style_text_align(
            _brightness_value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            _brightness_value, lv_color_hex(COLOR_AMBER), 0);
        lv_obj_align(_brightness_value, LV_ALIGN_TOP_RIGHT, 0, 0);

        _brightness = lv_slider_create(brightness_panel);
        lv_obj_set_size(_brightness, width - 30, 10);
        lv_obj_align(_brightness, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_slider_set_range(_brightness, 5, 100);
        lv_obj_set_style_bg_color(
            _brightness, lv_color_hex(COLOR_AMBER), LV_PART_INDICATOR);
        lv_obj_add_event_cb(_brightness, brightnessChangedCallback,
                            LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(_brightness, brightnessSaveCallback,
                            LV_EVENT_RELEASED, this);

        lv_obj_t *inversion_panel = lv_obj_create(screen);
        lv_obj_set_size(inversion_panel, width - 8, 43);
        lv_obj_set_pos(inversion_panel, 4, 90);
        stylePanel(inversion_panel);

        lv_obj_t *inversion_title = lv_label_create(inversion_panel);
        lv_label_set_text(inversion_title, "Color inversion");
        lv_obj_set_style_text_color(
            inversion_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_align(inversion_title, LV_ALIGN_LEFT_MID, 0, 0);

        _inversion = lv_switch_create(inversion_panel);
        lv_obj_set_size(_inversion, 46, 24);
        lv_obj_align(_inversion, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(
            _inversion, lv_color_hex(COLOR_TEAL),
            LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(_inversion, inversionCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        _status = lv_label_create(screen);
        lv_label_set_text(_status, "Settings are saved in flash");
        lv_obj_set_width(_status, width - 128);
        lv_label_set_long_mode(_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(
            _status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_status, 6, 148);

        lv_obj_t *reset = createButton(
            screen, "Reset defaults", 112, 0x633A3A);
        lv_obj_set_pos(reset, width - 116, 140);
        lv_obj_add_event_cb(reset, resetCallback,
                            LV_EVENT_CLICKED, this);

        syncFromConfig();
        return true;
    }

    bool back() override
    {
        return notifyCoreClosed();
    }

    bool close() override
    {
        saveBrightness();
        return true;
    }

    bool cleanResource() override
    {
        _brightness = nullptr;
        _brightness_value = nullptr;
        _inversion = nullptr;
        _status = nullptr;
        return true;
    }

private:
    void syncFromConfig()
    {
        const toolbox_settings_t *settings = config_service_get();
        if (settings == nullptr) {
            return;
        }
        lv_slider_set_value(
            _brightness, settings->brightness_percent, LV_ANIM_OFF);
        lv_label_set_text_fmt(_brightness_value, "%u%%",
                              settings->brightness_percent);
        if (settings->color_inversion) {
            lv_obj_add_state(_inversion, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(_inversion, LV_STATE_CHECKED);
        }
    }

    void applyBrightness()
    {
        const uint8_t value = static_cast<uint8_t>(
            lv_slider_get_value(_brightness));
        lv_label_set_text_fmt(_brightness_value, "%u%%", value);
        const esp_err_t result = board_display_set_brightness(value);
        if (result != ESP_OK) {
            showResult("Brightness", result);
        }
    }

    void saveBrightness()
    {
        const toolbox_settings_t *current = config_service_get();
        if (current == nullptr || _brightness == nullptr) {
            return;
        }
        toolbox_settings_t settings = *current;
        settings.brightness_percent = static_cast<uint8_t>(
            lv_slider_get_value(_brightness));
        showResult("Brightness saved",
                   config_service_update(&settings));
    }

    void toggleInversion()
    {
        const bool enabled =
            lv_obj_has_state(_inversion, LV_STATE_CHECKED);
        esp_err_t result = board_display_set_color_inversion(enabled);
        if (result == ESP_OK) {
            const toolbox_settings_t *current = config_service_get();
            if (current == nullptr) {
                result = ESP_ERR_INVALID_STATE;
            } else {
                toolbox_settings_t settings = *current;
                settings.color_inversion = enabled;
                result = config_service_update(&settings);
            }
        }
        showResult(enabled ? "Inversion on" : "Inversion off", result);
    }

    void resetDefaults()
    {
        esp_err_t result = config_service_reset_defaults();
        const toolbox_settings_t *settings = config_service_get();
        if (result == ESP_OK && settings != nullptr) {
            result = board_display_set_brightness(
                settings->brightness_percent);
            if (result == ESP_OK) {
                result = board_display_set_color_inversion(
                    settings->color_inversion);
            }
        }
        syncFromConfig();
        showResult("Defaults restored", result);
    }

    void showResult(const char *message, esp_err_t result)
    {
        if (_status == nullptr) {
            return;
        }
        if (result == ESP_OK) {
            lv_label_set_text(_status, message);
            lv_obj_set_style_text_color(
                _status, lv_color_hex(COLOR_TEAL), 0);
        } else {
            lv_label_set_text_fmt(_status, "%s: %s",
                                  message, esp_err_to_name(result));
            lv_obj_set_style_text_color(
                _status, lv_color_hex(0xE06C75), 0);
        }
    }

    static ToolboxSettingsApp *app(lv_event_t *event)
    {
        return static_cast<ToolboxSettingsApp *>(
            lv_event_get_user_data(event));
    }

    static void brightnessChangedCallback(lv_event_t *event)
    {
        app(event)->applyBrightness();
    }

    static void brightnessSaveCallback(lv_event_t *event)
    {
        app(event)->saveBrightness();
    }

    static void inversionCallback(lv_event_t *event)
    {
        app(event)->toggleInversion();
    }

    static void resetCallback(lv_event_t *event)
    {
        app(event)->resetDefaults();
    }

    lv_obj_t *_brightness = nullptr;
    lv_obj_t *_brightness_value = nullptr;
    lv_obj_t *_inversion = nullptr;
    lv_obj_t *_status = nullptr;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    base::App, ToolboxSettingsApp, "Settings", []() {
        return std::shared_ptr<ToolboxSettingsApp>(
            ToolboxSettingsApp::requestInstance(),
            [](ToolboxSettingsApp *) {});
    })

} // namespace esp_brookesia::apps