#include "my_nvs.h"
#include "esp_err.h"
#include "stdbool.h"
#include "stdint.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
static nvs_handle_t g_nvs_handle;  
static const char* TAG = "NVS";
void my_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    err = nvs_open("storage", NVS_READWRITE, &g_nvs_handle);
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "NVS全局句柄初始化成功");
}

void my_nvs_set_value(const char* key, const char* value) {
    if(g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS未初始化");
        return;
    }
    ESP_ERROR_CHECK(nvs_set_str(g_nvs_handle, key, value));
    ESP_ERROR_CHECK(nvs_commit(g_nvs_handle));
    ESP_LOGI(TAG, "NVS保存成功: %s = %s", key, value);
}

void my_nvs_get_value(const char* key, char* value, size_t* length) {
    if(g_nvs_handle == 0) {
        ESP_LOGE(TAG, "NVS未初始化");
        return;
    }
    esp_err_t err = nvs_get_str(g_nvs_handle, key, value, length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS键未找到: %s", key);
    } else {
        ESP_ERROR_CHECK(err);
        ESP_LOGI(TAG, "NVS读取成功: %s = %s", key, value);
    }
}