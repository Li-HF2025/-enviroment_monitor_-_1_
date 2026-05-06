#include "mqtt_report.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MQTT_REPORT_MAX_ITEMS 16

static mqtt_report_item_t s_items[MQTT_REPORT_MAX_ITEMS] = {
    {.key = "connect_status", .type = MQTT_REPORT_BOOL, .valid = false},
    {.key = "dB_value", .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "temp_value", .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "humi_value", .type = MQTT_REPORT_FLOAT, .valid = false},
    // 将来在这里追加字段项
};

static SemaphoreHandle_t s_lock;

void mqtt_report_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MQTT_REPORT_MAX_ITEMS; i++) s_items[i].valid = false;
    xSemaphoreGive(s_lock);
}

static mqtt_report_item_t *find_item(const char *key, mqtt_report_type_t type)
{
    for (int i = 0; i < MQTT_REPORT_MAX_ITEMS; i++) {
        if (s_items[i].key && s_items[i].type == type && strcmp(s_items[i].key, key) == 0) {
            return &s_items[i];
        }
    }
    return NULL;
}

void mqtt_report_set_bool(const char *key, bool value)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    mqtt_report_item_t *it = find_item(key, MQTT_REPORT_BOOL);
    if (it) { it->value.b = value; it->valid = true; }
    xSemaphoreGive(s_lock);
}

void mqtt_report_set_float(const char *key, float value)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    mqtt_report_item_t *it = find_item(key, MQTT_REPORT_FLOAT);
    if (it) { it->value.f = value; it->valid = true; }
    xSemaphoreGive(s_lock);
}

void mqtt_report_set_int(const char *key, int value)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    mqtt_report_item_t *it = find_item(key, MQTT_REPORT_INT);
    if (it) { it->value.i = value; it->valid = true; }
    xSemaphoreGive(s_lock);
}

const mqtt_report_item_t *mqtt_report_get_all(int *count)
{
    if (count) *count = MQTT_REPORT_MAX_ITEMS;
    return s_items; // 注意：外部读取时不要修改内容；已用 mutex 保护写
}