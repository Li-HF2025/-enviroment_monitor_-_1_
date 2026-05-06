idf_component_register(
    SRCS "src/my_wifi.c"
    INCLUDE_DIRS "inc"
    REQUIRES freertos esp_wifi esp_event log nvs_flash lwip my_screen
)