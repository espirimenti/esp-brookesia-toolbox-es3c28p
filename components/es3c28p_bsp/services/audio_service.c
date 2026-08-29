// SPDX-License-Identifier: GPL-3.0-only

#include "services/audio_service.h"

#include "board/board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/i2c_service.h"
#include "services/morse_service.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_FRAME_COUNT 256
#define AUDIO_TASK_STACK 8192
#define AUDIO_MIN_FREQUENCY_HZ 50
#define AUDIO_MAX_FREQUENCY_HZ 10000
#define ES8311_I2C_ADDRESS 0x18
#define ES8311_I2C_FREQUENCY_HZ 400000

typedef struct {
    uint8_t reg;
    uint8_t value;
} codec_register_t;

static const char *TAG = "audio_service";
static volatile audio_service_state_t service_state =
    AUDIO_SERVICE_UNINITIALIZED;
static volatile esp_err_t last_error;
static const char *error_stage = "not started";
static i2s_chan_handle_t tx_channel;
static i2s_chan_handle_t rx_channel;
static i2c_master_dev_handle_t codec_device;
static volatile bool tone_active;
static volatile uint32_t tone_frequency_hz = 1000;
static volatile uint8_t tone_volume_percent = 25;
static volatile audio_waveform_t tone_waveform = AUDIO_WAVE_SINE;
static volatile float level_dbfs = -96.0f;
static volatile float peak_dbfs = -96.0f;
static volatile uint16_t input_raw_peak;

static esp_err_t set_error(const char *stage, esp_err_t error)
{
    error_stage = stage;
    last_error = error;
    service_state = AUDIO_SERVICE_ERROR;
    ESP_LOGE(TAG, "%s failed: %s", stage, esp_err_to_name(error));
    return error;
}

static esp_err_t codec_write(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(
        codec_device, command, sizeof(command), 100);
}

static esp_err_t codec_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(
        codec_device, &reg, 1, value, 1, 100);
}

static void codec_verify(uint8_t reg, uint8_t expected)
{
    uint8_t actual = 0;
    const esp_err_t result = codec_read(reg, &actual);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 register 0x%02X read failed: %s",
                 reg, esp_err_to_name(result));
    } else if (actual != expected) {
        ESP_LOGW(TAG,
                 "ES8311 register 0x%02X: expected 0x%02X, read 0x%02X",
                 reg, expected, actual);
    }
}

static esp_err_t codec_write_sequence(void)
{
    esp_err_t result = codec_write(0x00, 0x1F);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    static const codec_register_t registers[] = {
        {0x00, 0x00}, {0x00, 0x80}, {0x01, 0x3F}, {0x02, 0x00},
        {0x03, 0x10}, {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03},
        {0x07, 0x00}, {0x08, 0xFF}, {0x09, 0x0C}, {0x0A, 0x0C},
        {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00}, {0x13, 0x10},
        {0x14, 0x1A}, {0x16, 0x04}, {0x17, 0xC8}, {0x1C, 0x6A},
        {0x37, 0x08}, {0x31, 0x00}, {0x32, 0xFF},
    };
    for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        result = codec_write(registers[i].reg, registers[i].value);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "ES8311 register 0x%02X write failed",
                     registers[i].reg);
            return result;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    static const codec_register_t verification[] = {
        {0x00, 0x80}, {0x01, 0x3F}, {0x02, 0x00}, {0x09, 0x0C},
        {0x0A, 0x0C}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00},
        {0x13, 0x10}, {0x14, 0x1A}, {0x16, 0x04}, {0x17, 0xC8},
        {0x31, 0x00}, {0x32, 0xFF}, {0x37, 0x08},
    };
    for (size_t i = 0;
         i < sizeof(verification) / sizeof(verification[0]); ++i) {
        codec_verify(verification[i].reg, verification[i].value);
    }
    return ESP_OK;
}

static void update_input_level(const int16_t *samples, size_t count)
{
    if (samples == NULL || count == 0) {
        return;
    }

    double square_sum = 0.0;
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t value = samples[i];
        const int32_t magnitude = value < 0 ? -value : value;
        square_sum += (double)value * value;
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    input_raw_peak = (uint16_t)peak;

    const float rms = sqrtf((float)(square_sum / count));
    const float measured_level =
        rms > 1.0f ? 20.0f * log10f(rms / 32768.0f) : -96.0f;
    const float measured_peak =
        peak > 0 ? 20.0f * log10f((float)peak / 32768.0f) : -96.0f;
    level_dbfs = level_dbfs * 0.78f + measured_level * 0.22f;
    const float decayed_peak = peak_dbfs - 0.8f;
    peak_dbfs = measured_peak > decayed_peak ? measured_peak : decayed_peak;
    if (peak_dbfs < -96.0f) {
        peak_dbfs = -96.0f;
    }
}

