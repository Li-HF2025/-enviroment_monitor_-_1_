idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "inc"
    PRIV_REQUIRES esp_http_client app_update esp_https_ota
                                  nvs_flash esp_netif esp_wifi efuse bt
                                  protocomm json mbedtls my_nvs
)