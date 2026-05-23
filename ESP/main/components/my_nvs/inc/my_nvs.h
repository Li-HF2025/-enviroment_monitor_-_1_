#ifndef MY_NVS_H
#define MY_NVS_H
#include "stddef.h"
void my_nvs_init(void);
void my_nvs_set_value(const char* key, const char* value);
void my_nvs_get_value(const char* key, char* value, size_t* length);
#endif // MY_NVS_H