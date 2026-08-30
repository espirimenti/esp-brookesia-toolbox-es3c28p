// SPDX-License-Identifier: GPL-3.0-only

#include <ctype.h>
#include <memory>
#include <stdio.h>
#include <string.h>

#include "esp_brookesia.hpp"
#include "esp_err.h"
#include "lvgl.h"
#include "assets/toolbox_icons.h"
#include "services/uart_service.h"

#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "UartMonitor"
#include "esp_lib_utils.h"

using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x101518;
constexpr uint32_t COLOR_PANEL = 0x1B2227;
constexpr uint32_t COLOR_TERMINAL = 0x090D0F;
constexpr uint32_t COLOR_TEXT = 0xF2F5F7;
constexpr uint32_t COLOR_MUTED = 0x9EABB3;
constexpr uint32_t COLOR_GREEN = 0x49B982;
constexpr uint32_t COLOR_BUTTON = 0x2B353C;
constexpr uint32_t COLOR_ACTIVE = 0x69521D;

constexpr size_t RX_HISTORY_CAPACITY = 1024;
constexpr size_t RENDER_CAPACITY = RX_HISTORY_CAPACITY * 3 + 1;
constexpr size_t TX_CAPACITY = 256;
constexpr uint32_t BAUD_RATES[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600,
};

uint32_t baudFromIndex(uint32_t index)
{
    return index < sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0])
               ? BAUD_RATES[index]
               : 115200;
}

uint32_t baudToIndex(uint32_t baud)
{
    for (size_t i = 0; i < sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]); ++i) {
        if (BAUD_RATES[i] == baud) {
            return static_cast<uint32_t>(i);
        }
    }
    return 4;
}

int hexNibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = static_cast<char>(toupper(static_cast<unsigned char>(value)));
    return value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
}

bool parseHex(const char *text, uint8_t *output, size_t &length)
{
    length = 0;
    while (*text != '\0') {
        while (*text == ' ' || *text == '\t' || *text == ',' ||
               *text == ':' || *text == '-') {
            ++text;
        }
        if (*text == '\0') {
            break;
        }
        if (length >= TX_CAPACITY) {
            return false;
        }
        const int high = hexNibble(*text++);
        if (high < 0 || *text == '\0') {
            return false;
        }
        const int low = hexNibble(*text++);
        if (low < 0) {
            return false;
        }
        output[length++] = static_cast<uint8_t>((high << 4) | low);
    }
    return length > 0;
}

void styleButton(lv_obj_t *button, uint32_t color = COLOR_BUTTON)
{
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 2, 0);
}

} // namespace

class ToolboxUartApp final : public phone::App {
public:
    static ToolboxUartApp *requestInstance()
    {
        static ToolboxUartApp instance;
        return &instance;
    }

protected:
    ToolboxUartApp(): App("UART Monitor", &toolbox_icon_uart_monitor, true, true, true) {}

    bool run() override
    {
        const esp_err_t result = uart_service_init();
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("UART init failed: %s", esp_err_to_name(result));
            return false;
        }

        _paused = false;
        _hex_mode = false;
        _rx_length = 0;
        _rx_total = 0;
        _tx_total = 0;

        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(screen, 6, 0);

