idf_component_register(
 SRCS "esp_bsp.c" "board/board.c" "board/display.c" "board/touch.c" "services/i2c_service.c"
 INCLUDE_DIRS "." "include"
 REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_timer lvgl
)