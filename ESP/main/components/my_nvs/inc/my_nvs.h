#ifndef MY_NVS_H
#define MY_NVS_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
void my_nvs_init(void);

esp_err_t my_nvs_set_value(const char* key, const char* value);
esp_err_t my_nvs_get_value(const char* key, char* value, size_t* length);

esp_err_t my_nvs_set_u16(const char* key, uint16_t value);
esp_err_t my_nvs_get_u16(const char* key, uint16_t* value);

esp_err_t my_nvs_erase_key(const char* key);

esp_err_t my_nvs_open_ns(const char* ns, nvs_handle_t* handle);
void      my_nvs_close_ns(nvs_handle_t handle);
esp_err_t my_nvs_erase_all_ns(nvs_handle_t handle);

#endif
