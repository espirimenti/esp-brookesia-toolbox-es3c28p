idf_component_register(
    SRCS "assets/toolbox_icons.c" "audio_tool_app.cpp" "ble_scanner_app.cpp" "gpio_tool_app.cpp" "logic_analyzer_app.cpp" "settings_app.cpp" "toolbox_apps.cpp" "uart_monitor_app.cpp" "wifi_scanner_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core es3c28p_bsp esp_timer heap
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)
