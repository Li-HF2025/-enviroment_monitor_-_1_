#include "my_nvs.h"
#include "stdbool.h"
#include "stdint.h"
#include "esp_log.h"

static nvs_handle_t g_nvs_handle;
static const char* TAG = "NVS";

void my_nvs_init(void)
{
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

esp_err_t my_nvs_set_value(const char* key, const char* value)
{
    if (g_nvs_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_str(g_nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(g_nvs_handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS保存成功: %s = %s", key, value);
    }
    return err;
}

esp_err_t my_nvs_get_value(const char* key, char* value, size_t* length)
{
    if (g_nvs_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_get_str(g_nvs_handle, key, value, length);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS读取成功: %s = %s", key, value);
    }
    return err;
}

esp_err_t my_nvs_set_u16(const char* key, uint16_t value)
{
    if (g_nvs_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u16(g_nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(g_nvs_handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS保存成功: %s = %u", key, value);
    }
    return err;
}

esp_err_t my_nvs_get_u16(const char* key, uint16_t* value)
{
    if (g_nvs_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_get_u16(g_nvs_handle, key, value);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS读取成功: %s = %u", key, *value);
    }
    return err;
}

esp_err_t my_nvs_erase_key(const char* key)
{
    if (g_nvs_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_erase_key(g_nvs_handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(g_nvs_handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS删除成功: %s", key);
    }
    return err;
}

// 独立 namespace，用于需要数据隔离的场景（如 OTA 断点续传）
esp_err_t my_nvs_open_ns(const char* ns, nvs_handle_t* handle)
{
    return nvs_open(ns, NVS_READWRITE, handle);
}

void my_nvs_close_ns(nvs_handle_t handle)
{
    nvs_close(handle);
}

esp_err_t my_nvs_erase_all_ns(nvs_handle_t handle)
{
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    return err;
}
