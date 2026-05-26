#ifndef MY_NVS_H
#define MY_NVS_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

void my_nvs_init(void);

/* 默认 namespace ("storage") 的简单存取 —— 向后兼容 */
esp_err_t my_nvs_set_value(const char* key, const char* value);
esp_err_t my_nvs_get_value(const char* key, char* value, size_t* length);

esp_err_t my_nvs_set_u16(const char* key, uint16_t value);
esp_err_t my_nvs_get_u16(const char* key, uint16_t* value);

esp_err_t my_nvs_erase_key(const char* key);

/* 多 namespace 统一管理 */
esp_err_t     my_nvs_open_ns(const char* ns);           /* 打开并注册到表 */
esp_err_t     my_nvs_close_ns(const char* ns);          /* 关闭并从表移除 */
nvs_handle_t  my_nvs_get_handle(const char* ns);        /* 按名查找handle，未找到返回0 */
esp_err_t     my_nvs_register_handle(const char* ns, nvs_handle_t handle); /* 注册外部打开的handle */
void          my_nvs_close_all(void);                   /* 关闭所有已注册的handle */
esp_err_t     my_nvs_erase_all_ns(const char* ns);      /* 擦除指定namespace的全部数据 */

#endif