        _baud = lv_dropdown_create(screen);
        lv_dropdown_set_options(_baud,
            "9600\n19200\n38400\n57600\n115200\n230400\n460800\n921600");
        lv_dropdown_set_selected(_baud, baudToIndex(uart_service_get_baud()));
        lv_obj_set_size(_baud, 74, 30);
        lv_obj_align(_baud, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_add_event_cb(_baud, baudCallback, LV_EVENT_VALUE_CHANGED, this);

        _mode = lv_dropdown_create(screen);
        lv_dropdown_set_options(_mode, "ASCII\nHEX");
        lv_obj_set_size(_mode, 58, 30);
        lv_obj_align_to(_mode, _baud, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        lv_obj_add_event_cb(_mode, modeCallback, LV_EVENT_VALUE_CHANGED, this);

        _pause_button = lv_button_create(screen);
        lv_obj_set_size(_pause_button, 32, 30);
        lv_obj_align_to(_pause_button, _mode, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        styleButton(_pause_button);
        lv_obj_add_event_cb(_pause_button, pauseCallback, LV_EVENT_CLICKED, this);
        _pause_label = lv_label_create(_pause_button);
        lv_label_set_text(_pause_label, LV_SYMBOL_PAUSE);
        lv_obj_center(_pause_label);

        lv_obj_t *clear_button = lv_button_create(screen);
        lv_obj_set_size(clear_button, 32, 30);
        lv_obj_align_to(clear_button, _pause_button, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        styleButton(clear_button);
        lv_obj_add_event_cb(clear_button, clearCallback, LV_EVENT_CLICKED, this);
        lv_obj_t *clear_label = lv_label_create(clear_button);
        lv_label_set_text(clear_label, LV_SYMBOL_TRASH);
        lv_obj_center(clear_label);

        _status = lv_label_create(screen);
        lv_label_set_text(_status, "RX 0  TX 0");
        lv_obj_set_width(_status, 91);
        lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(_status, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_align(_status, LV_ALIGN_TOP_RIGHT, 0, 8);

        _terminal = lv_obj_create(screen);
        lv_obj_set_size(_terminal, LV_PCT(100), 87);
        lv_obj_align(_terminal, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_set_style_radius(_terminal, 4, 0);
        lv_obj_set_style_bg_color(_terminal, lv_color_hex(COLOR_TERMINAL), 0);
        lv_obj_set_style_bg_opa(_terminal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_terminal, 0, 0);
        lv_obj_set_style_pad_all(_terminal, 5, 0);

        _terminal_label = lv_label_create(_terminal);
        lv_obj_set_width(_terminal_label, LV_PCT(100));
        lv_label_set_long_mode(_terminal_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(_terminal_label, lv_color_hex(0xB8E6C9), 0);
        lv_label_set_text(_terminal_label, "Waiting for RX...");
        lv_obj_align(_terminal_label, LV_ALIGN_TOP_LEFT, 0, 0);

        _tx_row = lv_obj_create(screen);
        lv_obj_remove_style_all(_tx_row);
        lv_obj_set_size(_tx_row, LV_PCT(100), 34);
        lv_obj_align(_tx_row, LV_ALIGN_BOTTOM_MID, 0, 0);

        _tx_input = lv_textarea_create(_tx_row);
        lv_textarea_set_one_line(_tx_input, true);
        lv_textarea_set_max_length(_tx_input, TX_CAPACITY);
        lv_textarea_set_placeholder_text(_tx_input, "Text to send");
        lv_obj_set_size(_tx_input, 244, 34);
        lv_obj_align(_tx_input, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(_tx_input, inputCallback, LV_EVENT_FOCUSED, this);

        lv_obj_t *send_button = lv_button_create(_tx_row);
        lv_obj_set_size(send_button, 56, 34);
        lv_obj_align(send_button, LV_ALIGN_RIGHT_MID, 0, 0);
        styleButton(send_button, 0x247A59);
        lv_obj_add_event_cb(send_button, sendCallback, LV_EVENT_CLICKED, this);
        lv_obj_t *send_label = lv_label_create(send_button);
        lv_label_set_text(send_label, "Send");
        lv_obj_center(send_label);

        _keyboard = lv_keyboard_create(screen);
        lv_obj_set_size(_keyboard, LV_PCT(100), 88);
        lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(_keyboard, _tx_input);
        lv_obj_add_event_cb(_keyboard, keyboardCallback, LV_EVENT_ALL, this);
        lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);

        _timer = lv_timer_create(timerCallback, 30, this);
        return _timer != nullptr;
    }

    bool back() override
    {
        if (_keyboard != nullptr && !lv_obj_has_flag(_keyboard, LV_OBJ_FLAG_HIDDEN)) {
            hideKeyboard();
            return true;
        }
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
        _baud = nullptr;
        _mode = nullptr;
        _pause_button = nullptr;
        _pause_label = nullptr;
        _status = nullptr;
        _terminal = nullptr;
        _terminal_label = nullptr;
        _tx_row = nullptr;
        _tx_input = nullptr;
        _keyboard = nullptr;
        _timer = nullptr;
        return true;
    }

private:
    static void timerCallback(lv_timer_t *timer)
    {
        static_cast<ToolboxUartApp *>(lv_timer_get_user_data(timer))->pollRx();
    }

    static void baudCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        const uint32_t baud = baudFromIndex(lv_dropdown_get_selected(app->_baud));
        const esp_err_t result = uart_service_set_baud(baud);
        if (result != ESP_OK) {
            ESP_UTILS_LOGE("Cannot set baud: %s", esp_err_to_name(result));
        }
    }

    static void modeCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        app->_hex_mode = lv_dropdown_get_selected(app->_mode) == 1;
        lv_textarea_set_accepted_chars(app->_tx_input,
            app->_hex_mode ? "0123456789abcdefABCDEF ,:-" : nullptr);
        lv_textarea_set_placeholder_text(app->_tx_input,
            app->_hex_mode ? "AA 55 0D 0A" : "Text to send");
        app->render();
    }

    static void pauseCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        app->_paused = !app->_paused;
        lv_label_set_text(app->_pause_label,
                          app->_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
        lv_obj_set_style_bg_color(app->_pause_button,
            lv_color_hex(app->_paused ? COLOR_ACTIVE : COLOR_BUTTON), 0);
        if (!app->_paused) {
            app->render();
        }
    }

    static void clearCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        app->_rx_length = 0;
        app->_rx_total = 0;
        uart_service_clear_rx();
        app->render();
        app->updateStatus();
    }

    static void sendCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        app->send();
        app->hideKeyboard();
    }

    static void inputCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        app->showKeyboard();
    }

    static void keyboardCallback(lv_event_t *event)
    {
        auto *app = static_cast<ToolboxUartApp *>(lv_event_get_user_data(event));
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_READY) {
            app->send();
            app->hideKeyboard();
        } else if (code == LV_EVENT_CANCEL) {
            app->hideKeyboard();
        }
    }

