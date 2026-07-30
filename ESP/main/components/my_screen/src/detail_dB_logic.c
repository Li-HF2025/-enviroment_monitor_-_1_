#include "detail_dB_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report_dispatcher.h"
#include <string.h>
#include "screens/ui_main01.h"
#include "screens/ui_detialDB.h"

extern lv_obj_t * ui_dBNum;//main01的dB数值显示标签

#define DB_WINDOW_SIZE  200     // 滑动窗口大小：200条 × 150ms ≈ 30秒

static float latest_dB_value = 0.0f;
static volatile bool latest_dB_valid = false;

// 滑动窗口统计
static float db_window[DB_WINDOW_SIZE];
static int   db_window_idx = 0;
static int   db_window_count = 0;
static float db_avg_val = 0.0f;
static float db_min_val = 0.0f;
static float db_max_val = 0.0f;

QueueHandle_t dB_queue;

static void dB_task(void *arg){
    while(1){
        float dB_value;
        if(xQueueReceive(dB_queue, &dB_value, portMAX_DELAY) == pdTRUE){
            latest_dB_value = dB_value;
            if(dB_value <0.0f || dB_value >180.0f) continue; //错误数据过滤
            latest_dB_valid = true;

            // 滑动窗口：存入最新值
            db_window[db_window_idx] = dB_value;
            db_window_idx = (db_window_idx + 1) % DB_WINDOW_SIZE;
            if (db_window_count < DB_WINDOW_SIZE) db_window_count++;

            // 全量重算 avg/min/max（200条窗口一次性计算很快）
            float sum = 0.0f, vmin = db_window[0], vmax = db_window[0];
            for (int i = 0; i < db_window_count; i++) {
                float v = db_window[i];
                sum += v;
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
            db_avg_val = sum / (float)db_window_count;
            db_min_val = vmin;
            db_max_val = vmax;

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

float dB_get_avg(void)  { return db_avg_val; }
float dB_get_min(void)  { return db_min_val; }
float dB_get_max(void)  { return db_max_val; }

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