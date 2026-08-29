// SPDX-License-Identifier: GPL-3.0-only
#include "services/morse_service.h"

#include "freertos/FreeRTOS.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#define MORSE_SAMPLE_RATE 48000
#define MORSE_MAX_EVENTS 512
#define MORSE_MIN_FREQUENCY_HZ 300
#define MORSE_MAX_FREQUENCY_HZ 2000
#define MORSE_MIN_WPM 5
#define MORSE_MAX_WPM 40

typedef struct {
    char character;
    const char *pattern;
} morse_code_t;

typedef struct {
    bool tone;
    uint32_t samples;
} morse_event_t;

static const morse_code_t MORSE_CODES[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
    {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
    {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"},
    {'3', "...--"}, {'4', "....-"}, {'5', "....."},
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
    {'9', "----."}, {'.', ".-.-.-"}, {',', "--..--"},
    {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"},
    {'+', ".-.-."}, {'-', "-....-"},
};

static morse_event_t tx_events[MORSE_MAX_EVENTS];
static volatile size_t tx_event_count;
static volatile size_t tx_event_index;
static volatile uint32_t tx_event_remaining;
static volatile bool tx_active;
static volatile uint32_t tx_frequency_hz = 700;
static volatile uint8_t tx_volume_percent = 35;
static float tx_phase;

static volatile bool decoder_enabled;
static volatile bool decoder_signal;
static volatile uint8_t decoder_quality;
static volatile uint32_t decoder_frequency_hz = 700;
static volatile uint8_t decoder_wpm = 15;
static bool decoder_previous_signal;
static uint32_t decoder_segment_samples;
static bool decoder_character_complete;
static bool decoder_word_complete;
static char decoder_pattern[8];
static size_t decoder_pattern_length;
static char decoder_text[MORSE_RX_TEXT_CAPACITY];
static portMUX_TYPE decoder_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *pattern_for_character(char character)
{
    const char upper = (char)toupper((unsigned char)character);
    for (size_t i = 0; i < sizeof(MORSE_CODES) / sizeof(MORSE_CODES[0]); i++) {
        if (MORSE_CODES[i].character == upper) {
            return MORSE_CODES[i].pattern;
        }
    }
    return NULL;
}

static char character_for_pattern(const char *pattern)
{
    for (size_t i = 0; i < sizeof(MORSE_CODES) / sizeof(MORSE_CODES[0]); i++) {
        if (strcmp(MORSE_CODES[i].pattern, pattern) == 0) {
            return MORSE_CODES[i].character;
        }
    }
    return '#';
}

static bool add_event(bool tone, uint32_t units, uint32_t unit_samples)
{
    if (tx_event_count >= MORSE_MAX_EVENTS) {
        return false;
    }
    tx_events[tx_event_count++] = (morse_event_t) {
        .tone = tone,
        .samples = units * unit_samples,
    };
    return true;
}

esp_err_t morse_service_start_tx(const char *text,
                                 uint32_t frequency_hz,
                                 uint8_t volume_percent,
                                 uint8_t wpm)
{
    if (text == NULL || text[0] == '\0' ||
        frequency_hz < MORSE_MIN_FREQUENCY_HZ ||
        frequency_hz > MORSE_MAX_FREQUENCY_HZ ||
        volume_percent > 100 || wpm < MORSE_MIN_WPM ||
        wpm > MORSE_MAX_WPM) {
        return ESP_ERR_INVALID_ARG;
    }

    tx_active = false;
    tx_event_count = 0;
    const uint32_t unit_samples =
        (MORSE_SAMPLE_RATE * 1200U) / ((uint32_t)wpm * 1000U);
    const size_t text_length = strnlen(text, MORSE_TX_TEXT_CAPACITY);
    for (size_t index = 0; index < text_length;) {
        while (index < text_length && text[index] == ' ') {
            index++;
        }
        if (index >= text_length) {
            break;
        }

        const char *pattern = pattern_for_character(text[index]);
        index++;
        if (pattern == NULL) {
            continue;
        }
        for (size_t symbol = 0; pattern[symbol] != '\0'; symbol++) {
            if (!add_event(true, pattern[symbol] == '-' ? 3 : 1, unit_samples)) {
                return ESP_ERR_NO_MEM;
            }
            if (pattern[symbol + 1] != '\0' &&
                !add_event(false, 1, unit_samples)) {
                return ESP_ERR_NO_MEM;
            }
        }

        size_t probe = index;
        bool word_gap = false;
        while (probe < text_length && text[probe] == ' ') {
            word_gap = true;
            probe++;
        }
        if (probe < text_length &&
            !add_event(false, word_gap ? 7 : 3, unit_samples)) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (tx_event_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    tx_frequency_hz = frequency_hz;
    tx_volume_percent = volume_percent;
    tx_event_index = 0;
    tx_event_remaining = tx_events[0].samples;
    tx_phase = 0.0f;
    tx_active = true;
    return ESP_OK;
}

void morse_service_stop_tx(void)
{
    tx_active = false;
}

bool morse_service_tx_active(void)
{
    return tx_active;
}

void morse_service_render(int16_t *stereo_samples, size_t frame_count)
{
    if (stereo_samples == NULL) {
        return;
    }
    const float phase_step =
        (float)tx_frequency_hz / (float)MORSE_SAMPLE_RATE;
    const float amplitude =
        32767.0f * 0.45f * ((float)tx_volume_percent / 100.0f);

    for (size_t frame = 0; frame < frame_count; frame++) {
        int16_t sample = 0;
        if (tx_active) {
            if (tx_events[tx_event_index].tone) {
                sample = (int16_t)(
                    sinf(tx_phase * 2.0f * (float)M_PI) * amplitude);
                tx_phase += phase_step;
                if (tx_phase >= 1.0f) {
                    tx_phase -= floorf(tx_phase);
                }
            }
            if (tx_event_remaining > 0) {
                tx_event_remaining--;
            }
            if (tx_event_remaining == 0) {
                tx_event_index++;
                if (tx_event_index >= tx_event_count) {
                    tx_active = false;
                } else {
                    tx_event_remaining = tx_events[tx_event_index].samples;
                }
            }
        }
        stereo_samples[frame * 2] = sample;
        stereo_samples[frame * 2 + 1] = sample;
    }
}

static void append_decoded_character(void)
{
    if (decoder_pattern_length == 0) {
        return;
    }
    decoder_pattern[decoder_pattern_length] = '\0';
    const char character = character_for_pattern(decoder_pattern);
    portENTER_CRITICAL(&decoder_lock);
    size_t length = strlen(decoder_text);
    if (length + 1 >= sizeof(decoder_text)) {
        memmove(decoder_text, decoder_text + 1, length);
        length--;
    }
    decoder_text[length] = character;
    decoder_text[length + 1] = '\0';
    portEXIT_CRITICAL(&decoder_lock);
    decoder_pattern_length = 0;
}

static void append_decoded_space(void)
{
    portENTER_CRITICAL(&decoder_lock);
    size_t length = strlen(decoder_text);
    if (length > 0 && decoder_text[length - 1] != ' ' &&
        length + 1 < sizeof(decoder_text)) {
        decoder_text[length] = ' ';
        decoder_text[length + 1] = '\0';
    }
    portEXIT_CRITICAL(&decoder_lock);
}

esp_err_t morse_service_set_decoder(bool enabled,
                                    uint32_t frequency_hz,
                                    uint8_t wpm)
{
    if (frequency_hz < MORSE_MIN_FREQUENCY_HZ ||
        frequency_hz > MORSE_MAX_FREQUENCY_HZ ||
        wpm < MORSE_MIN_WPM || wpm > MORSE_MAX_WPM) {
        return ESP_ERR_INVALID_ARG;
    }
    decoder_frequency_hz = frequency_hz;
    decoder_wpm = wpm;
    decoder_previous_signal = false;
    decoder_signal = false;
    decoder_quality = 0;
    decoder_segment_samples = 0;
    decoder_character_complete = false;
    decoder_word_complete = false;
    decoder_pattern_length = 0;
    decoder_enabled = enabled;
    return ESP_OK;
}

bool morse_service_decoder_enabled(void)
{
    return decoder_enabled;
}

bool morse_service_signal_detected(void)
{
    return decoder_signal;
}

uint8_t morse_service_signal_quality(void)
{
    return decoder_quality;
}

void morse_service_process_input(const int16_t *samples, size_t frames)
{
    if (!decoder_enabled || samples == NULL || frames < 32) {
        return;
    }

    const float omega =
        2.0f * (float)M_PI * (float)decoder_frequency_hz /
        (float)MORSE_SAMPLE_RATE;
    const float coefficient = 2.0f * cosf(omega);
    float q1[2] = {0.0f, 0.0f};
    float q2[2] = {0.0f, 0.0f};
    double energy[2] = {0.0, 0.0};
    for (size_t frame = 0; frame < frames; frame++) {
        for (size_t channel = 0; channel < 2; channel++) {
            const float sample = (float)samples[frame * 2 + channel];
            const float q0 = coefficient * q1[channel] - q2[channel] + sample;
            q2[channel] = q1[channel];
            q1[channel] = q0;
            energy[channel] += (double)sample * sample;
        }
    }

    float best_ratio = 0.0f;
    float best_rms = 0.0f;
    for (size_t channel = 0; channel < 2; channel++) {
        const float power = q1[channel] * q1[channel] +
                            q2[channel] * q2[channel] -
                            coefficient * q1[channel] * q2[channel];
        const float ratio = energy[channel] > 1.0
                                ? power / ((float)energy[channel] * frames)
                                : 0.0f;
        const float rms = sqrtf((float)(energy[channel] / frames));
        if (ratio > best_ratio) {
            best_ratio = ratio;
            best_rms = rms;
        }
    }

    const bool signal = best_rms > 120.0f && best_ratio > 0.12f;
    decoder_signal = signal;
    int quality = (int)(best_ratio * 200.0f);
    if (quality > 100) {
        quality = 100;
    }
    decoder_quality = (uint8_t)quality;

    if (signal == decoder_previous_signal) {
        decoder_segment_samples += frames;
    } else {
        const uint32_t unit_samples =
            (MORSE_SAMPLE_RATE * 1200U) /
            ((uint32_t)decoder_wpm * 1000U);
        if (decoder_previous_signal && decoder_pattern_length < 7) {
            decoder_pattern[decoder_pattern_length++] =
                decoder_segment_samples < unit_samples * 2U ? '.' : '-';
            decoder_character_complete = false;
            decoder_word_complete = false;
        }
        decoder_previous_signal = signal;
        decoder_segment_samples = frames;
    }

    if (!signal) {
        const uint32_t unit_samples =
            (MORSE_SAMPLE_RATE * 1200U) /
            ((uint32_t)decoder_wpm * 1000U);
        if (!decoder_character_complete &&
            decoder_segment_samples >= unit_samples * 3U) {
            append_decoded_character();
            decoder_character_complete = true;
        }
        if (!decoder_word_complete &&
            decoder_segment_samples >= unit_samples * 7U) {
            append_decoded_space();
            decoder_word_complete = true;
        }
    }
}

void morse_service_get_decoded_text(char *output, size_t capacity)
{
    if (output == NULL || capacity == 0) {
        return;
    }
    portENTER_CRITICAL(&decoder_lock);
    strlcpy(output, decoder_text, capacity);
    portEXIT_CRITICAL(&decoder_lock);
}

void morse_service_clear_decoded_text(void)
{
    portENTER_CRITICAL(&decoder_lock);
    decoder_text[0] = '\0';
    portEXIT_CRITICAL(&decoder_lock);
    decoder_pattern_length = 0;
}
