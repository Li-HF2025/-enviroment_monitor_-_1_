#include "mqtt_report_dispatcher.h"
#include "mqtt_report.h"
#include "my_mqtt.h"
#include "sensor_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

ESP_EVENT_DEFINE_BASE(SENSOR_EVENT_BASE);

static bool s_wifi_connected = false;

static void sensor_event_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data) {
    float value = *(float *)event_data;
    int16_t value_x10 = (int16_t)(value * 10.0f + (value >= 0 ? 0.5f : -0.5f));

    switch (event_id)
    {
    case SENSOR_DB_UPDATED:
        mqtt_report_set_float("dB_value", value);
        if (!s_wifi_connected) sensor_cache_push(0x02, value_x10);
        break;
    case SENSOR_TEMP_UPDATED:
        mqtt_report_set_float("temp_value", value);
        if (!s_wifi_connected) sensor_cache_push(0x01, value_x10);
        break;
    case SENSOR_HUMI_UPDATED:
        mqtt_report_set_float("humi_value", value);
        if (!s_wifi_connected) sensor_cache_push(0x04, value_x10);
        break;
    default:
        break;
    }
}

static void report_timer_callback(void *arg) {
    mqtt_report_set_bool("connect_status", true);
    mqtt_publish_all_report();
}

/** @brief 补传单条缓存数据到 MQTT */
static bool cache_publish_cb(uint8_t sensor_type, int16_t value_x10, uint32_t timestamp)
{
    const char *key = NULL;
    if (sensor_type == 0x01)      key = "temp_value";
    else if (sensor_type == 0x02) key = "dB_value";
    else if (sensor_type == 0x04) key = "humi_value";
    else return false;

    mqtt_report_set_float(key, value_x10 / 10.0f);
    mqtt_publish_all_report();  // 用批量发布代替单条，复用已有 JSON 编码
    return true;  // 尽力而为，不计单条成败
}

/** @brief WiFi 事件处理：记录连接状态，恢复时触发补传 */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == MY_WIFI_EVENT_BASE) {
        if (event_id == MY_WIFI_EVENT_CONNECTED) {
            s_wifi_connected = true;
            int pending = sensor_cache_pending_count();
            if (pending > 0) {
                ESP_LOGI("DISPATCHER", "WiFi 恢复，开始补传 %d 条缓存数据", pending);
                int flushed = sensor_cache_flush(cache_publish_cb);
                ESP_LOGI("DISPATCHER", "补传完成：成功 %d 条，剩余 %d 条",
                         flushed, sensor_cache_pending_count());
            }
        } else if (event_id == MY_WIFI_EVENT_DISCONNECTED) {
            s_wifi_connected = false;
            ESP_LOGI("DISPATCHER", "WiFi 断开，传感器数据将写入本地缓存");
        }
    }
}

void mqtt_report_dispatcher_init(void){
    mqtt_report_init();
    sensor_cache_init();

    esp_event_handler_register(SENSOR_EVENT_BASE, ESP_EVENT_ANY_ID, sensor_event_handler, NULL);
    esp_event_handler_register(MY_WIFI_EVENT_BASE, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);

    const esp_timer_create_args_t timer_args = {
        .callback = &report_timer_callback,
        .name     = "report_timer"
    };
    esp_timer_handle_t timer_handle = NULL;
    esp_timer_create(&timer_args, &timer_handle);
    esp_timer_start_periodic(timer_handle, 5 * 1000 * 1000);

    s_wifi_connected = true;
}