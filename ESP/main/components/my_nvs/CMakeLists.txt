idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "inc"
    REQUIRES freertos driver esp_driver_uart vfs log nvs_flash
)