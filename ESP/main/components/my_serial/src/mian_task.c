#include "main_task.h"
#include "my_serial.h"
#include "esp_log.h"
#define MAIN_QUEUE_SIZE 20
static const char *TAG = "MAIN_TASK";
QueueHandle_t main_queue;

extern QueueHandle_t uart_tx_queue; // UART发送事件队列句柄
extern QueueHandle_t dB_queue; // 声强数据队列句柄
extern QueueHandle_t temp_queue; // 温湿度数据队列句柄

static void mian_task(void *arg){
    UartTxItem item;
    BaseType_t status;
    while(1){
        if(xQueueReceive(main_queue, &item, portMAX_DELAY) == pdTRUE){
            char *payload = (char *)malloc(item.payload_len + 1);
            if(payload == NULL){
                ESP_LOGW(TAG, "payload malloc failed");
                continue;
            }

            memcpy(payload, item.payload, item.payload_len);
            payload[item.payload_len] = '\0';

            if(item.msg_type == MSG_TYPE_REQUEST){
                free(payload);
                continue;
            }else if(item.cmd == CMD_TEMPERATURE){
                if(temp_queue != NULL){
                    xQueueSend(temp_queue, &item, pdMS_TO_TICKS(10));
                }
            }else if(item.msg_type == MSG_TYPE_RESPONSE){
                if(item.cmd == CMD_DB){
                    float dB_value = atof(payload);
                    if(dB_queue != NULL){
                        xQueueSend(dB_queue, &dB_value, pdMS_TO_TICKS(10));
                    }
                }
            }else if(item.msg_type == MSG_TYPE_REPORT){
                ESP_LOGI(TAG, "收到上报消息: cmd=0x%02X, payload=%s", item.cmd, payload);
                if(item.cmd == CMD_DB){
                    float dB_value = atof(payload);
                    if(dB_queue != NULL){
                        xQueueSend(dB_queue, &dB_value, pdMS_TO_TICKS(10));
                    }
                }
            }

            free(payload);
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