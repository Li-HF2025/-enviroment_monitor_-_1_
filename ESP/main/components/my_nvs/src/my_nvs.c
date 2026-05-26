#include "my_nvs.h"
#include "stdbool.h"
#include "stdint.h"
#include "string.h"
#include "esp_log.h"

#define MY_NVS_MAX_NAMESPACES 8
#define MY_NVS_NS_NAME_MAX    16

typedef struct {
    char name[MY_NVS_NS_NAME_MAX];
    nvs_handle_t handle;
    bool in_use;
} my_nvs_ns_entry_t;

static nvs_handle_t g_nvs_handle;
static my_nvs_ns_entry_t g_ns_registry[MY_NVS_MAX_NAMESPACES];
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

    /* 注册默认 namespace */
    strncpy(g_ns_registry[0].name, "storage", MY_NVS_NS_NAME_MAX - 1);
    g_ns_registry[0].name[MY_NVS_NS_NAME_MAX - 1] = '\0';
    g_ns_registry[0].handle = g_nvs_handle;
    g_ns_registry[0].in_use = true;

    ESP_LOGI(TAG, "NVS初始化成功，默认namespace: storage");
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

/* ========== 多 namespace 统一管理 ========== */

static int find_free_slot(void)
{
    for (int i = 0; i < MY_NVS_MAX_NAMESPACES; i++) {
        if (!g_ns_registry[i].in_use) {
            return i;
        }
    }
    return -1;
}

static int find_ns_slot(const char* ns)
{
    for (int i = 0; i < MY_NVS_MAX_NAMESPACES; i++) {
        if (g_ns_registry[i].in_use && strcmp(g_ns_registry[i].name, ns) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t my_nvs_open_ns(const char* ns)
{
    if (find_ns_slot(ns) >= 0) {
        ESP_LOGW(TAG, "namespace %s 已打开，跳过", ns);
        return ESP_OK;
    }

    int slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "namespace槽位已满 (%d)", MY_NVS_MAX_NAMESPACES);
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    strncpy(g_ns_registry[slot].name, ns, MY_NVS_NS_NAME_MAX - 1);
    g_ns_registry[slot].name[MY_NVS_NS_NAME_MAX - 1] = '\0';
    g_ns_registry[slot].handle = handle;
    g_ns_registry[slot].in_use = true;

    ESP_LOGI(TAG, "打开namespace: %s (slot %d)", ns, slot);
    return ESP_OK;
}

esp_err_t my_nvs_close_ns(const char* ns)
{
    int slot = find_ns_slot(ns);
    if (slot < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    nvs_close(g_ns_registry[slot].handle);
    g_ns_registry[slot].in_use = false;
    g_ns_registry[slot].name[0] = '\0';

    ESP_LOGI(TAG, "关闭namespace: %s", ns);
    return ESP_OK;
}

nvs_handle_t my_nvs_get_handle(const char* ns)
{
    int slot = find_ns_slot(ns);
    if (slot < 0) {
        return 0;
    }
    return g_ns_registry[slot].handle;
}

esp_err_t my_nvs_register_handle(const char* ns, nvs_handle_t handle)
{
    if (find_ns_slot(ns) >= 0) {
        ESP_LOGW(TAG, "namespace %s 已注册", ns);
        return ESP_ERR_INVALID_STATE;
    }

    int slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "namespace槽位已满 (%d)", MY_NVS_MAX_NAMESPACES);
        return ESP_ERR_NO_MEM;
    }

    strncpy(g_ns_registry[slot].name, ns, MY_NVS_NS_NAME_MAX - 1);
    g_ns_registry[slot].name[MY_NVS_NS_NAME_MAX - 1] = '\0';
    g_ns_registry[slot].handle = handle;
    g_ns_registry[slot].in_use = true;

    ESP_LOGI(TAG, "注册namespace: %s (slot %d)", ns, slot);
    return ESP_OK;
}

void my_nvs_close_all(void)
{
    for (int i = 0; i < MY_NVS_MAX_NAMESPACES; i++) {
        if (g_ns_registry[i].in_use) {
            nvs_close(g_ns_registry[i].handle);
            g_ns_registry[i].in_use = false;
            g_ns_registry[i].name[0] = '\0';
        }
    }
    g_nvs_handle = 0;
    ESP_LOGI(TAG, "已关闭所有namespace");
}

esp_err_t my_nvs_erase_all_ns(const char* ns)
{
    nvs_handle_t handle = my_nvs_get_handle(ns);
    if (handle == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "已擦除namespace: %s", ns);
    }
    return err;
}
