#include "detail_dB_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report.h"
#include "my_mqtt.h"
extern lv_obj_t * ui_dBNum;//main01的dB数值显示标签

static float latest_dB_value = 0.0f;//不直接修改ui界面显示的dB数值，而是通过这个变量存储最新的dB值，定时器周期性更新显示
static volatile bool latest_dB_valid = false;

QueueHandle_t dB_queue; // dB数据队列句柄

static void dB_task(void *arg){
    while(1){
        float dB_value;
        if(xQueueReceive(dB_queue, &dB_value, portMAX_DELAY) == pdTRUE){
            latest_dB_value = dB_value;
            if(dB_value <0.0f || dB_value >180.0f) continue; //错误数据过滤
            latest_dB_valid = true;
            mqtt_report_set_float("dB_value", dB_value);
            mqtt_report_set_bool("connect_status", true);
            mqtt_report_request_publish();
        }
    }
}

void dB_init(){
    msg_Request(CMD_DB, (const uint8_t *)"DB Init", 7);
}

void dB_deinit(){
    msg_Request(CMD_DB, (const uint8_t *)"DB DeInit", 9);
}

bool dB_get_latest_valid(void)
{
    return latest_dB_valid;
}

float dB_get_latest_value(void)
{
    return latest_dB_value;
}

void dB_start(){
    static bool started = false;
    if(started){
        dB_init();
        return;
    }
    started = true;

    dB_queue = xQueueCreate(10, sizeof(float));
    xTaskCreate(dB_task, "dB_task", 2048, NULL, 10, NULL);
    dB_init();
}