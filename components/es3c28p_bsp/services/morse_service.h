// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MORSE_TX_TEXT_CAPACITY 48
#define MORSE_RX_TEXT_CAPACITY 128

esp_err_t morse_service_start_tx(const char *text,
                                 uint32_t frequency_hz,
                                 uint8_t volume_percent,
                                 uint8_t wpm);
void morse_service_stop_tx(void);
bool morse_service_tx_active(void);
void morse_service_render(int16_t *stereo_samples, size_t frame_count);

esp_err_t morse_service_set_decoder(bool enabled,
                                    uint32_t frequency_hz,
                                    uint8_t wpm);
bool morse_service_decoder_enabled(void);
bool morse_service_signal_detected(void);
uint8_t morse_service_signal_quality(void);
void morse_service_process_input(const int16_t *stereo_samples,
                                 size_t frame_count);
void morse_service_get_decoded_text(char *output, size_t capacity);
void morse_service_clear_decoded_text(void);

#ifdef __cplusplus
}
#endif
