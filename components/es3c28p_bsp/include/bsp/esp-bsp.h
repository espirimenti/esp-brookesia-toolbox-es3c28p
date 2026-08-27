#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BSP_LCD_H_RES 320
#define BSP_LCD_V_RES 240
typedef struct { uint32_t task_priority, task_stack; int32_t task_affinity; uint32_t task_max_sleep_ms, timer_period_ms; } bsp_lvgl_port_cfg_t;
typedef struct { bsp_lvgl_port_cfg_t lvgl_port_cfg; size_t buffer_size; bool double_buffer; } bsp_display_cfg_t;
#define BSP_DISPLAY_CONFIG_DEFAULT() { .lvgl_port_cfg = { 4, 10 * 1024, 1, 20, 5 }, .buffer_size = BSP_LCD_H_RES * 24, .double_buffer = true }
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *config);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
#ifdef __cplusplus
}
#endif