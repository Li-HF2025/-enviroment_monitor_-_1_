#include "detail_dB_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report_dispatcher.h"
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
            esp_event_post(SENSOR_EVENT_BASE, SENSOR_DB_UPDATED, &dB_value, sizeof(float), 0);
        }
    }
}

void dB_init(){
    uint8_t sub_cmd = SUB_CMD_INIT;
    msg_Request(CMD_DB, &sub_cmd, 1);
}

void dB_deinit(){
    uint8_t sub_cmd = SUB_CMD_DEINIT;
    msg_Request(CMD_DB, &sub_cmd, 1);
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

QueueHandle_t dB_logic_get_queue(void){
    return dB_queue;
}