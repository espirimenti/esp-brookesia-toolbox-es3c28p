// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <cstdio>
#include <memory>

#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "services/audio_service.h"
#include "services/morse_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "AudioTool"
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
constexpr uint32_t COLOR_TEAL = 0x54D1C1;
constexpr int32_t NAV_SAFE_BOTTOM = 36;
constexpr uint32_t MORSE_FREQUENCIES[] = {
    500, 600, 700, 800, 1000, 1200,
};
constexpr uint8_t MORSE_SPEEDS[] = {
    8, 12, 15, 20, 25, 30,
};

lv_obj_t *createButton(lv_obj_t *parent, const char *text,
                       int32_t width, int32_t height, uint32_t color,
                       lv_obj_t **label_out = nullptr)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (label_out != nullptr) {
        *label_out = label;
    }
    return button;
}

void styleControl(lv_obj_t *control)
{
    lv_obj_set_style_bg_color(control, lv_color_hex(COLOR_CONTROL), 0);
    lv_obj_set_style_text_color(control, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_width(control, 0, 0);
    lv_obj_set_style_radius(control, 4, 0);
}

void prepareTab(lv_obj_t *tab)
{
    lv_obj_set_style_bg_color(tab, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
}

} // namespace

class ToolboxAudioApp final : public phone::App {
public:
    static ToolboxAudioApp *requestInstance()
    {
        static ToolboxAudioApp instance;
        return &instance;
    }

protected:
    ToolboxAudioApp(): App("Audio Tool", nullptr, true, true, true) {}

    bool run() override
    {
        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(screen);

        const int32_t width = lv_obj_get_width(screen);
        const int32_t height =
            lv_obj_get_height(screen) - NAV_SAFE_BOTTOM;

        _tabs = lv_tabview_create(screen);
        lv_obj_set_size(_tabs, width, height);
        lv_obj_set_pos(_tabs, 0, 0);
        lv_tabview_set_tab_bar_position(_tabs, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(_tabs, 27);
        lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(_tabs),
                                  lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_text_color(lv_tabview_get_tab_bar(_tabs),
                                    lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(lv_tabview_get_content(_tabs),
                                  lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_pad_all(lv_tabview_get_content(_tabs), 0, 0);

        lv_obj_t *tone = lv_tabview_add_tab(_tabs, "Tone");
        lv_obj_t *morse = lv_tabview_add_tab(_tabs, "Morse");
        prepareTab(tone);
        prepareTab(morse);
        lv_obj_update_layout(_tabs);

        buildTone(tone);
        buildMorse(morse);

        _keyboard = lv_keyboard_create(screen);
        lv_obj_set_width(_keyboard, LV_PCT(100));
        lv_obj_set_height(_keyboard, 106);
        lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(_keyboard, _morse_input);
        lv_obj_add_event_cb(_keyboard, keyboardCallback,
                            LV_EVENT_ALL, this);
        lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);

        const esp_err_t result = audio_service_init();
        if (result != ESP_OK) {
            lv_label_set_text_fmt(_tone_status, "%s: %s",
                audio_service_error_stage(), esp_err_to_name(result));
            lv_obj_set_style_text_color(
                _tone_status, lv_color_hex(COLOR_RED), 0);
            lv_label_set_text(_morse_status, "Audio unavailable");
            disableControls();
            ESP_UTILS_LOGE("Audio init failed at %s: %s",
                           audio_service_error_stage(),
                           esp_err_to_name(result));
            return true;
        }

        applyToneSettings();
        morse_service_set_decoder(false, selectedMorseFrequency(),
                                  selectedMorseSpeed());
        _timer = lv_timer_create(timerCallback, 80, this);
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
        if (audio_service_state() == AUDIO_SERVICE_READY) {
            audio_service_stop_tone();
            audio_service_stop_morse();
            morse_service_set_decoder(false, 700, 15);
        }
        hideKeyboard();
        return true;
    }

    bool cleanResource() override
    {
        _tabs = nullptr;
        _timer = nullptr;
        _meter_bar = nullptr;
        _meter_level = nullptr;
        _meter_peak = nullptr;
        _frequency_slider = nullptr;
        _frequency_label = nullptr;
        _volume_slider = nullptr;
        _volume_label = nullptr;
        _waveform = nullptr;
        _tone_button = nullptr;
        _tone_button_label = nullptr;
        _tone_status = nullptr;
        _morse_input = nullptr;
        _morse_frequency = nullptr;
        _morse_speed = nullptr;
        _morse_tx_button = nullptr;
        _morse_tx_label = nullptr;
        _morse_rx_button = nullptr;
        _morse_rx_label = nullptr;
        _morse_status = nullptr;
        _morse_decoded = nullptr;
        _keyboard = nullptr;
        return true;
    }

private:
    uint32_t selectedFrequency() const
    {
        return static_cast<uint32_t>(
            lv_slider_get_value(_frequency_slider));
    }

    uint8_t selectedVolume() const
    {
        return static_cast<uint8_t>(
            lv_slider_get_value(_volume_slider));
    }

    audio_waveform_t selectedWaveform() const
    {
        return lv_dropdown_get_selected(_waveform) == 0
                   ? AUDIO_WAVE_SINE : AUDIO_WAVE_SQUARE;
    }

    uint32_t selectedMorseFrequency() const
    {
        const uint32_t index =
            lv_dropdown_get_selected(_morse_frequency);
        return index < sizeof(MORSE_FREQUENCIES) /
                           sizeof(MORSE_FREQUENCIES[0])
                   ? MORSE_FREQUENCIES[index] : 700;
    }

    uint8_t selectedMorseSpeed() const
    {
        const uint32_t index =
            lv_dropdown_get_selected(_morse_speed);
        return index < sizeof(MORSE_SPEEDS) / sizeof(MORSE_SPEEDS[0])
                   ? MORSE_SPEEDS[index] : 15;
    }

    void buildTone(lv_obj_t *tab)
    {
        const int32_t width = lv_obj_get_width(tab);

        lv_obj_t *meter = lv_obj_create(tab);
        lv_obj_set_size(meter, width - 8, 42);
        lv_obj_set_pos(meter, 4, 3);
        lv_obj_set_style_bg_color(meter, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_border_width(meter, 0, 0);
        lv_obj_set_style_radius(meter, 5, 0);
        lv_obj_set_style_pad_all(meter, 5, 0);
        lv_obj_clear_flag(meter, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *mic = lv_label_create(meter);
        lv_label_set_text(mic, "MIC");
        lv_obj_set_style_text_color(mic, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_align(mic, LV_ALIGN_TOP_LEFT, 0, 0);

        _meter_level = lv_label_create(meter);
        lv_label_set_text(_meter_level, "-96.0 dBFS");
        lv_obj_set_style_text_color(
            _meter_level, lv_color_hex(COLOR_TEAL), 0);
        lv_obj_align(_meter_level, LV_ALIGN_TOP_RIGHT, 0, 0);

        _meter_bar = lv_bar_create(meter);
        lv_obj_set_size(_meter_bar, width - 28, 7);
        lv_obj_align(_meter_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_bar_set_range(_meter_bar, -60, 0);
        lv_bar_set_value(_meter_bar, -60, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(
            _meter_bar, lv_color_hex(0x283238), LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            _meter_bar, lv_color_hex(COLOR_TEAL), LV_PART_INDICATOR);

        lv_obj_t *frequency_title = lv_label_create(tab);
        lv_label_set_text(frequency_title, "Frequency");
        lv_obj_set_style_text_color(
            frequency_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(frequency_title, 6, 49);

        _frequency_label = lv_label_create(tab);
        lv_label_set_text(_frequency_label, "1000 Hz");
        lv_obj_set_width(_frequency_label, 70);
        lv_obj_set_style_text_align(
            _frequency_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            _frequency_label, lv_color_hex(COLOR_TEAL), 0);
        lv_obj_set_pos(_frequency_label, 132, 49);

        _frequency_slider = lv_slider_create(tab);
        lv_obj_set_size(_frequency_slider, 196, 9);
        lv_obj_set_pos(_frequency_slider, 7, 69);
        lv_slider_set_range(_frequency_slider, 100, 5000);
        lv_slider_set_value(_frequency_slider, 1000, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(
            _frequency_slider, lv_color_hex(COLOR_TEAL),
            LV_PART_INDICATOR);
        lv_obj_add_event_cb(_frequency_slider, toneSettingsCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *volume_title = lv_label_create(tab);
        lv_label_set_text(volume_title, "Volume");
        lv_obj_set_style_text_color(
            volume_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_pos(volume_title, 6, 86);

        _volume_label = lv_label_create(tab);
        lv_label_set_text(_volume_label, "25%");
        lv_obj_set_width(_volume_label, 48);
        lv_obj_set_style_text_align(
            _volume_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(
            _volume_label, lv_color_hex(COLOR_AMBER), 0);
        lv_obj_set_pos(_volume_label, 154, 86);

        _volume_slider = lv_slider_create(tab);
        lv_obj_set_size(_volume_slider, 196, 9);
        lv_obj_set_pos(_volume_slider, 7, 106);
        lv_slider_set_range(_volume_slider, 0, 100);
        lv_slider_set_value(_volume_slider, 25, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(
            _volume_slider, lv_color_hex(COLOR_AMBER),
            LV_PART_INDICATOR);
        lv_obj_add_event_cb(_volume_slider, toneSettingsCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        _waveform = lv_dropdown_create(tab);
        lv_dropdown_set_options(_waveform, "Sine\nSquare");
        lv_obj_set_size(_waveform, 104, 31);
        lv_obj_set_pos(_waveform, width - 109, 49);
        styleControl(_waveform);
        lv_obj_add_event_cb(_waveform, toneSettingsCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        _tone_button = createButton(
            tab, "Start", 78, 31, 0x247A59, &_tone_button_label);
        lv_obj_set_pos(_tone_button, width - 83, 87);
        lv_obj_add_event_cb(_tone_button, toneButtonCallback,
                            LV_EVENT_CLICKED, this);

        _tone_status = lv_label_create(tab);
        lv_label_set_text(_tone_status, "Initializing audio...");
        lv_obj_set_width(_tone_status, width - 12);
        lv_label_set_long_mode(_tone_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(
            _tone_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_tone_status, 6, 132);

        _meter_peak = lv_label_create(tab);
        lv_label_set_text(_meter_peak, "Peak -96.0 dBFS");
        lv_obj_set_style_text_color(
            _meter_peak, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_align(_meter_peak, LV_ALIGN_BOTTOM_RIGHT, -6, -5);
    }

    void buildMorse(lv_obj_t *tab)
    {
        const int32_t width = lv_obj_get_width(tab);
        const int32_t height = lv_obj_get_height(tab);

        _morse_input = lv_textarea_create(tab);
        lv_textarea_set_one_line(_morse_input, true);
        lv_textarea_set_max_length(_morse_input, MORSE_TX_TEXT_CAPACITY);
        lv_textarea_set_placeholder_text(_morse_input, "Text to send");
        lv_obj_set_size(_morse_input, 198, 32);
        lv_obj_set_pos(_morse_input, 4, 3);
        lv_obj_add_event_cb(_morse_input, morseInputCallback,
                            LV_EVENT_FOCUSED, this);

        _morse_tx_button = createButton(
            tab, "TX", 52, 32, 0x247A59, &_morse_tx_label);
        lv_obj_set_pos(_morse_tx_button, 206, 3);
        lv_obj_add_event_cb(_morse_tx_button, morseTxCallback,
                            LV_EVENT_CLICKED, this);

        _morse_rx_button = createButton(
            tab, "RX", 54, 32, COLOR_CONTROL, &_morse_rx_label);
        lv_obj_set_pos(_morse_rx_button, 262, 3);
        lv_obj_add_event_cb(_morse_rx_button, morseRxCallback,
                            LV_EVENT_CLICKED, this);

        _morse_frequency = lv_dropdown_create(tab);
        lv_dropdown_set_options(
            _morse_frequency,
            "500 Hz\n600 Hz\n700 Hz\n800 Hz\n1000 Hz\n1200 Hz");
        lv_dropdown_set_selected(_morse_frequency, 2);
        lv_obj_set_size(_morse_frequency, 90, 31);
        lv_obj_set_pos(_morse_frequency, 4, 40);
        styleControl(_morse_frequency);
        lv_obj_add_event_cb(_morse_frequency, morseSettingsCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        _morse_speed = lv_dropdown_create(tab);
        lv_dropdown_set_options(
            _morse_speed,
            "8 WPM\n12 WPM\n15 WPM\n20 WPM\n25 WPM\n30 WPM");
        lv_dropdown_set_selected(_morse_speed, 2);
        lv_obj_set_size(_morse_speed, 86, 31);
        lv_obj_set_pos(_morse_speed, 98, 40);
        styleControl(_morse_speed);
        lv_obj_add_event_cb(_morse_speed, morseSettingsCallback,
                            LV_EVENT_VALUE_CHANGED, this);

        _morse_status = lv_label_create(tab);
        lv_label_set_text(_morse_status, "RX off");
        lv_obj_set_width(_morse_status, 88);
        lv_label_set_long_mode(_morse_status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(
            _morse_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_pos(_morse_status, 190, 48);

        lv_obj_t *clear = createButton(
            tab, LV_SYMBOL_TRASH, 32, 31, COLOR_CONTROL);
        lv_obj_set_pos(clear, width - 36, 40);
        lv_obj_add_event_cb(clear, morseClearCallback,
                            LV_EVENT_CLICKED, this);

        lv_obj_t *decoded_panel = lv_obj_create(tab);
        lv_obj_set_size(decoded_panel, width - 8, height - 79);
        lv_obj_set_pos(decoded_panel, 4, 75);
        lv_obj_set_style_radius(decoded_panel, 5, 0);
        lv_obj_set_style_border_width(decoded_panel, 0, 0);
        lv_obj_set_style_bg_color(
            decoded_panel, lv_color_hex(0x0B0E10), 0);
        lv_obj_set_style_pad_all(decoded_panel, 6, 0);
        lv_obj_clear_flag(decoded_panel, LV_OBJ_FLAG_SCROLLABLE);

        _morse_decoded = lv_label_create(decoded_panel);
        lv_obj_set_width(_morse_decoded, LV_PCT(100));
        lv_label_set_long_mode(_morse_decoded, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_morse_decoded, "Listening for Morse...");
        lv_obj_set_style_text_color(
            _morse_decoded, lv_color_hex(0xB8E6C9), 0);
        lv_obj_align(_morse_decoded, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    void applyToneSettings()
    {
        lv_label_set_text_fmt(_frequency_label, "%lu Hz",
                              (unsigned long)selectedFrequency());
        lv_label_set_text_fmt(_volume_label, "%u%%", selectedVolume());
        const esp_err_t result = audio_service_set_tone(
            selectedFrequency(), selectedVolume(), selectedWaveform());
        if (result != ESP_OK) {
            lv_label_set_text_fmt(_tone_status, "Settings: %s",
                                  esp_err_to_name(result));
        }
    }

    void toggleTone()
    {
        esp_err_t result;
        if (audio_service_tone_active()) {
            result = audio_service_stop_tone();
        } else {
            audio_service_stop_morse();
            result = audio_service_start_tone(
                selectedFrequency(), selectedVolume(), selectedWaveform());
        }
        if (result != ESP_OK) {
            lv_label_set_text_fmt(_tone_status, "Tone: %s",
                                  esp_err_to_name(result));
        }
    }

    void toggleMorseTx()
    {
        if (morse_service_tx_active()) {
            audio_service_stop_morse();
            return;
        }
        const char *text = lv_textarea_get_text(_morse_input);
        if (text == nullptr || text[0] == 0) {
            lv_label_set_text(_morse_status, "Enter text");
            return;
        }
        const esp_err_t result = audio_service_start_morse(
            text, selectedMorseFrequency(), selectedVolume(),
            selectedMorseSpeed());
        if (result != ESP_OK) {
            lv_label_set_text_fmt(_morse_status, "TX: %s",
                                  esp_err_to_name(result));
        }
        hideKeyboard();
    }

    void toggleMorseRx()
    {
        const bool enabled = !morse_service_decoder_enabled();
        const esp_err_t result = morse_service_set_decoder(
            enabled, selectedMorseFrequency(), selectedMorseSpeed());
        if (result != ESP_OK) {
            lv_label_set_text_fmt(_morse_status, "RX: %s",
                                  esp_err_to_name(result));
        }
    }

    void applyMorseSettings()
    {
        morse_service_set_decoder(
            morse_service_decoder_enabled(), selectedMorseFrequency(),
            selectedMorseSpeed());
    }

    void showKeyboard()
    {
        if (_keyboard == nullptr) {
            return;
        }
        lv_keyboard_set_textarea(_keyboard, _morse_input);
        lv_obj_clear_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_keyboard);
    }

    void hideKeyboard()
    {
        if (_keyboard != nullptr) {
            lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void update()
    {
        const float level = audio_service_level_dbfs();
        const float peak = audio_service_peak_dbfs();
        const int32_t meter =
            std::clamp<int32_t>(static_cast<int32_t>(level), -60, 0);
        lv_bar_set_value(_meter_bar, meter, LV_ANIM_OFF);
        lv_label_set_text_fmt(_meter_level, "%.1f dBFS", (double)level);
        lv_label_set_text_fmt(_meter_peak, "Peak %.1f dBFS", (double)peak);
        lv_label_set_text(_tone_button_label,
                          audio_service_tone_active() ? "Stop" : "Start");
        lv_label_set_text_fmt(
            _tone_status, "%s | MIC %u",
            audio_service_tone_active() ? "Tone active" : "Ready",
            audio_service_input_raw_peak());

        const bool tx = morse_service_tx_active();
        const bool rx = morse_service_decoder_enabled();
        lv_label_set_text(_morse_tx_label, tx ? "Stop" : "TX");
        lv_label_set_text(_morse_rx_label, rx ? "RX On" : "RX");
        if (rx) {
            lv_label_set_text_fmt(
                _morse_status, "%s %u%%",
                morse_service_signal_detected() ? "Signal" : "Listening",
                morse_service_signal_quality());
        } else if (tx) {
            lv_label_set_text(_morse_status, "Transmitting");
        } else {
            lv_label_set_text(_morse_status, "RX off");
        }

        char decoded[MORSE_RX_TEXT_CAPACITY];
        morse_service_get_decoded_text(decoded, sizeof(decoded));
        lv_label_set_text(_morse_decoded,
                          decoded[0] == 0
                              ? "Listening for Morse..." : decoded);
    }

    void disableControls()
    {
        lv_obj_t *controls[] = {
            _frequency_slider, _volume_slider, _waveform, _tone_button,
            _morse_input, _morse_frequency, _morse_speed,
            _morse_tx_button, _morse_rx_button,
        };
        for (lv_obj_t *control : controls) {
            if (control != nullptr) {
                lv_obj_add_state(control, LV_STATE_DISABLED);
            }
        }
    }

    static ToolboxAudioApp *app(lv_event_t *event)
    {
        return static_cast<ToolboxAudioApp *>(
            lv_event_get_user_data(event));
    }

    static void toneSettingsCallback(lv_event_t *event)
    {
        app(event)->applyToneSettings();
    }

    static void toneButtonCallback(lv_event_t *event)
    {
        app(event)->toggleTone();
    }

    static void morseInputCallback(lv_event_t *event)
    {
        app(event)->showKeyboard();
    }

    static void morseTxCallback(lv_event_t *event)
    {
        app(event)->toggleMorseTx();
    }

    static void morseRxCallback(lv_event_t *event)
    {
        app(event)->toggleMorseRx();
    }

    static void morseSettingsCallback(lv_event_t *event)
    {
        app(event)->applyMorseSettings();
    }

    static void morseClearCallback(lv_event_t *event)
    {
        auto *owner = app(event);
        morse_service_clear_decoded_text();
        lv_label_set_text(owner->_morse_decoded,
                          "Listening for Morse...");
    }

    static void keyboardCallback(lv_event_t *event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            app(event)->hideKeyboard();
        }
    }

    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxAudioApp *>(
            lv_timer_get_user_data(timer))->update();
    }

    lv_obj_t *_tabs = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_meter_bar = nullptr;
    lv_obj_t *_meter_level = nullptr;
    lv_obj_t *_meter_peak = nullptr;
    lv_obj_t *_frequency_slider = nullptr;
    lv_obj_t *_frequency_label = nullptr;
    lv_obj_t *_volume_slider = nullptr;
    lv_obj_t *_volume_label = nullptr;
    lv_obj_t *_waveform = nullptr;
    lv_obj_t *_tone_button = nullptr;
    lv_obj_t *_tone_button_label = nullptr;
    lv_obj_t *_tone_status = nullptr;
    lv_obj_t *_morse_input = nullptr;
    lv_obj_t *_morse_frequency = nullptr;
    lv_obj_t *_morse_speed = nullptr;
    lv_obj_t *_morse_tx_button = nullptr;
    lv_obj_t *_morse_tx_label = nullptr;
    lv_obj_t *_morse_rx_button = nullptr;
    lv_obj_t *_morse_rx_label = nullptr;
    lv_obj_t *_morse_status = nullptr;
    lv_obj_t *_morse_decoded = nullptr;
    lv_obj_t *_keyboard = nullptr;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    base::App, ToolboxAudioApp, "Audio Tool", []() {
        return std::shared_ptr<ToolboxAudioApp>(
            ToolboxAudioApp::requestInstance(),
            [](ToolboxAudioApp *) {});
    })

} // namespace esp_brookesia::apps