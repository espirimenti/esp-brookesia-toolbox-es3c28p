#include "board/board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define LCD_HOST SPI2_HOST
#define LCD_NATIVE_WIDTH 240
#define LCD_NATIVE_HEIGHT 320
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_DMA_CHUNK_PIXELS 1024
#define LCD_BACKLIGHT_TIMER LEDC_TIMER_0
#define LCD_BACKLIGHT_CHANNEL LEDC_CHANNEL_0
#define LCD_BACKLIGHT_MODE LEDC_LOW_SPEED_MODE
#define LCD_BACKLIGHT_RESOLUTION LEDC_TIMER_10_BIT
#define LCD_BACKLIGHT_MAX_DUTY 1023
#define LCD_BACKLIGHT_FREQ_HZ 5000

#define ILI9341_CMD_SWRESET 0x01
#define ILI9341_CMD_SLPOUT 0x11
#define ILI9341_CMD_INVOFF 0x20
#define ILI9341_CMD_INVON 0x21
#define ILI9341_CMD_MADCTL 0x36
#define ILI9341_CMD_COLMOD 0x3A
#define ILI9341_CMD_CASET 0x2A
#define ILI9341_CMD_PASET 0x2B
#define ILI9341_CMD_RAMWR 0x2C
#define ILI9341_CMD_DISPON 0x29

static const char *TAG = "display";
static spi_device_handle_t lcd_spi;
static uint8_t *line_buffer;
static uint16_t display_width = LCD_NATIVE_WIDTH;
static uint16_t display_height = LCD_NATIVE_HEIGHT;

static esp_err_t lcd_spi_tx(const void *data, size_t length_bytes)
{
    if (length_bytes == 0) {
        return ESP_OK;
    }

    spi_transaction_t transaction = {
        .length = length_bytes * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(lcd_spi, &transaction);
}

static esp_err_t lcd_cmd(uint8_t command)
{
    const board_pins_t *pins = board_pins();
    ESP_ERROR_CHECK(gpio_set_level(pins->lcd_dc, 0));
    return lcd_spi_tx(&command, sizeof(command));
}

static esp_err_t lcd_data(const void *data, size_t length_bytes)
{
    const board_pins_t *pins = board_pins();
    ESP_ERROR_CHECK(gpio_set_level(pins->lcd_dc, 1));
    return lcd_spi_tx(data, length_bytes);
}

static esp_err_t lcd_write_cmd_data(uint8_t command, const void *data, size_t length_bytes)
{
    ESP_ERROR_CHECK(lcd_cmd(command));
    return lcd_data(data, length_bytes);
}

static esp_err_t lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t column_data[] = {
        (uint8_t)(x0 >> 8),
        (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8),
        (uint8_t)(x1 & 0xFF),
    };
    const uint8_t row_data[] = {
        (uint8_t)(y0 >> 8),
        (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8),
        (uint8_t)(y1 & 0xFF),
    };

    ESP_ERROR_CHECK(lcd_write_cmd_data(ILI9341_CMD_CASET, column_data, sizeof(column_data)));
    ESP_ERROR_CHECK(lcd_write_cmd_data(ILI9341_CMD_PASET, row_data, sizeof(row_data)));
    return lcd_cmd(ILI9341_CMD_RAMWR);
}

static esp_err_t lcd_init_sequence(void)
{
    const uint8_t color_mode = 0x55; /* 16-bit RGB565 */
    const uint8_t madctl = 0x48;     /* MX + BGR, portrait 240x320 */

    ESP_ERROR_CHECK(lcd_cmd(ILI9341_CMD_SWRESET));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(lcd_cmd(ILI9341_CMD_SLPOUT));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(lcd_cmd(ILI9341_CMD_INVON));
    ESP_ERROR_CHECK(lcd_write_cmd_data(ILI9341_CMD_COLMOD, &color_mode, sizeof(color_mode)));
    ESP_ERROR_CHECK(lcd_write_cmd_data(ILI9341_CMD_MADCTL, &madctl, sizeof(madctl)));
    ESP_ERROR_CHECK(lcd_cmd(ILI9341_CMD_DISPON));
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

esp_err_t board_display_fill_rect(uint16_t x0,
                                  uint16_t y0,
                                  uint16_t x1,
                                  uint16_t y1,
                                  uint16_t color_rgb565)
{
    if (lcd_spi == NULL || line_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (x0 > x1 || y0 > y1 || x1 >= display_width || y1 >= display_height) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < LCD_DMA_CHUNK_PIXELS; i++) {
        line_buffer[i * 2] = (uint8_t)(color_rgb565 >> 8);
        line_buffer[i * 2 + 1] = (uint8_t)(color_rgb565 & 0xFF);
    }

    ESP_ERROR_CHECK(lcd_set_window(x0, y0, x1, y1));
    ESP_ERROR_CHECK(gpio_set_level(board_pins()->lcd_dc, 1));

    size_t pixels_left = (size_t)(x1 - x0 + 1) * (y1 - y0 + 1);
    while (pixels_left > 0) {
        const size_t chunk_pixels =
            pixels_left > LCD_DMA_CHUNK_PIXELS ? LCD_DMA_CHUNK_PIXELS : pixels_left;
        ESP_ERROR_CHECK(lcd_spi_tx(line_buffer, chunk_pixels * sizeof(uint16_t)));
        pixels_left -= chunk_pixels;
    }

    return ESP_OK;
}

esp_err_t board_display_init(void)
{
    const board_pins_t *pins = board_pins();

    const gpio_config_t output_config = {
        .pin_bit_mask = 1ULL << pins->lcd_dc,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output_config));

    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LCD_BACKLIGHT_MODE,
        .duty_resolution = LCD_BACKLIGHT_RESOLUTION,
        .timer_num = LCD_BACKLIGHT_TIMER,
        .freq_hz = LCD_BACKLIGHT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));

    const ledc_channel_config_t backlight_channel = {
        .gpio_num = pins->lcd_backlight,
        .speed_mode = LCD_BACKLIGHT_MODE,
        .channel = LCD_BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));

    const spi_bus_config_t bus_config = {
        .sclk_io_num = pins->lcd_sclk,
        .mosi_io_num = pins->lcd_mosi,
        .miso_io_num = pins->lcd_miso,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_DMA_CHUNK_PIXELS * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = LCD_PIXEL_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = pins->lcd_cs,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &device_config, &lcd_spi));

    line_buffer = heap_caps_malloc(LCD_DMA_CHUNK_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(lcd_init_sequence());
    ESP_ERROR_CHECK(board_display_fill(0x1082));

    ESP_LOGI(TAG, "ILI9341 initialized, backlight PWM ready");
    return ESP_OK;
}