static void render_tone(int16_t *samples, size_t frames, float *phase)
{
    const bool active = tone_active;
    const uint32_t frequency = tone_frequency_hz;
    const uint8_t volume = tone_volume_percent;
    const audio_waveform_t waveform = tone_waveform;
    const float amplitude =
        32767.0f * 0.45f * ((float)volume / 100.0f);
    const float phase_step =
        (float)frequency / (float)AUDIO_SAMPLE_RATE;

    for (size_t frame = 0; frame < frames; ++frame) {
        int16_t sample = 0;
        if (active) {
            const float wave =
                waveform == AUDIO_WAVE_SQUARE
                    ? (*phase < 0.5f ? 1.0f : -1.0f)
                    : sinf(*phase * 2.0f * (float)M_PI);
            sample = (int16_t)(wave * amplitude);
            *phase += phase_step;
            if (*phase >= 1.0f) {
                *phase -= floorf(*phase);
            }
        }
        samples[frame * 2] = sample;
        samples[frame * 2 + 1] = sample;
    }
}

static void audio_task(void *argument)
{
    (void)argument;
    int16_t tx_samples[AUDIO_FRAME_COUNT * 2];
    int16_t rx_samples[AUDIO_FRAME_COUNT * 2];
    float phase = 0.0f;

    while (true) {
        const bool morse_was_active = morse_service_tx_active();
        if (morse_was_active) {
            morse_service_render(tx_samples, AUDIO_FRAME_COUNT);
        } else {
            render_tone(tx_samples, AUDIO_FRAME_COUNT, &phase);
        }
        if (morse_was_active && !morse_service_tx_active()) {
            gpio_set_level(board_pins()->audio_enable, 1);
        }

        size_t bytes_written = 0;
        const esp_err_t write_result = i2s_channel_write(
            tx_channel, tx_samples, sizeof(tx_samples), &bytes_written,
            pdMS_TO_TICKS(50));
        if (write_result != ESP_OK) {
            ESP_LOGW(TAG, "I2S write failed: %s",
                     esp_err_to_name(write_result));
        }

        size_t bytes_read = 0;
        const esp_err_t read_result = i2s_channel_read(
            rx_channel, rx_samples, sizeof(rx_samples), &bytes_read,
            pdMS_TO_TICKS(50));
        if (read_result == ESP_OK && bytes_read > 0) {
            const size_t sample_count = bytes_read / sizeof(rx_samples[0]);
            update_input_level(rx_samples, sample_count);
            morse_service_process_input(rx_samples, sample_count / 2);
        } else if (read_result != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read failed: %s",
                     esp_err_to_name(read_result));
        }
    }
}

esp_err_t audio_service_init(void)
{
    if (service_state == AUDIO_SERVICE_READY) {
        return ESP_OK;
    }
    if (service_state == AUDIO_SERVICE_ERROR) {
        return last_error;
    }

    esp_err_t result = i2c_service_probe(ES8311_I2C_ADDRESS, 100);
    if (result != ESP_OK) {
        return set_error("ES8311 probe", result);
    }
    result = i2c_service_add_device(
        ES8311_I2C_ADDRESS, ES8311_I2C_FREQUENCY_HZ, &codec_device);
    if (result != ESP_OK) {
        return set_error("ES8311 I2C", result);
    }

    const board_pins_t *pins = board_pins();
    const gpio_config_t amplifier_config = {
        .pin_bit_mask = 1ULL << pins->audio_enable,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&amplifier_config);
    if (result == ESP_OK) {
        result = gpio_set_level(pins->audio_enable, 1);
    }
    if (result != ESP_OK) {
        return set_error("amplifier GPIO", result);
    }

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    result = i2s_new_channel(&channel_config, &tx_channel, &rx_channel);
    if (result != ESP_OK) {
        return set_error("I2S channels", result);
    }

    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = pins->i2s_mclk,
            .bclk = pins->i2s_bclk,
            .ws = pins->i2s_lrck,
            .dout = pins->i2s_dout,
            .din = pins->i2s_din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    result = i2s_channel_init_std_mode(tx_channel, &i2s_config);
    if (result == ESP_OK) {
        result = i2s_channel_init_std_mode(rx_channel, &i2s_config);
    }
    if (result == ESP_OK) {
        result = i2s_channel_enable(tx_channel);
    }
    if (result == ESP_OK) {
        result = i2s_channel_enable(rx_channel);
    }
    if (result != ESP_OK) {
        return set_error("I2S start", result);
    }

    result = codec_write_sequence();
    if (result != ESP_OK) {
        return set_error("ES8311 registers", result);
    }

    if (xTaskCreate(audio_task, "audio_io", AUDIO_TASK_STACK, NULL, 7, NULL)
        != pdPASS) {
        return set_error("audio task", ESP_ERR_NO_MEM);
    }

    error_stage = "ready";
    last_error = ESP_OK;
    service_state = AUDIO_SERVICE_READY;
    ESP_LOGI(TAG,
             "ES8311 audio ready at 48 kHz, MCLK, TX=GPIO8 RX=GPIO6");
    return ESP_OK;
}