    void showKeyboard()
    {
        lv_obj_add_flag(_terminal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(_tx_row, LV_ALIGN_BOTTOM_MID, 0, -92);
        lv_obj_move_foreground(_keyboard);
    }

    void hideKeyboard()
    {
        lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_terminal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(_tx_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    void pollRx()
    {
        uint8_t data[128];
        size_t bytes_read = 0;
        const esp_err_t result = uart_service_read(data, sizeof(data), &bytes_read);
        if (result != ESP_OK || bytes_read == 0) {
            return;
        }
        append(data, bytes_read);
        _rx_total += bytes_read;
        updateStatus();
        if (!_paused) {
            render();
        }
    }

    void append(const uint8_t *data, size_t length)
    {
        if (length >= RX_HISTORY_CAPACITY) {
            memcpy(_rx_history, data + length - RX_HISTORY_CAPACITY,
                   RX_HISTORY_CAPACITY);
            _rx_length = RX_HISTORY_CAPACITY;
            return;
        }
        const size_t overflow =
            _rx_length + length > RX_HISTORY_CAPACITY
                ? _rx_length + length - RX_HISTORY_CAPACITY
                : 0;
        if (overflow > 0) {
            memmove(_rx_history, _rx_history + overflow, _rx_length - overflow);
            _rx_length -= overflow;
        }
        memcpy(_rx_history + _rx_length, data, length);
        _rx_length += length;
    }

    void render()
    {
        if (_rx_length == 0) {
            lv_label_set_text(_terminal_label, "Waiting for RX...");
            return;
        }

        size_t output = 0;
        if (_hex_mode) {
            for (size_t i = 0; i < _rx_length && output + 4 < RENDER_CAPACITY; ++i) {
                const int written = snprintf(_render + output,
                    RENDER_CAPACITY - output, "%02X%s", _rx_history[i],
                    ((i + 1) % 16) == 0 ? "\n" : " ");
                if (written <= 0) {
                    break;
                }
                output += static_cast<size_t>(written);
            }
        } else {
            for (size_t i = 0; i < _rx_length && output + 1 < RENDER_CAPACITY; ++i) {
                const uint8_t value = _rx_history[i];
                if (value == '\r') {
                    if (i + 1 < _rx_length && _rx_history[i + 1] == '\n') {
                        continue;
                    }
                    _render[output++] = '\n';
                } else if (value == '\n' || value == '\t' ||
                           (value >= 32 && value <= 126)) {
                    _render[output++] = static_cast<char>(value);
                } else {
                    _render[output++] = '.';
                }
            }
        }
        _render[output] = '\0';
        lv_label_set_text(_terminal_label, _render);
        lv_obj_update_layout(_terminal);
        lv_obj_scroll_to_y(_terminal, LV_COORD_MAX, LV_ANIM_OFF);
    }

    void send()
    {
        const char *text = lv_textarea_get_text(_tx_input);
        if (text == nullptr || text[0] == '\0') {
            return;
        }

        uint8_t hex_data[TX_CAPACITY];
        const uint8_t *data = reinterpret_cast<const uint8_t *>(text);
        size_t length = strlen(text);
        if (_hex_mode) {
            if (!parseHex(text, hex_data, length)) {
                lv_textarea_set_text(_tx_input, "");
                lv_textarea_set_placeholder_text(_tx_input, "Invalid HEX");
                return;
            }
            data = hex_data;
        }

        size_t written = 0;
        const esp_err_t result = uart_service_write(data, length, &written);
        if (result != ESP_OK) {
            lv_textarea_set_placeholder_text(_tx_input, "TX failed");
            return;
        }
        _tx_total += written;
        updateStatus();
        lv_textarea_set_text(_tx_input, "");
        lv_textarea_set_placeholder_text(_tx_input,
            _hex_mode ? "AA 55 0D 0A" : "Text to send");
    }

    void updateStatus()
    {
        lv_label_set_text_fmt(_status, "RX %u  TX %u",
            static_cast<unsigned>(_rx_total),
            static_cast<unsigned>(_tx_total));
    }

    lv_obj_t *_baud = nullptr;
    lv_obj_t *_mode = nullptr;
    lv_obj_t *_pause_button = nullptr;
    lv_obj_t *_pause_label = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_terminal = nullptr;
    lv_obj_t *_terminal_label = nullptr;
    lv_obj_t *_tx_row = nullptr;
    lv_obj_t *_tx_input = nullptr;
    lv_obj_t *_keyboard = nullptr;
    lv_timer_t *_timer = nullptr;
    uint8_t _rx_history[RX_HISTORY_CAPACITY]{};
    char _render[RENDER_CAPACITY]{};
    size_t _rx_length = 0;
    size_t _rx_total = 0;
    size_t _tx_total = 0;
    bool _paused = false;
    bool _hex_mode = false;
};

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(base::App, ToolboxUartApp, "UART Monitor", []() {
    return std::shared_ptr<ToolboxUartApp>(ToolboxUartApp::requestInstance(),
                                           [](ToolboxUartApp *) {});
})

} // namespace esp_brookesia::apps
