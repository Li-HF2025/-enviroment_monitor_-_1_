#include "mqtt_report_dispatcher.h"
#include "mqtt_report.h"
#include "my_mqtt.h"
#include "esp_log.h"
#include "esp_timer.h"

ESP_EVENT_DEFINE_BASE(SENSOR_EVENT_BASE);

static void sensor_event_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data) {
    switch (event_id)
    {
    case SENSOR_DB_UPDATED:
        mqtt_report_set_float("dB_value", *(float *)event_data);
        break;
    case SENSOR_TEMP_UPDATED:
        mqtt_report_set_float("temp_value", *(float *)event_data);
        break;
    case SENSOR_HUMI_UPDATED:
        mqtt_report_set_float("humi_value", *(float *)event_data);
        break;
    default:
        break;
    }
}

static void report_timer_callback(void *arg) {
    mqtt_report_set_bool("connect_status", true);   // 心跳
    mqtt_publish_all_report();
}

void mqtt_report_dispatcher_init(void){
    esp_event_handler_register(SENSOR_EVENT_BASE, ESP_EVENT_ANY_ID, sensor_event_handler, NULL);
    // 1. 准备配置
    const esp_timer_create_args_t timer_args = {
        .callback = &report_timer_callback,   // 调哪个函数
        .name     = "report_timer"            // 调试用名字
    };

    // 2. 创建定时器
    esp_timer_handle_t timer_handle = NULL;
    esp_timer_create(&timer_args, &timer_handle);

    // 3. 启动，每 5 秒触发一次
    esp_timer_start_periodic(timer_handle, 5 * 1000 * 1000);
}