// SPDX-License-Identifier: GPL-3.0-only
#include <memory>
#include "driver/gpio.h"
#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "services/logic_analyzer_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "LogicAnalyzer"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {
namespace {
constexpr uint32_t BG = 0x101518;
constexpr uint32_t CONTROL = 0x2B353C;
constexpr uint32_t TEXT = 0xF2F5F7;
constexpr uint32_t MUTED = 0x9EABB3;
constexpr uint32_t ACTIVE = 0x247A59;
constexpr int32_t NAV_SAFE_BOTTOM = 36;
constexpr uint32_t COLORS[] = {0x58D68D, 0x4FC3F7, 0xF6D365, 0xE58AAE};
constexpr uint32_t RATES[] = {1000, 10000, 100000, 500000, 1000000};
constexpr uint16_t VIEWS[] = {128, 256, 512, 1024};

struct Trigger {
    gpio_num_t pin;
    logic_analyzer_trigger_edge_t edge;
};
constexpr Trigger TRIGGERS[] = {
    {GPIO_NUM_NC, LOGIC_ANALYZER_TRIGGER_NONE},
    {GPIO_NUM_2, LOGIC_ANALYZER_TRIGGER_RISING},
    {GPIO_NUM_2, LOGIC_ANALYZER_TRIGGER_FALLING},
    {GPIO_NUM_3, LOGIC_ANALYZER_TRIGGER_RISING},
    {GPIO_NUM_3, LOGIC_ANALYZER_TRIGGER_FALLING},
    {GPIO_NUM_14, LOGIC_ANALYZER_TRIGGER_RISING},
    {GPIO_NUM_14, LOGIC_ANALYZER_TRIGGER_FALLING},
    {GPIO_NUM_21, LOGIC_ANALYZER_TRIGGER_RISING},
    {GPIO_NUM_21, LOGIC_ANALYZER_TRIGGER_FALLING},
};

void styleDropdown(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(CONTROL), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
}

void drawLine(lv_layer_t *layer, lv_color_t color,
              int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = 1;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}
} // namespace

class ToolboxLogicAnalyzerApp final : public phone::App {
public:
    static ToolboxLogicAnalyzerApp *requestInstance()
    {
        static ToolboxLogicAnalyzerApp instance;
        return &instance;
    }

protected:
    ToolboxLogicAnalyzerApp(): App("Logic Analyzer", nullptr, true, true, true) {}

    bool run() override
    {
        const esp_err_t err = logic_analyzer_service_init();
        if (err != ESP_OK) {
            ESP_UTILS_LOGE("Init failed: %s", esp_err_to_name(err));
            return false;
        }
        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(screen);
        build(screen);
        _previous = LOGIC_ANALYZER_STATE_ERROR;
        _timer = lv_timer_create(timerCallback, 50, this);
        updateState();
        return _timer != nullptr;
    }

    bool back() override { return notifyCoreClosed(); }

    bool close() override
    {
        if (_timer != nullptr) {
            lv_timer_pause(_timer);
        }
        logic_analyzer_service_cancel();
        return true;
    }

