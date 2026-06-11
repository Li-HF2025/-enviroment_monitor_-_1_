#ifndef MY_SERIAL_H
#define MY_SERIAL_H
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include <string.h>
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
    CMD_DB = 0x02, // 分贝数据
    CMD_LIGHT = 0x03 // 光照数据
}CMDType;

typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;   // 0x01=温度, 0x02=分贝, 0x03=光照, 0x04=湿度
    uint8_t  status;        // bit0=数据有效, bit1=超量程, bit2=传感器故障
    int16_t  value_x10;     // 数值 × 10（有符号，支持负温度）
    uint16_t reserved;      // 预留扩展
} SensorDataBin;  // 固定 6 字节

typedef enum {
    SUB_CMD_INIT    = 0x01,  // 初始化/使能传感器
    SUB_CMD_DEINIT  = 0x02,  // 反初始化/停用传感器
    SUB_CMD_STATUS  = 0x03,  // 查询传感器状态
} ProtocolSubCmd;

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
void mySerial_init(void);
void mySerial_RTOS_Init(void);
void msg_Report(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
void msg_Response(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
void msg_Request(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);
void sensor_report(uint8_t cmd,uint8_t sensor_type,int16_t value_x10,uint8_t status);
#endif