#include "bsp/esp-bsp.h"
#include <stdlib.h>
#include "board/board.h"
#include "services/i2c_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "es3c28p_bsp";
static lv_display_t *display;
static SemaphoreHandle_t mutex;
static bool touch_ready;

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    esp_err_t result = board_display_draw_bitmap(
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, (const uint16_t *)pixels);
    if (result != ESP_OK) ESP_LOGE(TAG, "Flush failed: %s", esp_err_to_name(result));
    lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!touch_ready) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    board_touch_state_t state = {0};
    if (board_touch_read(&state) != ESP_OK || !state.pressed) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int32_t x = state.y;
    int32_t y = 239 - state.x;
    data->point.x = x < 0 ? 0 : (x >= BSP_LCD_H_RES ? BSP_LCD_H_RES - 1 : x);
    data->point.y = y < 0 ? 0 : (y >= BSP_LCD_V_RES ? BSP_LCD_V_RES - 1 : y);
    data->state = LV_INDEV_STATE_PRESSED;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    if (mutex == NULL) return false;
    TickType_t timeout = timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(mutex, timeout) == pdTRUE;
}

void bsp_display_unlock(void)
{
    if (mutex != NULL) xSemaphoreGiveRecursive(mutex);
}

static void lvgl_task(void *argument)
{
    bsp_lvgl_port_cfg_t config = *(bsp_lvgl_port_cfg_t *)argument;
    free(argument);
    while (true) {
        uint32_t delay = config.task_max_sleep_ms;
        if (bsp_display_lock(0)) {
            delay = lv_timer_handler();
            bsp_display_unlock();
        }
        if (delay < config.timer_period_ms) delay = config.timer_period_ms;
        if (delay > config.task_max_sleep_ms) delay = config.task_max_sleep_ms;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *config)
{
    if (config == NULL || display != NULL) return NULL;
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(i2c_service_init());
    ESP_ERROR_CHECK(board_display_init());
    ESP_ERROR_CHECK(board_display_set_orientation(90));
    esp_err_t touch_status = board_touch_init();
    touch_ready = (touch_status == ESP_OK);
    if (touch_status != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller is unavailable: %s; continuing without input",
                 esp_err_to_name(touch_status));
    }

    mutex = xSemaphoreCreateRecursiveMutex();
    if (mutex == NULL) return NULL;
    lv_init();
    lv_tick_set_cb(tick_ms);

    size_t pixels = config->buffer_size ? config->buffer_size : BSP_LCD_H_RES * 24;
    size_t bytes = pixels * sizeof(lv_color_t);
    void *buffer_a = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *buffer_b = config->double_buffer
        ? heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : NULL;
    if (buffer_a == NULL || (config->double_buffer && buffer_b == NULL)) return NULL;

    display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (display == NULL) return NULL;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_buffers(display, buffer_a, buffer_b, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *touch = lv_indev_create();
    if (touch == NULL) return NULL;
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_cb);
    lv_indev_set_display(touch, display);

    bsp_lvgl_port_cfg_t *task_config = malloc(sizeof(*task_config));
    if (task_config == NULL) return NULL;
    *task_config = config->lvgl_port_cfg;
    BaseType_t created = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", task_config->task_stack, task_config,
        task_config->task_priority, NULL, task_config->task_affinity);
    if (created != pdPASS) { free(task_config); return NULL; }

    ESP_LOGI(TAG, "ES3C28P display ready at 320x240 (touch: %s)",
             touch_status == ESP_OK ? "ready" : "unavailable");
    return display;
}

esp_err_t bsp_display_backlight_on(void) { return board_display_set_brightness(100); }
esp_err_t bsp_display_backlight_off(void) { return board_display_set_brightness(0); }
