#ifndef __MY_SERIAL_H__
#define __MY_SERIAL_H__

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PROTOCOL_MAX_PAYLOAD 128// 协议最大载荷长度

//协议消息类型定义
typedef enum {
    MSG_TYPE_REQUEST  = 0,  // 请求
    MSG_TYPE_RESPONSE = 1,  // 响应
    MSG_TYPE_REPORT   = 2   // 上报
} ProtocolMsgType;

//协议命令定义
typedef enum {
    CMD_TEST= 0xAA, // 测试命令
    CMD_TEMPERATURE = 0x01, // 温度数据
    CMD_DB = 0x02, // 声强数据
    CMD_LIGHT = 0x03, // 光照数据
    CMD_VERSION = 0x04, // 固件版本查询 (STM32 OTA)
}CMDType;

typedef struct {
    uint8_t sof[2]; // 开始帧
    uint8_t version; // 协议版本
    uint8_t msg_type; // 消息类型
    uint8_t cmd; // 命令
    uint16_t seq; // 序列号
    uint16_t payload_len; // 数据长度
    uint8_t *payload; // 数据内容
    uint16_t checksum; // 校验和
    uint8_t eof[2]; // 结束帧
} ProtocolMessage;


typedef struct {
    uint8_t msg_type;
    uint8_t cmd;
    uint16_t seq;
    uint16_t payload_len;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} UartTxItem;

// UART 任务句柄 — 供 my_stm_ota 在 AN3155 烧写期间挂起/恢复
extern TaskHandle_t uart_rx_task_handle;
extern TaskHandle_t uart_tx_task_handle;

void uart_init();
void msg_Report(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
void msg_Response(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
void msg_Request(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
#endif /* __MY_SERIAL_H__ */