esp_err_t board_display_set_orientation(uint16_t orientation_degrees)
{
    uint8_t madctl;
    switch (orientation_degrees) {
    case 0:
        madctl = 0x48; /* MX + BGR */
        display_width = LCD_NATIVE_WIDTH;
        display_height = LCD_NATIVE_HEIGHT;
        break;
    case 90:
        madctl = 0x28; /* MV + BGR */
        display_width = LCD_NATIVE_HEIGHT;
        display_height = LCD_NATIVE_WIDTH;
        break;
    case 180:
        madctl = 0x88; /* MY + BGR */
        display_width = LCD_NATIVE_WIDTH;
        display_height = LCD_NATIVE_HEIGHT;
        break;
    case 270:
        madctl = 0xE8; /* MX + MY + MV + BGR */
        display_width = LCD_NATIVE_HEIGHT;
        display_height = LCD_NATIVE_WIDTH;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(lcd_write_cmd_data(ILI9341_CMD_MADCTL, &madctl, sizeof(madctl)));
    ESP_LOGI(TAG, "Orientation=%u, viewport=%ux%u",
             orientation_degrees, display_width, display_height);
    return ESP_OK;
}

esp_err_t board_display_set_color_inversion(bool enabled)
{
    ESP_ERROR_CHECK(lcd_cmd(enabled ? ILI9341_CMD_INVON : ILI9341_CMD_INVOFF));
    ESP_LOGI(TAG, "Color inversion %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t board_display_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t duty =
        ((uint32_t)LCD_BACKLIGHT_MAX_DUTY * brightness_percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BACKLIGHT_MODE, LCD_BACKLIGHT_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BACKLIGHT_MODE, LCD_BACKLIGHT_CHANNEL));
    ESP_LOGI(TAG, "Backlight brightness=%u%%", brightness_percent);
    return ESP_OK;
}

uint16_t board_display_width(void)
{
    return display_width;
}

uint16_t board_display_height(void)
{
    return display_height;
}

esp_err_t board_display_fill(uint16_t color_rgb565)
{
    return board_display_fill_rect(
        0, 0, display_width - 1, display_height - 1, color_rgb565);
}

esp_err_t board_display_draw_bitmap(uint16_t x0,
                                    uint16_t y0,
                                    uint16_t x1,
                                    uint16_t y1,
                                    const uint16_t *pixels)
{
    if (lcd_spi == NULL || line_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pixels == NULL ||
        x0 >= x1 ||
        y0 >= y1 ||
        x1 > display_width ||
        y1 > display_height) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(lcd_set_window(x0, y0, x1 - 1, y1 - 1));
    ESP_ERROR_CHECK(gpio_set_level(board_pins()->lcd_dc, 1));

    size_t pixels_left = (size_t)(x1 - x0) * (y1 - y0);
    while (pixels_left > 0) {
        const size_t chunk_pixels =
            pixels_left > LCD_DMA_CHUNK_PIXELS ? LCD_DMA_CHUNK_PIXELS : pixels_left;
        for (size_t i = 0; i < chunk_pixels; i++) {
            const uint16_t color = pixels[i];
            line_buffer[i * 2] = (uint8_t)(color >> 8);
            line_buffer[i * 2 + 1] = (uint8_t)(color & 0xFF);
        }
        ESP_ERROR_CHECK(lcd_spi_tx(line_buffer, chunk_pixels * sizeof(uint16_t)));
        pixels += chunk_pixels;
        pixels_left -= chunk_pixels;
    }

    return ESP_OK;
}
