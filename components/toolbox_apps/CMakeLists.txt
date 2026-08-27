idf_component_register(
    SRCS "toolbox_apps.cpp" "uart_monitor_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core es3c28p_bsp esp_timer heap
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)
