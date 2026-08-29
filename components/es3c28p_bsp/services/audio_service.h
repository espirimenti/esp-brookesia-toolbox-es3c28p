// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SERVICE_UNINITIALIZED,
    AUDIO_SERVICE_READY,
    AUDIO_SERVICE_ERROR,
} audio_service_state_t;

typedef enum {
    AUDIO_WAVE_SINE,
    AUDIO_WAVE_SQUARE,
} audio_waveform_t;

esp_err_t audio_service_init(void);
audio_service_state_t audio_service_state(void);
esp_err_t audio_service_last_error(void);
const char *audio_service_error_stage(void);
esp_err_t audio_service_start_tone(uint32_t frequency_hz,
                                   uint8_t volume_percent,
                                   audio_waveform_t waveform);
esp_err_t audio_service_stop_tone(void);
esp_err_t audio_service_set_tone(uint32_t frequency_hz,
                                 uint8_t volume_percent,
                                 audio_waveform_t waveform);
bool audio_service_tone_active(void);
esp_err_t audio_service_start_morse(const char *text,
                                    uint32_t frequency_hz,
                                    uint8_t volume_percent,
                                    uint8_t wpm);
esp_err_t audio_service_stop_morse(void);
float audio_service_level_dbfs(void);
float audio_service_peak_dbfs(void);
uint16_t audio_service_input_raw_peak(void);

#ifdef __cplusplus
}
#endif