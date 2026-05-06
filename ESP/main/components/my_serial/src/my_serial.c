#include "my_serial.h"
#include <string.h>

#define UART_PORT      UART_NUM_1
#define UART_BAUDRATE  115200
#define UART_TX_PIN    17
#define UART_RX_PIN    16
#define UART_RX_BUF    1024
#define UART_TX_BUF    1024
QueueHandle_t uart_rx_queue; // UART接收事件队列句柄
QueueHandle_t uart_tx_queue; // UART发送事件队列句柄
static const char *TAG = "MY_SERIAL";

//开始帧和结束帧定义
#define PROTOCOL_SOF_1 0x55
#define PROTOCOL_SOF_2 0xAA
#define PROTOCOL_EOF_1 0x0D
#define PROTOCOL_EOF_2 0x0A

extern QueueHandle_t main_queue;


//协议版本定义(现在没用到，预留字段)
#define Ver 0x01



static uint16_t calculate_checksum_fields(uint8_t version,
                                          uint8_t msg_type,
                                          uint8_t cmd,
                                          uint16_t seq,
                                          const uint8_t *payload,
                                          uint16_t payload_len) {
    uint16_t checksum = 0;
    checksum += version;
    checksum += msg_type;
    checksum += cmd;
    checksum += (seq & 0xFF) + ((seq >> 8) & 0xFF);
    checksum += (payload_len & 0xFF) + ((payload_len >> 8) & 0xFF);
    for (uint16_t i = 0; i < payload_len; i++) {
        checksum += payload[i];
    }
    return checksum;
}

