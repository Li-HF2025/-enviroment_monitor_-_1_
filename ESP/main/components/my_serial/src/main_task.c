#include "main_task.h"
#include "my_serial.h"
#include "esp_log.h"
#include "detail_dB_logic.h"
#include "detail_temp_logic.h"
#include "string.h"
#define MAIN_QUEUE_SIZE 20
static const char *TAG = "MAIN_TASK";
QueueHandle_t main_queue;

static void mian_task(void *arg){
    UartTxItem item;
    while(1){
        if(xQueueReceive(main_queue, &item, portMAX_DELAY) == pdTRUE){
            char payload[PROTOCOL_MAX_PAYLOAD + 1];
            memcpy(payload, item.payload, item.payload_len);
            payload[item.payload_len] = '\0';

            // 统一 SensorDataBin 解析（仅响应/上报类型的传感器命令）
            if ((item.cmd == CMD_DB || item.cmd == CMD_TEMPERATURE || item.cmd == CMD_LIGHT)
                && item.payload_len >= sizeof(SensorDataBin)) {
                SensorDataBin sensor;
                memcpy(&sensor, item.payload, sizeof(SensorDataBin));

                if (sensor.status & 0x04) {
                    ESP_LOGW(TAG, "传感器故障: type=0x%02X", sensor.sensor_type);
                    continue;
                }

                float value = sensor.value_x10 / 10.0f;

                if (sensor.sensor_type == 0x02) {
                    if (dB_logic_get_queue() != NULL) {
                        xQueueSend(dB_logic_get_queue(), &value, pdMS_TO_TICKS(10));
                    }
                } else if (sensor.sensor_type == 0x01 || sensor.sensor_type == 0x04) {
                    UartTxItem temp_item;
                    temp_item.cmd = CMD_TEMPERATURE;
                    temp_item.msg_type = item.msg_type;
                    temp_item.payload_len = sizeof(SensorDataBin);
                    memcpy(temp_item.payload, &sensor, sizeof(SensorDataBin));
                    if (temp_logic_get_queue() != NULL) {
                        xQueueSend(temp_logic_get_queue(), &temp_item, pdMS_TO_TICKS(10));
                    }
                }

                continue;
            }

            // 以下为旧格式兼容（子命令码等非传感器帧）
            if(item.msg_type == MSG_TYPE_REQUEST){
                continue;
            }
        }
    }
}
void main_task_init(){
    main_queue = xQueueCreate(MAIN_QUEUE_SIZE, sizeof(UartTxItem));
    if(main_queue == NULL){
        ESP_LOGW(TAG, "主任务队列创建失败");
        return;
    }
    xTaskCreate(mian_task, "main_task", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "主任务初始化完成");
}