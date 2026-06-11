#include "detail_light_logic.h"
#include "ui.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report_dispatcher.h"
static float latest_light_value = 0.0f;
static volatile bool latest_light_valid = false;

QueueHandle_t light_queue;

static void light_task(void *arg){
    while(1){
        float light_value;
        if(xQueueReceive(light_queue, &light_value, portMAX_DELAY) == pdTRUE){
            latest_light_value = light_value;
            latest_light_valid = true;
            esp_event_post(SENSOR_EVENT_BASE, SENSOR_LIGHT_UPDATED, &light_value, sizeof(float), 0);
        }
    }
}

void light_init(void){
    uint8_t sub_cmd = SUB_CMD_INIT;
    msg_Request(CMD_LIGHT, &sub_cmd, 1);
}

void light_deinit(void){
    uint8_t sub_cmd = SUB_CMD_DEINIT;
    msg_Request(CMD_LIGHT, &sub_cmd, 1);
}

bool light_get_latest_valid(void)
{
    return latest_light_valid;
}

float light_get_latest_value(void)
{
    return latest_light_value;
}

void light_start(void){
    static bool started = false;
    if(started){
        light_init();
        return;
    }
    started = true;

    light_queue = xQueueCreate(10, sizeof(float));
    xTaskCreate(light_task, "light_task", 2048, NULL, 10, NULL);
    light_init();
}
