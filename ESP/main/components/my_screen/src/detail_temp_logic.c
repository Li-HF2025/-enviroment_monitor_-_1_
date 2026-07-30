#include "detail_temp_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report_dispatcher.h"
QueueHandle_t temp_queue;// 温湿度数据队列句柄

static float latest_temp_value = 0.0f;//温度
static float latest_humidity_value = 0.0f;//湿度
static volatile bool latest_temp_valid = false;

static void temp_task(void *arg){
    while(1){
        UartTxItem item;
        if(xQueueReceive(temp_queue, &item, portMAX_DELAY) == pdTRUE){
            if(item.cmd != CMD_TEMPERATURE || item.payload_len < (int)sizeof(SensorDataBin)){
                continue;
            }

            // 按 SensorDataBin 格式解析（主任务已按 sensor_type 分发了温度/湿度）
            SensorDataBin sensor;
            memcpy(&sensor, item.payload, sizeof(SensorDataBin));
            float value = sensor.value_x10 / 10.0f;

            if (sensor.sensor_type == 0x01) {
                // 温度
                if (value <= -100.0f || value > 80.0f) continue;
                latest_temp_value = value;
                latest_temp_valid = true;
                esp_event_post(SENSOR_EVENT_BASE, SENSOR_TEMP_UPDATED, &value, sizeof(float), 0);
            } else if (sensor.sensor_type == 0x04) {
                // 湿度
                if (value < 0.0f || value > 100.0f) continue;
                latest_humidity_value = value;
                esp_event_post(SENSOR_EVENT_BASE, SENSOR_HUMI_UPDATED, &value, sizeof(float), 0);
            }
        }
    }
}

void temp_init(){
    uint8_t sub_cmd = SUB_CMD_INIT;
    msg_Request(CMD_TEMPERATURE, &sub_cmd, 1);
}

void temp_deinit(){
    uint8_t sub_cmd = SUB_CMD_DEINIT;
    msg_Request(CMD_TEMPERATURE, &sub_cmd, 1);
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

QueueHandle_t temp_logic_get_queue(void){
    return temp_queue;
}