#include "detail_temp_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report_dispatcher.h"
#include <string.h>
#include "screens/ui_main01.h"
#include "screens/ui_detailTemperature.h"

#define TEMP_WINDOW_SIZE 150    // 滑动窗口：150条 × 2秒 ≈ 5分钟

QueueHandle_t temp_queue;// 温湿度数据队列句柄

static float latest_temp_value = 0.0f;//温度
static float latest_humidity_value = 0.0f;//湿度
static volatile bool latest_temp_valid = false;

// 温度滑动窗口统计
static float temp_window[TEMP_WINDOW_SIZE];
static int   temp_window_idx = 0;
static int   temp_window_count = 0;
static float temp_avg_val = 0.0f;
static float temp_min_val = 0.0f;
static float temp_max_val = 0.0f;

// 湿度滑动窗口统计
static float humi_window[TEMP_WINDOW_SIZE];
static int   humi_window_idx = 0;
static int   humi_window_count = 0;
static float humi_avg_val = 0.0f;
static float humi_min_val = 0.0f;
static float humi_max_val = 0.0f;

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

                // 温度滑动窗口
                temp_window[temp_window_idx] = value;
                temp_window_idx = (temp_window_idx + 1) % TEMP_WINDOW_SIZE;
                if (temp_window_count < TEMP_WINDOW_SIZE) temp_window_count++;

                float tsum = 0.0f, tmin = temp_window[0], tmax = temp_window[0];
                for (int i = 0; i < temp_window_count; i++) {
                    float v = temp_window[i];
                    tsum += v;
                    if (v < tmin) tmin = v;
                    if (v > tmax) tmax = v;
                }
                temp_avg_val = tsum / (float)temp_window_count;
                temp_min_val = tmin;
                temp_max_val = tmax;

                esp_event_post(SENSOR_EVENT_BASE, SENSOR_TEMP_UPDATED, &value, sizeof(float), 0);
            } else if (sensor.sensor_type == 0x04) {
                // 湿度
                if (value < 0.0f || value > 100.0f) continue;
                latest_humidity_value = value;

                // 湿度滑动窗口
                humi_window[humi_window_idx] = value;
                humi_window_idx = (humi_window_idx + 1) % TEMP_WINDOW_SIZE;
                if (humi_window_count < TEMP_WINDOW_SIZE) humi_window_count++;

                float hsum = 0.0f, hmin = humi_window[0], hmax = humi_window[0];
                for (int i = 0; i < humi_window_count; i++) {
                    float v = humi_window[i];
                    hsum += v;
                    if (v < hmin) hmin = v;
                    if (v > hmax) hmax = v;
                }
                humi_avg_val = hsum / (float)humi_window_count;
                humi_min_val = hmin;
                humi_max_val = hmax;

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

float temp_get_avg(void)   { return temp_avg_val; }
float temp_get_min(void)   { return temp_min_val; }
float temp_get_max(void)   { return temp_max_val; }
float humi_get_avg(void)   { return humi_avg_val; }
float humi_get_min(void)   { return humi_min_val; }
float humi_get_max(void)   { return humi_max_val; }

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