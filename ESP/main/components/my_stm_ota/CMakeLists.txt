idf_component_register(
    SRCS "src/my_stm_ota_stub.c"
    INCLUDE_DIRS "inc"
    REQUIRES driver esp_http_client nvs_flash freertos my_nvs my_ota mbedtls
)
