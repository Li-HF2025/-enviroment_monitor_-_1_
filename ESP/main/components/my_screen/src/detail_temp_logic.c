#include "detail_temp_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report.h"
#include "my_mqtt.h"

QueueHandle_t temp_queue;// 温湿度数据队列句柄

static float latest_temp_value = 0.0f;//温度
static float latest_humidity_value = 0.0f;//湿度
static volatile bool latest_temp_valid = false;

static void temp_task(void *arg){
    while(1){
        UartTxItem item;
        if(xQueueReceive(temp_queue, &item, portMAX_DELAY) == pdTRUE){
            if(item.cmd != CMD_TEMPERATURE || item.payload_len < 4){
                continue;
            }

            int16_t temp_raw = (int16_t)((uint16_t)item.payload[0] | ((uint16_t)item.payload[1] << 8));
            uint16_t humidity_raw = (uint16_t)item.payload[2] | ((uint16_t)item.payload[3] << 8);
            float temp_value = (float)temp_raw / 10.0f;
            float humidity_value = (float)humidity_raw / 10.0f;

            if(temp_value <= -100.0f || humidity_value <0.0f) continue;//错误数据过滤

            latest_temp_value = temp_value;
            latest_humidity_value = humidity_value;
            latest_temp_valid = true;

            mqtt_report_set_float("temp_value", temp_value);
            mqtt_report_set_float("humi_value", humidity_value);
            mqtt_report_set_bool("connect_status", true);
            mqtt_report_request_publish();
        }
    }
}

void temp_init(){
    msg_Request(CMD_TEMPERATURE, (const uint8_t *)"DHT22 Init", 11);
}

void temp_deinit(){
    msg_Request(CMD_TEMPERATURE, (const uint8_t *)"DHT22 DeInit", 13);
}

bool temp_get_latest_valid(void)
{
    return latest_temp_valid;
}

float temp_get_latest_value(void)
{
    return latest_temp_value;
}

float temp_get_latest_humidity(void)
{
    return latest_humidity_value;
}

void temp_start(){
    static bool started = false;
    if(started){
        temp_init();
        return;
    }
    started = true;

    temp_queue = xQueueCreate(10, sizeof(UartTxItem));
    xTaskCreate(temp_task, "temp_task", 2048, NULL, 10, NULL);
    temp_init();
}