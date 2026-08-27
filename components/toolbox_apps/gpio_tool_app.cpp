// SPDX-License-Identifier: GPL-3.0-only

#include <memory>

#include "driver/gpio.h"
#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "services/gpio_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "GpioTool"
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
constexpr uint32_t COLOR_RED = 0x9A4545;
constexpr uint32_t COLOR_AMBER = 0xF6D365;
constexpr gpio_num_t GPIO_PINS[] = {
    GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_14, GPIO_NUM_21,
};
constexpr uint32_t PWM_FREQUENCIES[] = {
    1, 10, 100, 1000, 10000, 20000, 50000,
};
constexpr uint32_t PULSE_WIDTHS_US[] = {
    10, 100, 1000, 10000, 100000, 1000000,
};

gpio_service_mode_t modeFromIndex(uint32_t index)
{
    static constexpr gpio_service_mode_t modes[] = {
        GPIO_SERVICE_MODE_INPUT,
        GPIO_SERVICE_MODE_INPUT_PULL_UP,
        GPIO_SERVICE_MODE_INPUT_PULL_DOWN,
        GPIO_SERVICE_MODE_OUTPUT,
    };
    return index < 4 ? modes[index] : GPIO_SERVICE_MODE_INPUT;
}

uint32_t modeToIndex(gpio_service_mode_t mode)
{
    return static_cast<uint32_t>(mode);
}

void styleDropdown(lv_obj_t *dropdown)
{
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(COLOR_CONTROL), LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 4, LV_PART_MAIN);
}

lv_obj_t *createButton(lv_obj_t *parent, const char *text, int width,
                       uint32_t color)
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

} // namespace

