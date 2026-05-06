idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "inc"
    REQUIRES freertos log driver esp_lcd lvgl esp_lcd_ili9341 esp_lcd_touch_xpt2046 my_ui my_wifi my_serial my_mqtt
)