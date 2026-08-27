#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME "LCDWiki ES3C28P"

typedef enum {
    BOARD_EXPAND_GPIO_2 = GPIO_NUM_2,
    BOARD_EXPAND_GPIO_3 = GPIO_NUM_3,
    BOARD_EXPAND_GPIO_14 = GPIO_NUM_14,
    BOARD_EXPAND_GPIO_21 = GPIO_NUM_21,
} board_expand_gpio_t;

typedef struct {
    gpio_num_t lcd_cs;
    gpio_num_t lcd_dc;
    gpio_num_t lcd_sclk;
    gpio_num_t lcd_mosi;
    gpio_num_t lcd_miso;
    gpio_num_t lcd_backlight;
    gpio_num_t touch_sda;
    gpio_num_t touch_scl;
    gpio_num_t touch_reset;
    gpio_num_t touch_interrupt;
    gpio_num_t rgb_led;
    gpio_num_t battery_adc;
    gpio_num_t sd_clk;
    gpio_num_t sd_cmd;
    gpio_num_t sd_d0;
    gpio_num_t sd_d1;
    gpio_num_t sd_d2;
    gpio_num_t sd_d3;
    gpio_num_t audio_enable;
    gpio_num_t i2s_mclk;
    gpio_num_t i2s_bclk;
    gpio_num_t i2s_dout;
    gpio_num_t i2s_lrck;
    gpio_num_t i2s_din;
    gpio_num_t uart_rx;
    gpio_num_t uart_tx;
} board_pins_t;

typedef struct {
    bool pressed;
    uint8_t touch_count;
    uint16_t x;
    uint16_t y;
} board_touch_state_t;

const board_pins_t *board_pins(void);
esp_err_t board_init(void);
esp_err_t board_display_init(void);
esp_err_t board_display_set_orientation(uint16_t orientation_degrees);
esp_err_t board_display_set_color_inversion(bool enabled);
esp_err_t board_display_set_brightness(uint8_t brightness_percent);
uint16_t board_display_width(void);
uint16_t board_display_height(void);
esp_err_t board_display_fill(uint16_t color_rgb565);
esp_err_t board_display_fill_rect(uint16_t x0,
                                  uint16_t y0,
                                  uint16_t x1,
                                  uint16_t y1,
                                  uint16_t color_rgb565);
esp_err_t board_display_draw_bitmap(uint16_t x0,
                                    uint16_t y0,
                                    uint16_t x1,
                                    uint16_t y1,
                                    const uint16_t *pixels);
esp_err_t board_touch_init(void);
esp_err_t board_touch_read(board_touch_state_t *state);

#ifdef __cplusplus
}
#endif