audio_service_state_t audio_service_state(void)
{
    return service_state;
}

esp_err_t audio_service_last_error(void)
{
    return last_error;
}

const char *audio_service_error_stage(void)
{
    return error_stage;
}

esp_err_t audio_service_start_tone(uint32_t frequency_hz,
                                   uint8_t volume_percent,
                                   audio_waveform_t waveform)
{
    ESP_RETURN_ON_FALSE(service_state == AUDIO_SERVICE_READY,
                        ESP_ERR_INVALID_STATE, TAG, "Audio is not ready");
    ESP_RETURN_ON_FALSE(
        frequency_hz >= AUDIO_MIN_FREQUENCY_HZ &&
            frequency_hz <= AUDIO_MAX_FREQUENCY_HZ &&
            volume_percent <= 100 && waveform <= AUDIO_WAVE_SQUARE,
        ESP_ERR_INVALID_ARG, TAG, "Invalid tone settings");

    tone_frequency_hz = frequency_hz;
    tone_volume_percent = volume_percent;
    tone_waveform = waveform;
    morse_service_stop_tx();

    esp_err_t result = codec_write(0x31, 0x00);
    if (result == ESP_OK) {
        result = codec_write(0x32, 0xFF);
    }
    if (result == ESP_OK) {
        result = gpio_set_level(board_pins()->audio_enable, 0);
    }
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    tone_active = true;
    return ESP_OK;
}

esp_err_t audio_service_stop_tone(void)
{
    if (service_state != AUDIO_SERVICE_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    tone_active = false;
    return gpio_set_level(board_pins()->audio_enable, 1);
}

esp_err_t audio_service_set_tone(uint32_t frequency_hz,
                                 uint8_t volume_percent,
                                 audio_waveform_t waveform)
{
    if (frequency_hz < AUDIO_MIN_FREQUENCY_HZ ||
        frequency_hz > AUDIO_MAX_FREQUENCY_HZ ||
        volume_percent > 100 || waveform > AUDIO_WAVE_SQUARE) {
        return ESP_ERR_INVALID_ARG;
    }
    tone_frequency_hz = frequency_hz;
    tone_volume_percent = volume_percent;
    tone_waveform = waveform;
    return ESP_OK;
}

bool audio_service_tone_active(void)
{
    return tone_active;
}

esp_err_t audio_service_start_morse(const char *text,
                                    uint32_t frequency_hz,
                                    uint8_t volume_percent,
                                    uint8_t wpm)
{
    ESP_RETURN_ON_FALSE(service_state == AUDIO_SERVICE_READY,
                        ESP_ERR_INVALID_STATE, TAG, "Audio is not ready");
    tone_active = false;
    esp_err_t result = morse_service_start_tx(
        text, frequency_hz, volume_percent, wpm);
    if (result == ESP_OK) {
        result = codec_write(0x31, 0x00);
    }
    if (result == ESP_OK) {
        result = codec_write(0x32, 0xFF);
    }
    if (result == ESP_OK) {
        result = gpio_set_level(board_pins()->audio_enable, 0);
    }
    if (result != ESP_OK) {
        morse_service_stop_tx();
    }
    return result;
}

esp_err_t audio_service_stop_morse(void)
{
    morse_service_stop_tx();
    return gpio_set_level(board_pins()->audio_enable, 1);
}

float audio_service_level_dbfs(void)
{
    return level_dbfs;
}

float audio_service_peak_dbfs(void)
{
    return peak_dbfs;
}

uint16_t audio_service_input_raw_peak(void)
{
    return input_raw_peak;
}