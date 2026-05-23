idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "inc"
    REQUIRES freertos esp_wifi esp_event log nvs_flash lwip my_screen my_mqtt my_ota my_nvs
)