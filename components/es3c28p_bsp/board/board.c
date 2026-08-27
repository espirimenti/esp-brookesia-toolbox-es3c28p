#include "board/board.h"

#include "esp_log.h"

static const char *TAG = "board";

static const board_pins_t BOARD_PINS = {
    .lcd_cs = GPIO_NUM_10,
    .lcd_dc = GPIO_NUM_46,
    .lcd_sclk = GPIO_NUM_12,
    .lcd_mosi = GPIO_NUM_11,
    .lcd_miso = GPIO_NUM_13,
    .lcd_backlight = GPIO_NUM_45,
    .touch_sda = GPIO_NUM_16,
    .touch_scl = GPIO_NUM_15,
    .touch_reset = GPIO_NUM_18,
    .touch_interrupt = GPIO_NUM_17,
    .rgb_led = GPIO_NUM_42,
    .battery_adc = GPIO_NUM_9,
    .sd_clk = GPIO_NUM_38,
    .sd_cmd = GPIO_NUM_40,
    .sd_d0 = GPIO_NUM_39,
    .sd_d1 = GPIO_NUM_41,
    .sd_d2 = GPIO_NUM_48,
    .sd_d3 = GPIO_NUM_47,
    .audio_enable = GPIO_NUM_1,
    .i2s_mclk = GPIO_NUM_4,
    .i2s_bclk = GPIO_NUM_5,
    .i2s_dout = GPIO_NUM_8,
    .i2s_lrck = GPIO_NUM_7,
    .i2s_din = GPIO_NUM_6,
    .uart_rx = GPIO_NUM_43,
    .uart_tx = GPIO_NUM_44,
};

const board_pins_t *board_pins(void)
{
    return &BOARD_PINS;
}

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "Board: %s", BOARD_NAME);
    ESP_LOGI(TAG, "LCD SPI: CS=%d DC=%d SCLK=%d MOSI=%d MISO=%d BL=%d",
             BOARD_PINS.lcd_cs,
             BOARD_PINS.lcd_dc,
             BOARD_PINS.lcd_sclk,
             BOARD_PINS.lcd_mosi,
             BOARD_PINS.lcd_miso,
             BOARD_PINS.lcd_backlight);
    ESP_LOGI(TAG, "Touch I2C: SDA=%d SCL=%d RST=%d INT=%d",
             BOARD_PINS.touch_sda,
             BOARD_PINS.touch_scl,
             BOARD_PINS.touch_reset,
             BOARD_PINS.touch_interrupt);
    ESP_LOGI(TAG, "Audio I2S: EN=%d MCLK=%d BCLK=%d DOUT=%d LRCK=%d DIN=%d",
             BOARD_PINS.audio_enable,
             BOARD_PINS.i2s_mclk,
             BOARD_PINS.i2s_bclk,
             BOARD_PINS.i2s_dout,
             BOARD_PINS.i2s_lrck,
             BOARD_PINS.i2s_din);

    return ESP_OK;
}
