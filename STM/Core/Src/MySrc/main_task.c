#include "main_task.h"
#include "string.h"
#include <stdio.h>
#include "OLED.h"
#include "db.h"
#include "temperature.h"
#define MAIN_QUEUE_SIZE 4

osMessageQueueId_t main_queue;
osThreadId_t mainTaskHandle;

extern osMessageQueueId_t uart_tx_queue; // UART发送事件队列句柄

static const osThreadAttr_t main_task_attr = {
    .name = "MainTask",
    .stack_size = 128U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal,
};

static void display_value(UartTxItem item)
{
    char req_buf[PROTOCOL_MAX_PAYLOAD + 1];
    char line[22];
    char line_hex[22];
    uint16_t req_len = item.payload_len;

    if (req_len > PROTOCOL_MAX_PAYLOAD) {
        req_len = PROTOCOL_MAX_PAYLOAD;
    }
    memcpy(req_buf, item.payload, req_len);
    req_buf[req_len] = '\0';

    while (req_len > 0 &&
        (req_buf[req_len - 1] == '\r' || req_buf[req_len - 1] == '\n' || req_buf[req_len - 1] == ' ')) {
        req_buf[--req_len] = '\0';
    }

    line_hex[0] = '\0';
    if (req_len > 0) {
        size_t off = 0;
        uint16_t show_len = (req_len > 7) ? 7 : req_len;
        for (uint16_t i = 0; i < show_len; i++) {
            int wrote = snprintf(line_hex + off, sizeof(line_hex) - off,
                                (i + 1 == show_len) ? "%02X" : "%02X ",
                                (unsigned)req_buf[i] & 0xFFU);
            if (wrote <= 0) {
                break;
            }
            off += (size_t)wrote;
            if (off >= sizeof(line_hex) - 1) {
                break;
            }
        }
    }

    snprintf(line, sizeof(line), "A:%s", req_buf);
    OLED_WriteString(2, 0, line_hex);
    OLED_WriteString(3, 0, line);
}

void main_task(void *pvParameters){
    UartTxItem item;
    while(1){
        if(osMessageQueueGet(main_queue, &item, NULL, osWaitForever) == osOK){    
            if(item.msg_type == MSG_TYPE_REQUEST){
                char buf[PROTOCOL_MAX_PAYLOAD + 1];
                uint16_t copy_len = item.payload_len;
                if (copy_len > PROTOCOL_MAX_PAYLOAD) {
                    copy_len = PROTOCOL_MAX_PAYLOAD;
                }
                memcpy(buf, item.payload, copy_len);
                buf[copy_len] = '\0';
                while (copy_len > 0 &&
                       (buf[copy_len - 1] == '\r' || buf[copy_len - 1] == '\n' || buf[copy_len - 1] == ' ' || buf[copy_len - 1] == '\t')) {
                    buf[--copy_len] = '\0';
                }
                while (buf[0] == ' ' || buf[0] == '\t') {
                    memmove(buf, buf + 1, copy_len--);
                }
                switch (item.cmd)
                {
                case CMD_TEST:
                    osMessageQueuePut(uart_tx_queue, &item, 0U, 10U);
                    OLED_WriteString(1,0,"Test Cmd Recv");
                    break;
                case CMD_DB:
                    if(strcmp(buf, "DB Init") == 0){
                        DB_Init();
                        msg_Report(CMD_DB, (uint8_t*)"DB Init OK", 10);
                    } else if(strcmp(buf, "DB DeInit") == 0){
                        DB_DeInit();
                        msg_Report(CMD_DB, (uint8_t*)"DB DeInit OK", 12);
                    }else{
                        msg_Report(CMD_DB, (uint8_t*)"Invalid DB Cmd", 14);
                    }
                    break;
                case CMD_TEMPERATURE:
                    if(strcmp(buf, "DHT22 Init") == 0){
                        DHT22_Init();
                        msg_Report(CMD_TEMPERATURE, (uint8_t*)"DHT22 Init OK", 14);
                    } else if(strcmp(buf, "DHT22 DeInit") == 0){
                        DHT22_DeInit();
                        msg_Report(CMD_TEMPERATURE, (uint8_t*)"DHT22 DeInit OK", 15);
                    } else {
                        msg_Report(CMD_TEMPERATURE, (uint8_t*)"Invalid Temp Cmd", 16);
                    }
                    break;
                default:
                    break;
                }
                
            }
        }
    }
}
void main_task_RTOS_Init(void){
    main_queue = osMessageQueueNew(MAIN_QUEUE_SIZE, sizeof(UartTxItem), NULL);
    if(main_queue == NULL){
        OLED_WriteString(2, 0, "Main Queue Err");
    }

    mainTaskHandle = osThreadNew(main_task, NULL, &main_task_attr);
    if(mainTaskHandle == NULL){
        OLED_WriteString(3, 0, "Main Task Err");
    }
}