static esp_err_t protocol_send_frame(uint8_t msg_type,
                                     uint8_t cmd,
                                     uint16_t seq,
                                     const uint8_t *payload,
                                     uint16_t payload_len){
    if(payload_len > 0 && payload == NULL){
        ESP_LOGW(TAG, "payload长度大于0，但payload指针为NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if(payload_len > PROTOCOL_MAX_PAYLOAD){
        ESP_LOGW(TAG, "payload长度超过协议最大载荷长度");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t frame[2+1+1+1+2+2+PROTOCOL_MAX_PAYLOAD+2+2]; // 最大帧长度
    uint8_t idx = 0;
    uint16_t checksum = 0;// 校验和计算
    frame[idx++] = PROTOCOL_SOF_1;
    frame[idx++] = PROTOCOL_SOF_2;
    frame[idx++] = Ver;
    frame[idx++] = msg_type;
    frame[idx++] = cmd;
    frame[idx++] = seq & 0xFF;
    frame[idx++] = (seq >> 8) & 0xFF;
    frame[idx++] = payload_len & 0xFF;
    frame[idx++] = (payload_len >> 8) & 0xFF;
    if(payload_len > 0){
        memcpy(&frame[idx], payload, payload_len);
        idx += payload_len;
    }
    checksum = calculate_checksum_fields(Ver, msg_type, cmd, seq, payload, payload_len);
    frame[idx++] = checksum & 0xFF;
    frame[idx++] = (checksum >> 8) & 0xFF;
    frame[idx++] = PROTOCOL_EOF_1;
    frame[idx++] = PROTOCOL_EOF_2;
    int written = uart_write_bytes(UART_PORT, (const char *)frame, idx);
    if (written != idx) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief 串口接收任务（核心：UART事件驱动 + 协议帧状态机解析）
 * 功能：监听串口事件，收到数据后按【自定义协议格式】逐字节解析
 * 协议帧：SOF(2) + Ver(1) + Type(1) + Cmd(1) + Seq(2) + Len(2) + Payload(N) + CRC(2) + EOF(2)
 * @param arg 任务入参（未使用）
 */
static void uart_rx_task(void *arg)
{
    // ===================== 1. 定义协议解析状态机枚举 =====================
    // 状态机：按协议帧顺序，逐字节匹配解析，一个状态对应一个字段
    typedef enum {
        RX_WAIT_SOF1 = 0,      // 等待帧头第1字节 0x55
        RX_WAIT_SOF2,          // 等待帧头第2字节 0xAA
        RX_READ_VER,           // 读取协议版本号 Ver
        RX_READ_TYPE,          // 读取消息类型 MsgType
        RX_READ_CMD,           // 读取命令字 Cmd
        RX_READ_SEQ_L,         // 读取序列号 低字节（大端序）
        RX_READ_SEQ_H,         // 读取序列号 高字节（大端序）
        RX_READ_LEN_L,         // 读取Payload长度 低字节（大端序）
        RX_READ_LEN_H,         // 读取Payload长度 高字节（大端序）
        RX_READ_PAYLOAD,       // 读取有效数据 Payload
        RX_READ_CRC_L,         // 读取CRC校验值 低字节
        RX_READ_CRC_H,         // 读取CRC校验值 高字节
        RX_READ_EOF1,          // 等待帧尾第1字节 0x0D
        RX_READ_EOF2           // 等待帧尾第2字节 0x0A
    } RxState;

    // ===================== 2. 定义局部变量 =====================
    uart_event_t event;                // 串口事件结构体（存储UART事件）
    uint8_t rx_buf[128];               // 串口临时接收缓冲区（存储单次读取的字节）

    RxState st = RX_WAIT_SOF1;         // 状态机初始状态：等待帧头SOF1
    ProtocolMessage msg = {0};         // 协议帧结构体（存储解析完成的一帧数据）
    uint8_t payload_buf[PROTOCOL_MAX_PAYLOAD]; // Payload数据缓冲区
    uint16_t payload_idx = 0;          // Payload数据接收索引（记录收到第几个字节）
    uint16_t rx_crc = 0;               // 存储接收到的CRC校验值

    // 绑定Payload缓冲区到协议结构体
    msg.payload = payload_buf;

    // ===================== 3. 任务死循环：持续监听串口事件 =====================
    while (1) {
        // 阻塞等待：从串口事件队列获取事件（无事件则休眠，不占用CPU）
        if (xQueueReceive(uart_rx_queue, &event, portMAX_DELAY)) {
            // ============== 事件1：串口收到有效数据（核心解析逻辑） ==============
            if (event.type == UART_DATA) {
                // 从串口硬件读取数据，最多读128字节，超时20ms
                int read_len = uart_read_bytes(UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(20));
                
                // 逐字节解析收到的数据（状态机核心：一个字节一个状态流转）
                for (int i = 0; i < read_len; i++) {
                    uint8_t b = rx_buf[i];  // 当前待解析的字节

                    // ===================== 状态机：按协议顺序解析 =====================
                    switch (st) {
                        // 状态1：等待帧头第1字节 0x55
                        case RX_WAIT_SOF1:
                            if (b == PROTOCOL_SOF_1) {
                                // 匹配到SOF1，切换状态：等待SOF2
                                st = RX_WAIT_SOF2;
                            }
                            break;

                        // 状态2：等待帧头第2字节 0xAA
                        case RX_WAIT_SOF2:
                            if (b == PROTOCOL_SOF_2) {
                                // 匹配到完整帧头，切换状态：读取版本号
                                st = RX_READ_VER;
                            } else {
                                // 匹配失败，重置状态机
                                st = RX_WAIT_SOF1;
                            }
                            break;

                        // 状态3：读取协议版本号 Ver
                        case RX_READ_VER:
                            msg.version = b;          // 保存版本号
                            st = RX_READ_TYPE;        // 切换状态：读取消息类型
                            break;

                        // 状态4：读取消息类型 MsgType
                        case RX_READ_TYPE:
                            msg.msg_type = b;         // 保存消息类型(0请求/1响应/2上报)
                            st = RX_READ_CMD;         // 切换状态：读取命令字
                            break;

                        // 状态5：读取命令字 Cmd
                        case RX_READ_CMD:
                            msg.cmd = b;              // 保存命令字
                            st = RX_READ_SEQ_L;       // 切换状态：读取序列号低字节
                            break;

                        // 状态6：读取序列号 低字节（大端序：低字节先传输）
                        case RX_READ_SEQ_L:
                            msg.seq = b;              // 低字节存入seq低8位
                            st = RX_READ_SEQ_H;       // 切换状态：读取序列号高字节
                            break;

                        // 状态7：读取序列号 高字节
                        case RX_READ_SEQ_H:
                            msg.seq |= ((uint16_t)b << 8);  // 高字节左移8位，拼接成16位序列号
                            st = RX_READ_LEN_L;       // 切换状态：读取Payload长度低字节
                            break;

                        // 状态8：读取Payload长度 低字节
                        case RX_READ_LEN_L:
                            msg.payload_len = b;      // 低字节存入长度低8位
                            st = RX_READ_LEN_H;       // 切换状态：读取Payload长度高字节
                            break;

                        // 状态9：读取Payload长度 高字节
                        case RX_READ_LEN_H:
                            msg.payload_len |= ((uint16_t)b << 8);  // 拼接成16位Payload长度
                            
                            // 长度合法性校验
                            if (msg.payload_len > PROTOCOL_MAX_PAYLOAD) {
                                st = RX_WAIT_SOF1;    // 长度超限，重置状态机
                            } else if (msg.payload_len == 0) {
                                st = RX_READ_CRC_L;    // 无Payload，直接读取CRC
                            } else {
                                payload_idx = 0;       // 重置Payload索引
                                st = RX_READ_PAYLOAD;  // 切换状态：读取Payload
                            }
                            break;

                        // 状态10：读取Payload有效数据
                        case RX_READ_PAYLOAD:
                            msg.payload[payload_idx++] = b;  // 逐字节存储Payload
                            // 接收完所有Payload，切换状态：读取CRC
                            if (payload_idx >= msg.payload_len) {
                                st = RX_READ_CRC_L;
                            }
                            break;

                        // 状态11：读取CRC校验值 低字节
                        case RX_READ_CRC_L:
                            rx_crc = b;                // 保存CRC低字节
                            st = RX_READ_CRC_H;        // 切换状态：读取CRC高字节
                            break;

                        // 状态12：读取CRC校验值 高字节
                        case RX_READ_CRC_H:
                            rx_crc |= ((uint16_t)b << 8);  // 拼接成16位CRC值
                            st = RX_READ_EOF1;         // 切换状态：等待帧尾EOF1
                            break;

                        // 状态13：等待帧尾第1字节 0x0D
                        case RX_READ_EOF1:
                            if (b == PROTOCOL_EOF_1) {
                                st = RX_READ_EOF2;     // 匹配到EOF1，等待EOF2
                            } else {
                                st = RX_WAIT_SOF1;     // 匹配失败，重置状态机
                            }
                            break;

                        // 状态14：等待帧尾第2字节 0x0A（一帧解析完成）
                        case RX_READ_EOF2:
                            if (b == PROTOCOL_EOF_2) {
                                // 1. 本地重新计算CRC校验值
                                uint16_t local_crc = calculate_checksum_fields(
                                    msg.version, msg.msg_type, msg.cmd, msg.seq, msg.payload, msg.payload_len);
                                // 2. CRC校验：对比本地计算值和接收值
                                if (local_crc == rx_crc) {
                                    UartTxItem item;
                                    item.msg_type = msg.msg_type;
                                    item.cmd = msg.cmd;
                                    item.seq = msg.seq;
                                    item.payload_len = msg.payload_len;
                                    memcpy(item.payload, msg.payload, msg.payload_len);
                                    xQueueSend(main_queue, &item, pdMS_TO_TICKS(20)); // 将解析完成的消息发送到主任务队列
                                } else {
                                    // 校验失败：打印CRC错误
                                    ESP_LOGW(TAG, "RX crc mismatch local=0x%04X recv=0x%04X", local_crc, rx_crc);
                                }
                            }
                            // 一帧解析完成，无论成功失败，重置状态机，等待下一帧
                            st = RX_WAIT_SOF1;
                            break;

                        // 默认状态：异常，重置状态机
                        default:
                            st = RX_WAIT_SOF1;
                            break;
                    }
                }
            }
            // ============== 事件2：串口FIFO/缓冲区溢出（异常处理） ==============
            else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                uart_flush_input(UART_PORT);      // 清空串口输入缓冲区
                xQueueReset(uart_rx_queue);       // 重置串口事件队列
                ESP_LOGW(TAG, "UART overflow, reset queue");
            }
            // ============== 事件3：串口帧错误/校验错误（异常处理） ==============
            else if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
                ESP_LOGW(TAG, "UART frame/parity error");
            }
        }
    }
}

static void uart_tx_task(void *arg){
    UartTxItem item;
    while(1){
        if(xQueueReceive(uart_tx_queue, &item, portMAX_DELAY) == pdTRUE){
            esp_err_t err = protocol_send_frame(item.msg_type, item.cmd, item.seq, item.payload, item.payload_len);// 发送协议帧
            if(err != ESP_OK){
                ESP_LOGW(TAG, "协议帧发送失败, 错误码:%d", err);
            }else{
                ESP_LOGI(TAG, "协议帧发送成功, msg_type:%d, cmd:%d, seq:%d, payload_len:%d", item.msg_type, item.cmd, item.seq, item.payload_len);
            }
        }
    }
}
void uart_send_test(){
    UartTxItem item = {
        .msg_type = MSG_TYPE_REQUEST,
        .cmd = CMD_TEST,
        .seq = 1,
        .payload_len = 0
    };
    xQueueSend(uart_tx_queue, &item, portMAX_DELAY); // 将测试消息发送到UART发送队列
}

void uart_init(){
    uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    // 配置UART参数
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    // 设置UART引脚
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // 安装UART驱动
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF, UART_TX_BUF, 5, &uart_rx_queue, 0));

    uart_tx_queue = xQueueCreate(5, sizeof(UartTxItem)); // 创建UART发送队列
    xTaskCreate(uart_rx_task, "uart_event_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL);// 创建UART接收任务
    xTaskCreate(uart_tx_task, "uart_tx_task", 3072, NULL, configMAX_PRIORITIES - 2, NULL);// 创建UART发送任务
    ESP_LOGI(TAG, "UART初始化完成");

    uart_send_test(); // 发送测试消息，验证UART通信是否正常
}

void msg_Report(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0;
    UartTxItem tx_item = {0};
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_REPORT;
    tx_item.seq = seq_num++;
    tx_item.payload_len = payload_len;
    memcpy(tx_item.payload, payload, payload_len);
    xQueueSend(uart_tx_queue, &tx_item, pdMS_TO_TICKS(10));
}

void msg_Response(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0; // 全局序列号，发送每条消息时递增
    UartTxItem tx_item;
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_RESPONSE;
    tx_item.seq = seq_num++;
    memcpy(tx_item.payload, payload, payload_len);
    tx_item.payload_len = payload_len;
    xQueueSend(uart_tx_queue, &tx_item, pdMS_TO_TICKS(10));
}

void msg_Request(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0; // 全局序列号，发送每条消息时递增
    UartTxItem tx_item;
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_REQUEST;
    tx_item.seq = seq_num++;
    memcpy(tx_item.payload, payload, payload_len);
    tx_item.payload_len = payload_len;
    xQueueSend(uart_tx_queue, &tx_item, pdMS_TO_TICKS(10));
}