    bool cleanResource() override
    {
        _rate = _trigger = _run_label = _waveform = nullptr;
        _capture_status = _window_status = _view = _offset = nullptr;
        _timer = nullptr;
        return true;
    }

private:
    void build(lv_obj_t *screen)
    {
        const int32_t width = lv_obj_get_width(screen);
        const int32_t height = lv_obj_get_height(screen);
        const int32_t bottom_controls_y = height - NAV_SAFE_BOTTOM - 32;

        _rate = lv_dropdown_create(screen);
        lv_dropdown_set_options(_rate, "1 kHz\n10 kHz\n100 kHz\n500 kHz\n1 MHz");
        lv_dropdown_set_selected(_rate, 2);
        lv_obj_set_size(_rate, 76, 32);
        lv_obj_set_pos(_rate, 4, 4);
        styleDropdown(_rate);

        _trigger = lv_dropdown_create(screen);
        lv_dropdown_set_options(_trigger,
            "Auto\nIO2 rising\nIO2 falling\nIO3 rising\nIO3 falling\n"
            "IO14 rising\nIO14 falling\nIO21 rising\nIO21 falling");
        lv_obj_set_size(_trigger, width - 146, 32);
        lv_obj_set_pos(_trigger, 84, 4);
        styleDropdown(_trigger);

        lv_obj_t *button = lv_button_create(screen);
        lv_obj_set_size(button, 54, 32);
        lv_obj_set_pos(button, width - 58, 4);
        lv_obj_set_style_radius(button, 5, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(ACTIVE), 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, runCallback, LV_EVENT_CLICKED, this);
        _run_label = lv_label_create(button);
        lv_label_set_text(_run_label, "Run");
        lv_obj_center(_run_label);

        _waveform = lv_obj_create(screen);
        lv_obj_set_size(_waveform, width - 8, bottom_controls_y - 44);
        lv_obj_set_pos(_waveform, 4, 40);
        lv_obj_set_style_radius(_waveform, 3, 0);
        lv_obj_set_style_bg_color(_waveform, lv_color_hex(0x0B0F11), 0);
        lv_obj_set_style_bg_opa(_waveform, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_waveform, lv_color_hex(CONTROL), 0);
        lv_obj_set_style_border_width(_waveform, 1, 0);
        lv_obj_set_style_pad_all(_waveform, 0, 0);
        lv_obj_clear_flag(_waveform, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(_waveform, drawCallback,
                            LV_EVENT_DRAW_MAIN_END, this);
        lv_obj_update_layout(_waveform);

        static const char *names[] = {"2", "3", "14", "21"};
        const int32_t band =
            lv_obj_get_height(_waveform) / LOGIC_ANALYZER_CHANNEL_COUNT;
        for (size_t i = 0; i < LOGIC_ANALYZER_CHANNEL_COUNT; ++i) {
            lv_obj_t *label = lv_label_create(_waveform);
            lv_label_set_text(label, names[i]);
            lv_obj_set_style_text_color(label, lv_color_hex(COLORS[i]), 0);
            lv_obj_set_pos(label, 5, static_cast<int32_t>(i) * band + 3);
        }

        _capture_status = lv_label_create(_waveform);
        lv_label_set_text(_capture_status, "Ready");
        styleOverlay(_capture_status);
        lv_obj_align(_capture_status, LV_ALIGN_TOP_RIGHT, -4, 2);

        _window_status = lv_label_create(_waveform);
        lv_label_set_text(_window_status, "No capture");
        styleOverlay(_window_status);
        lv_obj_align(_window_status, LV_ALIGN_BOTTOM_RIGHT, -4, -2);

        _view = lv_dropdown_create(screen);
        lv_dropdown_set_options(_view, "128 smp\n256 smp\n512 smp\n1024 smp");
        lv_dropdown_set_selected(_view, 1);
        lv_obj_set_size(_view, 84, 28);
        lv_obj_set_pos(_view, 4, bottom_controls_y);
        styleDropdown(_view);
        lv_obj_add_event_cb(_view, viewCallback, LV_EVENT_VALUE_CHANGED, this);

        _offset = lv_slider_create(screen);
        lv_slider_set_range(_offset, 0, 1);
        lv_slider_set_value(_offset, 0, LV_ANIM_OFF);
        lv_obj_set_size(_offset, width - 102, 10);
        lv_obj_set_pos(_offset, 94, bottom_controls_y + 9);
        lv_obj_set_style_bg_color(_offset, lv_color_hex(ACTIVE),
                                  LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_offset, lv_color_hex(TEXT), LV_PART_KNOB);
        lv_obj_add_state(_offset, LV_STATE_DISABLED);
        lv_obj_add_event_cb(_offset, offsetCallback,
                            LV_EVENT_VALUE_CHANGED, this);
    }

    static void styleOverlay(lv_obj_t *obj)
    {
        lv_obj_set_style_text_color(obj, lv_color_hex(MUTED), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x0B0F11), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    }

    uint32_t sampleRate() const
    {
        const uint32_t i = lv_dropdown_get_selected(_rate);
        return i < sizeof(RATES) / sizeof(RATES[0]) ? RATES[i] : 100000;
    }

    uint16_t viewCount() const
    {
        const uint32_t i = lv_dropdown_get_selected(_view);
        return i < sizeof(VIEWS) / sizeof(VIEWS[0]) ? VIEWS[i] : 256;
    }

    static void formatDuration(char *out, size_t size,
                               size_t samples, uint32_t rate)
    {
        if (rate == 0) {
            snprintf(out, size, "--");
            return;
        }
        const uint64_t us = static_cast<uint64_t>(samples) * 1000000ULL / rate;
        if (us >= 1000000) {
            snprintf(out, size, "%llu.%01llu s",
                static_cast<unsigned long long>(us / 1000000),
                static_cast<unsigned long long>((us % 1000000) / 100000));
        } else if (us >= 1000) {
            snprintf(out, size, "%llu.%01llu ms",
                static_cast<unsigned long long>(us / 1000),
                static_cast<unsigned long long>((us % 1000) / 100));
        } else {
            snprintf(out, size, "%llu us",
                     static_cast<unsigned long long>(us));
        }
    }

    void updateWindow()
    {
        const size_t available = logic_analyzer_service_sample_count();
        uint16_t visible = viewCount();
        if (available > 0 && visible > available) {
            visible = static_cast<uint16_t>(available);
        }
        const int32_t max_offset =
            available > visible ? static_cast<int32_t>(available - visible) : 0;
        lv_slider_set_range(_offset, 0, max_offset > 0 ? max_offset : 1);
        if (lv_slider_get_value(_offset) > max_offset) {
            lv_slider_set_value(_offset, max_offset, LV_ANIM_OFF);
        }
        if (max_offset > 0) {
            lv_obj_clear_state(_offset, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_offset, LV_STATE_DISABLED);
        }
        char duration[24];
        formatDuration(duration, sizeof(duration), visible,
                       logic_analyzer_service_sample_rate_hz());
        lv_label_set_text_fmt(_window_status, "%u smp / %s",
                              static_cast<unsigned>(visible), duration);
        lv_obj_invalidate(_waveform);
    }

    void startOrStop()
    {
        const logic_analyzer_state_t state = logic_analyzer_service_state();
        if (state == LOGIC_ANALYZER_STATE_WAITING_TRIGGER ||
            state == LOGIC_ANALYZER_STATE_CAPTURING) {
            logic_analyzer_service_cancel();
            lv_label_set_text(_capture_status, "Stopping");
            return;
        }

        const uint32_t i = lv_dropdown_get_selected(_trigger);
        const Trigger trigger =
            i < sizeof(TRIGGERS) / sizeof(TRIGGERS[0]) ? TRIGGERS[i] : TRIGGERS[0];
        logic_analyzer_config_t config{};
        config.sample_rate_hz = sampleRate();
        config.trigger_pin = trigger.pin;
        config.trigger_edge = trigger.edge;
        config.trigger_timeout_ms = 2000;
        const esp_err_t err = logic_analyzer_service_start(&config);
        if (err != ESP_OK) {
            lv_label_set_text(_capture_status, "Start error");
            ESP_UTILS_LOGE("Capture failed: %s", esp_err_to_name(err));
            return;
        }
        lv_label_set_text(_run_label, "Stop");
        lv_label_set_text(_capture_status,
            trigger.edge == LOGIC_ANALYZER_TRIGGER_NONE
                ? "Capturing" : "Waiting trigger");
    }

    void updateState()
    {
        const logic_analyzer_state_t state = logic_analyzer_service_state();
        if (state == _previous) {
            return;
        }
        _previous = state;
        switch (state) {
        case LOGIC_ANALYZER_STATE_WAITING_TRIGGER:
            lv_label_set_text(_capture_status, "Waiting trigger");
            lv_label_set_text(_run_label, "Stop");
            break;
        case LOGIC_ANALYZER_STATE_CAPTURING:
            lv_label_set_text(_capture_status, "Capturing");
            lv_label_set_text(_run_label, "Stop");
            break;
        case LOGIC_ANALYZER_STATE_COMPLETE: {
            char rate[16];
            const uint32_t hz = logic_analyzer_service_sample_rate_hz();
            if (hz >= 1000000) {
                snprintf(rate, sizeof(rate), "%lu MHz",
                         static_cast<unsigned long>(hz / 1000000));
            } else {
                snprintf(rate, sizeof(rate), "%lu kHz",
                         static_cast<unsigned long>(hz / 1000));
            }
            lv_label_set_text_fmt(_capture_status, "%u @ %s",
                static_cast<unsigned>(logic_analyzer_service_sample_count()),
                rate);
            lv_label_set_text(_run_label, "Run");
            lv_slider_set_value(_offset, 0, LV_ANIM_OFF);
            updateWindow();
            break;
        }
        case LOGIC_ANALYZER_STATE_ERROR:
            lv_label_set_text(_capture_status, "Capture error");
            lv_label_set_text(_run_label, "Run");
            break;
        default:
            lv_label_set_text(_capture_status, "Ready");
            lv_label_set_text(_run_label, "Run");
            break;
        }
    }

    void drawWaveform(lv_event_t *event)
    {
        lv_layer_t *layer = lv_event_get_layer(event);
        lv_area_t area;
        lv_obj_get_coords(_waveform, &area);
        const int32_t left = area.x1 + 30;
        const int32_t right = area.x2 - 3;
        const int32_t width = right - left + 1;
        const int32_t band =
            lv_area_get_height(&area) / LOGIC_ANALYZER_CHANNEL_COUNT;

        for (int32_t i = 0; i <= 4; ++i) {
            const int32_t x = left + ((width - 1) * i) / 4;
            drawLine(layer, lv_color_hex(CONTROL),
                     x, area.y1 + 2, x, area.y2 - 2);
        }
        for (int32_t i = 1; i < LOGIC_ANALYZER_CHANNEL_COUNT; ++i) {
            const int32_t y = area.y1 + i * band;
            drawLine(layer, lv_color_hex(0x263038), left, y, right, y);
        }
        if (logic_analyzer_service_state() != LOGIC_ANALYZER_STATE_COMPLETE) {
            return;
        }

        const uint8_t *data = logic_analyzer_service_data();
        const size_t available = logic_analyzer_service_sample_count();
        size_t visible = viewCount();
        if (visible > available) visible = available;
        if (visible == 0) return;
        size_t offset = static_cast<size_t>(lv_slider_get_value(_offset));
        if (offset + visible > available) offset = available - visible;

        for (uint32_t channel = 0;
             channel < LOGIC_ANALYZER_CHANNEL_COUNT; ++channel) {
            const int32_t top = area.y1 + static_cast<int32_t>(channel) * band;
            const int32_t high = top + 5;
            const int32_t low = top + band - 5;
            int32_t previous =
                (data[offset] & (1U << channel)) ? high : low;

            for (int32_t pixel = 0; pixel < width; ++pixel) {
                const size_t first =
                    offset + static_cast<size_t>(pixel) * visible / width;
                size_t end =
                    offset + static_cast<size_t>(pixel + 1) * visible / width;
                if (end <= first) end = first + 1;
                if (end > offset + visible) end = offset + visible;
                bool has_high = false;
                bool has_low = false;
                for (size_t sample = first; sample < end; ++sample) {
                    if (data[sample] & (1U << channel)) has_high = true;
                    else has_low = true;
                }
                const int32_t x = left + pixel;
                const int32_t current =
                    (data[first] & (1U << channel)) ? high : low;
                if (pixel > 0) {
                    drawLine(layer, lv_color_hex(COLORS[channel]),
                             x - 1, previous, x, current);
                }
                if (has_high && has_low) {
                    drawLine(layer, lv_color_hex(COLORS[channel]),
                             x, high, x, low);
                }
                previous =
                    (data[end - 1] & (1U << channel)) ? high : low;
            }
        }
    }

    static ToolboxLogicAnalyzerApp *app(lv_event_t *event)
    {
        return static_cast<ToolboxLogicAnalyzerApp *>(
            lv_event_get_user_data(event));
    }
    static void runCallback(lv_event_t *event) { app(event)->startOrStop(); }
    static void viewCallback(lv_event_t *event) { app(event)->updateWindow(); }
    static void offsetCallback(lv_event_t *event)
    {
        lv_obj_invalidate(app(event)->_waveform);
    }
    static void drawCallback(lv_event_t *event) { app(event)->drawWaveform(event); }
    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxLogicAnalyzerApp *>(
            lv_timer_get_user_data(timer))->updateState();
    }

    lv_obj_t *_rate = nullptr;
    lv_obj_t *_trigger = nullptr;
    lv_obj_t *_run_label = nullptr;
    lv_obj_t *_waveform = nullptr;
    lv_obj_t *_capture_status = nullptr;
    lv_obj_t *_window_status = nullptr;
    lv_obj_t *_view = nullptr;
    lv_obj_t *_offset = nullptr;
    lv_timer_t *_timer = nullptr;
    logic_analyzer_state_t _previous = LOGIC_ANALYZER_STATE_ERROR;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    base::App, ToolboxLogicAnalyzerApp, "Logic Analyzer", []() {
        return std::shared_ptr<ToolboxLogicAnalyzerApp>(
            ToolboxLogicAnalyzerApp::requestInstance(),
            [](ToolboxLogicAnalyzerApp *) {});
    })

} // namespace esp_brookesia::apps