class ToolboxGpioApp final : public phone::App {
public:
    static ToolboxGpioApp *requestInstance()
    {
        static ToolboxGpioApp instance;
        return &instance;
    }

protected:
    ToolboxGpioApp(): App("GPIO Tool", nullptr, true, true, true)
    {
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            _channels[i].owner = this;
            _channels[i].pin = GPIO_PINS[i];
        }
    }

    bool run() override
    {
        const esp_err_t result = gpio_service_init();
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("GPIO init failed: %s", esp_err_to_name(result));
            return false;
        }
        gpio_service_reset_all();

        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);

        _tabs = lv_tabview_create(screen);
        lv_obj_set_size(_tabs, LV_PCT(100), LV_PCT(100));
        lv_tabview_set_tab_bar_position(_tabs, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(_tabs, 28);
        lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(_tabs),
                                  lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_text_color(lv_tabview_get_tab_bar(_tabs),
                                    lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(lv_tabview_get_content(_tabs),
                                  lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_pad_all(lv_tabview_get_content(_tabs), 0, 0);
        lv_obj_add_event_cb(_tabs, tabCallback, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *monitor = lv_tabview_add_tab(_tabs, "Monitor");
        lv_obj_t *generator = lv_tabview_add_tab(_tabs, "Generator");
        prepareTab(monitor);
        prepareTab(generator);
        lv_obj_update_layout(_tabs);

        buildMonitor(monitor);
        buildGenerator(generator);

        _timer = lv_timer_create(timerCallback, 100, this);
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
        gpio_service_reset_all();
        return true;
    }

    bool cleanResource() override
    {
        _tabs = nullptr;
        _timer = nullptr;
        _generator_pin = nullptr;
        _frequency = nullptr;
        _duty = nullptr;
        _duty_label = nullptr;
        _pulse_width = nullptr;
        _generator_status = nullptr;
        for (auto &channel : _channels) {
            channel.indicator = nullptr;
            channel.level = nullptr;
            channel.mode = nullptr;
            channel.output = nullptr;
        }
        return true;
    }

private:
    static constexpr size_t CHANNEL_COUNT =
        sizeof(GPIO_PINS) / sizeof(GPIO_PINS[0]);

    struct Channel {
        ToolboxGpioApp *owner = nullptr;
        gpio_num_t pin = GPIO_NUM_NC;
        lv_obj_t *indicator = nullptr;
        lv_obj_t *level = nullptr;
        lv_obj_t *mode = nullptr;
        lv_obj_t *output = nullptr;
    };

    static void prepareTab(lv_obj_t *tab)
    {
        lv_obj_set_style_bg_color(tab, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_pad_all(tab, 0, 0);
        lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    }

    void buildMonitor(lv_obj_t *tab)
    {
        const int width = lv_obj_get_width(tab);
        const int height = lv_obj_get_height(tab);
        const int margin = 3;
        const int gap = 3;
        const int row_height = (height - margin * 2 - gap * 3) / 4;

        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            Channel &channel = _channels[i];
            lv_obj_t *row = lv_obj_create(tab);
            lv_obj_set_size(row, width - 6, row_height);
            lv_obj_set_pos(row, 3, margin + static_cast<int>(i) * (row_height + gap));
            lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_PANEL), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 4, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *pin = lv_label_create(row);
            lv_label_set_text_fmt(pin, "IO%d", channel.pin);
            lv_obj_set_style_text_color(pin, lv_color_hex(COLOR_TEXT), 0);
            lv_obj_align(pin, LV_ALIGN_LEFT_MID, 5, 0);

            channel.indicator = lv_obj_create(row);
            lv_obj_remove_style_all(channel.indicator);
            lv_obj_set_size(channel.indicator, 9, 9);
            lv_obj_set_style_radius(channel.indicator, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(channel.indicator, LV_OPA_COVER, 0);
            lv_obj_align(channel.indicator, LV_ALIGN_LEFT_MID, 40, 0);

            channel.level = lv_label_create(row);
            lv_obj_set_width(channel.level, 38);
            lv_obj_align(channel.level, LV_ALIGN_LEFT_MID, 54, 0);

            channel.mode = lv_dropdown_create(row);
            lv_dropdown_set_options(channel.mode, "IN\nUP\nDOWN\nOUT");
            lv_obj_set_size(channel.mode, 78, row_height - 4);
            lv_obj_align(channel.mode, LV_ALIGN_RIGHT_MID, -47, 0);
            styleDropdown(channel.mode);
            lv_obj_add_event_cb(channel.mode, modeCallback,
                                LV_EVENT_VALUE_CHANGED, &channel);

            channel.output = lv_switch_create(row);
            lv_obj_set_size(channel.output, 39, 22);
            lv_obj_align(channel.output, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_add_state(channel.output, LV_STATE_DISABLED);
            lv_obj_add_event_cb(channel.output, outputCallback,
                                LV_EVENT_VALUE_CHANGED, &channel);
            updateChannel(channel);
        }
    }

    void buildGenerator(lv_obj_t *tab)
    {
        const int width = lv_obj_get_width(tab);
        const int height = lv_obj_get_height(tab);

        _generator_pin = lv_dropdown_create(tab);
        lv_dropdown_set_options(_generator_pin, "GPIO2\nGPIO3\nGPIO14\nGPIO21");
        lv_obj_set_size(_generator_pin, 84, 31);
        lv_obj_set_pos(_generator_pin, 5, 4);
        styleDropdown(_generator_pin);

        _frequency = lv_dropdown_create(tab);
        lv_dropdown_set_options(_frequency,
            "1 Hz\n10 Hz\n100 Hz\n1 kHz\n10 kHz\n20 kHz\n50 kHz");
        lv_dropdown_set_selected(_frequency, 3);
        lv_obj_set_size(_frequency, 96, 31);
        lv_obj_set_pos(_frequency, width - 101, 4);
        styleDropdown(_frequency);

        lv_obj_t *duty_title = lv_label_create(tab);
        lv_label_set_text(duty_title, "Duty");
        lv_obj_set_style_text_color(duty_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(duty_title, 6, 40);

        _duty_label = lv_label_create(tab);
        lv_label_set_text(_duty_label, "50%");
        lv_obj_set_style_text_color(_duty_label, lv_color_hex(COLOR_AMBER), 0);
        lv_obj_align(_duty_label, LV_ALIGN_TOP_RIGHT, -6, 40);

        _duty = lv_slider_create(tab);
        lv_slider_set_range(_duty, 1, 100);
        lv_slider_set_value(_duty, 50, LV_ANIM_OFF);
        lv_obj_set_size(_duty, width - 28, 10);
        lv_obj_set_pos(_duty, 14, 61);
        lv_obj_set_style_bg_color(_duty, lv_color_hex(COLOR_GREEN),
                                  LV_PART_INDICATOR);
        lv_obj_add_event_cb(_duty, dutyCallback, LV_EVENT_VALUE_CHANGED, this);

        _pulse_width = lv_dropdown_create(tab);
        lv_dropdown_set_options(_pulse_width,
            "10 us\n100 us\n1 ms\n10 ms\n100 ms\n1 s");
        lv_dropdown_set_selected(_pulse_width, 2);
        lv_obj_set_size(_pulse_width, 94, 31);
        lv_obj_set_pos(_pulse_width, 5, 79);
        styleDropdown(_pulse_width);

        lv_obj_t *pulse = createButton(tab, "Pulse", 64, 0x69521D);
        lv_obj_set_pos(pulse, 105, 79);
        lv_obj_add_event_cb(pulse, pulseCallback, LV_EVENT_CLICKED, this);

        _generator_status = lv_label_create(tab);
        lv_label_set_text(_generator_status, "Stopped");
        lv_obj_set_width(_generator_status, 102);
        lv_label_set_long_mode(_generator_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(_generator_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_align(_generator_status, LV_ALIGN_BOTTOM_LEFT, 6, -8);

        lv_obj_t *pwm = createButton(tab, "PWM", 62, 0x247A59);
        lv_obj_set_pos(pwm, width - 136, height - 35);
        lv_obj_add_event_cb(pwm, pwmCallback, LV_EVENT_CLICKED, this);

        lv_obj_t *stop = createButton(tab, "Stop", 66, 0x633A3A);
        lv_obj_set_pos(stop, width - 70, height - 35);
        lv_obj_add_event_cb(stop, stopCallback, LV_EVENT_CLICKED, this);
    }

    static void timerCallback(lv_timer_t *timer)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_timer_get_user_data(timer));
        for (auto &channel : app->_channels) {
            app->updateChannel(channel);
        }
    }

    static void modeCallback(lv_event_t *event)
    {
        auto *channel = static_cast<Channel *>(lv_event_get_user_data(event));
        const gpio_service_mode_t mode =
            modeFromIndex(lv_dropdown_get_selected(channel->mode));
        const esp_err_t result = gpio_service_configure(channel->pin, mode);
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("GPIO%d mode failed: %s", channel->pin,
                           esp_err_to_name(result));
            return;
        }
        lv_obj_remove_state(channel->output, LV_STATE_CHECKED);
        if (mode == GPIO_SERVICE_MODE_OUTPUT) {
            lv_obj_clear_state(channel->output, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(channel->output, LV_STATE_DISABLED);
        }
        channel->owner->updateChannel(*channel);
    }

    static void outputCallback(lv_event_t *event)
    {
        auto *channel = static_cast<Channel *>(lv_event_get_user_data(event));
        const bool high = lv_obj_has_state(channel->output, LV_STATE_CHECKED);
        const esp_err_t result = gpio_service_write(channel->pin, high);
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("GPIO%d write failed: %s", channel->pin,
                           esp_err_to_name(result));
            return;
        }
        channel->owner->updateChannel(*channel);
    }

    static void dutyCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_event_get_user_data(event));
        lv_label_set_text_fmt(app->_duty_label, "%ld%%",
                              (long)lv_slider_get_value(app->_duty));
    }

    static void pwmCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_event_get_user_data(event));
        const uint32_t pin_index = lv_dropdown_get_selected(app->_generator_pin);
        const uint32_t frequency_index = lv_dropdown_get_selected(app->_frequency);
        const gpio_num_t pin = pin_index < CHANNEL_COUNT
                                   ? GPIO_PINS[pin_index] : GPIO_NUM_2;
        const uint32_t frequency =
            frequency_index < sizeof(PWM_FREQUENCIES) / sizeof(PWM_FREQUENCIES[0])
                ? PWM_FREQUENCIES[frequency_index] : 1000;
        const esp_err_t result = gpio_service_pwm_start(
            pin, frequency, static_cast<uint8_t>(lv_slider_get_value(app->_duty)));
        lv_label_set_text_fmt(app->_generator_status,
                              result == ESP_OK ? "IO%d PWM" : "PWM error", pin);
    }

    static void pulseCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_event_get_user_data(event));
        const uint32_t pin_index = lv_dropdown_get_selected(app->_generator_pin);
        const uint32_t width_index = lv_dropdown_get_selected(app->_pulse_width);
        const gpio_num_t pin = pin_index < CHANNEL_COUNT
                                   ? GPIO_PINS[pin_index] : GPIO_NUM_2;
        const uint32_t width =
            width_index < sizeof(PULSE_WIDTHS_US) / sizeof(PULSE_WIDTHS_US[0])
                ? PULSE_WIDTHS_US[width_index] : 1000;
        const esp_err_t result = gpio_service_pulse(pin, width);
        lv_label_set_text_fmt(app->_generator_status,
                              result == ESP_OK ? "IO%d pulse" : "Pulse error", pin);
    }

    static void stopCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_event_get_user_data(event));
        const esp_err_t result = gpio_service_generator_stop();
        lv_label_set_text(app->_generator_status,
                          result == ESP_OK ? "Stopped" : "Stop error");
    }

    static void tabCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxGpioApp *>(lv_event_get_user_data(event));
        if (lv_tabview_get_tab_active(app->_tabs) == 0) {
            gpio_service_generator_stop();
            lv_label_set_text(app->_generator_status, "Stopped");
            app->syncModes();
        }
    }

    void updateChannel(Channel &channel)
    {
        bool high = false;
        if (gpio_service_read(channel.pin, &high) != ESP_OK) {
            return;
        }
        lv_label_set_text(channel.level, high ? "HIGH" : "LOW");
        lv_obj_set_style_text_color(channel.level,
            lv_color_hex(high ? COLOR_GREEN : COLOR_RED), 0);
        lv_obj_set_style_bg_color(channel.indicator,
            lv_color_hex(high ? COLOR_GREEN : COLOR_RED), 0);
    }

    void syncModes()
    {
        for (auto &channel : _channels) {
            gpio_service_mode_t mode = GPIO_SERVICE_MODE_INPUT;
            gpio_service_get_mode(channel.pin, &mode);
            lv_dropdown_set_selected(channel.mode, modeToIndex(mode));
            lv_obj_remove_state(channel.output, LV_STATE_CHECKED);
            if (mode == GPIO_SERVICE_MODE_OUTPUT) {
                lv_obj_clear_state(channel.output, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(channel.output, LV_STATE_DISABLED);
            }
            updateChannel(channel);
        }
    }

    Channel _channels[CHANNEL_COUNT]{};
    lv_obj_t *_tabs = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_generator_pin = nullptr;
    lv_obj_t *_frequency = nullptr;
    lv_obj_t *_duty = nullptr;
    lv_obj_t *_duty_label = nullptr;
    lv_obj_t *_pulse_width = nullptr;
    lv_obj_t *_generator_status = nullptr;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(base::App, ToolboxGpioApp, "GPIO Tool", []() {
    return std::shared_ptr<ToolboxGpioApp>(ToolboxGpioApp::requestInstance(),
                                           [](ToolboxGpioApp *) {});
})

} // namespace esp_brookesia::apps
