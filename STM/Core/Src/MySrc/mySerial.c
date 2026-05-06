#include "mySerial.h"
#include "OLED.h"
#define UART_TX_PIN GPIO_PIN_9
#define UART_RX_PIN GPIO_PIN_10
#define UART_RX_BUF    128// 接收缓冲区大小
#define UART_TX_BUF    4

static const osThreadAttr_t uart_rx_task_attr = {
    .name = "UART_RX_TASK",
    .stack_size = 128U * 4U,
    .priority = (osPriority_t)osPriorityHigh,
};

static const osThreadAttr_t uart_tx_task_attr = {
    .name = "UART_TX_TASK",
    .stack_size = 128U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal,
};

osMessageQueueId_t uart_rx_queue; // UART接收事件队列句柄
osMessageQueueId_t uart_tx_queue; // UART发送事件队列句柄

extern UART_HandleTypeDef huart1;

extern osMessageQueueId_t main_queue; // 主任务事件队列句柄

static uint8_t s_rx_byte=0; // UART接收单字节缓冲区

//开始帧和结束帧定义
#define PROTOCOL_SOF_1 0x55
#define PROTOCOL_SOF_2 0xAA
#define PROTOCOL_EOF_1 0x0D
#define PROTOCOL_EOF_2 0x0A

//协议版本定义(现在没用到，预留字段)
#define Ver 0x01



//计算校验和函数
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

static int protocol_send_frame(uint8_t msg_type,
                                     uint8_t cmd,
                                     uint16_t seq,
                                     const uint8_t *payload,
                                     uint16_t payload_len){
    if(payload_len > PROTOCOL_MAX_PAYLOAD) {
        return -1; // 数据长度超过最大载荷长度
    }
    if(payload_len > 0 && payload == NULL) {
        return -1; // 有数据但数据指针为NULL
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
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, frame, idx, HAL_MAX_DELAY);
    if(status != HAL_OK) {
        return -1; // 发送失败
    }
    return 0; // 发送成功
}

static void uart_tx_task(void *param){
    UartTxItem item;
    while(1){
        if(osMessageQueueGet(uart_tx_queue, &item, NULL, osWaitForever) == osOK){
            int result = protocol_send_frame(item.msg_type, item.cmd, item.seq, item.payload, item.payload_len);
            if(result != 0){
                msg_Report(CMD_TEST, (uint8_t*)"Send Err", 8);
            }
        }
    }
}

static void uart_rx_task(void *param){
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

    RxState state = RX_WAIT_SOF1;
    uint8_t current_byte = 0;
    ProtocolMessage parsed_msg = {0}; // 用于暂存正在解析的消息
    uint8_t payload_buf[PROTOCOL_MAX_PAYLOAD]; // Payload数据缓冲区
    uint16_t payload_index = 0;
    uint16_t received_checksum = 0;

    // 绑定Payload缓冲区到协议结构体
    parsed_msg.payload = payload_buf;

    while(1)
    {
        // 从队列中阻塞等待一个字节
        if (osMessageQueueGet(uart_rx_queue, &current_byte, NULL, osWaitForever) == osOK)
        {
            // 使用状态机处理接收到的字节
            switch (state)
            {
                case RX_WAIT_SOF1:
                    if (current_byte == PROTOCOL_SOF_1) {
                        state = RX_WAIT_SOF2;
                    }
                    break;

                case RX_WAIT_SOF2:
                    if (current_byte == PROTOCOL_SOF_2) {
                        state = RX_READ_VER;
                    } else {
                        state = RX_WAIT_SOF1; // 复位状态机
                    }
                    break;

                case RX_READ_VER:
                    parsed_msg.version = current_byte;
                    state = RX_READ_TYPE;
                    break;

                case RX_READ_TYPE:
                    parsed_msg.msg_type = current_byte;
                    state = RX_READ_CMD;
                    break;

                case RX_READ_CMD:
                    parsed_msg.cmd = current_byte;
                    state = RX_READ_SEQ_L;
                    break;

                case RX_READ_SEQ_L:
                    parsed_msg.seq = current_byte; // 先存低字节
                    state = RX_READ_SEQ_H;
                    break;

                case RX_READ_SEQ_H:
                    parsed_msg.seq |= ((uint16_t)current_byte << 8); // 再存高字节
                    state = RX_READ_LEN_L;
                    break;

                case RX_READ_LEN_L:
                    parsed_msg.payload_len = current_byte;
                    state = RX_READ_LEN_H;
                    break;

                case RX_READ_LEN_H:
                    parsed_msg.payload_len |= ((uint16_t)current_byte << 8);
                    // 检查载荷长度是否合法
                    if (parsed_msg.payload_len > PROTOCOL_MAX_PAYLOAD) {
                        state = RX_WAIT_SOF1; // 长度非法，复位
                    } else if (parsed_msg.payload_len == 0) {
                        state = RX_READ_CRC_L; // 无载荷，直接跳到校验
                    } else {
                        payload_index = 0;
                        state = RX_READ_PAYLOAD;
                    }
                    break;

                case RX_READ_PAYLOAD:
                    parsed_msg.payload[payload_index++] = current_byte;
                    if (payload_index >= parsed_msg.payload_len) {
                        state = RX_READ_CRC_L;
                    }
                    break;

                case RX_READ_CRC_L:
                    received_checksum = current_byte;
                    state = RX_READ_CRC_H;
                    break;

                case RX_READ_CRC_H:
                    received_checksum |= ((uint16_t)current_byte << 8);
                    
                    // 计算校验和并验证
                    uint16_t calculated_checksum = calculate_checksum_fields(
                        parsed_msg.version,
                        parsed_msg.msg_type,
                        parsed_msg.cmd,
                        parsed_msg.seq,
                        parsed_msg.payload,
                        parsed_msg.payload_len
                    );
                    
                    if (calculated_checksum == received_checksum) {
                        state = RX_READ_EOF1; // 校验通过，检查帧尾
                    } else {
                        state = RX_WAIT_SOF1; // 校验失败，复位
                    }
                    break;

                case RX_READ_EOF1:
                    if (current_byte == PROTOCOL_EOF_1) {
                        state = RX_READ_EOF2;
                    } else {
                        state = RX_WAIT_SOF1;
                    }
                    break;

                case RX_READ_EOF2:
                    if (current_byte == PROTOCOL_EOF_2) {
                        // 完整帧接收成功，处理消息
                        UartTxItem item;
                        item.msg_type = parsed_msg.msg_type;
                        item.cmd = parsed_msg.cmd;
                        item.payload_len = parsed_msg.payload_len;
                        item.seq = parsed_msg.seq;
                        if (item.payload_len > 0) {
                            memcpy(item.payload, parsed_msg.payload, item.payload_len);
                        }
                        msg_Report(item.cmd, item.payload, item.payload_len);
                        if(osMessageQueuePut(main_queue, &item, 0U, 15U) != osOK){
                            msg_Report(CMD_TEST, (uint8_t*)"Main Queue Err", 14);
                        }
                        // 处理完后，复位状态机以接收下一帧
                        state = RX_WAIT_SOF1;
                    } else {
                        state = RX_WAIT_SOF1;
                    }
                    break;

                default:
                    state = RX_WAIT_SOF1;
                    break;
            }
        }
    }
}

void mySerial_RTOS_Init(void)
{
    uart_rx_queue = osMessageQueueNew(UART_RX_BUF, sizeof(uint8_t), NULL);
    if(uart_rx_queue == NULL){
        // @TODO 创建队列失败处理
        OLED_WriteString(0, 0, "RX Queue Err");
    }
    uart_tx_queue = osMessageQueueNew(UART_TX_BUF, sizeof(UartTxItem), NULL);
    if(uart_tx_queue == NULL){
        // @TODO 创建队列失败处理
        OLED_WriteString(0, 0, "TX Queue Err");
    }
    osThreadNew(uart_rx_task, NULL, &uart_rx_task_attr);
    osThreadNew(uart_tx_task, NULL, &uart_tx_task_attr);
}

void mySerial_init(void)
{
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    msg_Report(CMD_TEST, (uint8_t*)"Serial Init OK", 14);
}

void msg_Report(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0; // 全局序列号，发送每条消息时递增
    UartTxItem tx_item;
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_REPORT;
    tx_item.seq = seq_num++;
    memcpy(tx_item.payload, payload, payload_len);
    tx_item.payload_len = payload_len;
    osMessageQueuePut(uart_tx_queue, &tx_item, 0U, 10U);
}

void msg_Response(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0; // 全局序列号，发送每条消息时递增
    UartTxItem tx_item;
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_RESPONSE;
    tx_item.seq = seq_num++;
    memcpy(tx_item.payload, payload, payload_len);
    tx_item.payload_len = payload_len;
    osMessageQueuePut(uart_tx_queue, &tx_item, 0U, 10U);
}

void msg_Request(uint8_t cmd, const uint8_t *payload, uint16_t payload_len){
    static uint16_t seq_num = 0; // 全局序列号，发送每条消息时递增
    UartTxItem tx_item;
    tx_item.cmd = cmd;
    tx_item.msg_type = MSG_TYPE_REQUEST;
    tx_item.seq = seq_num++;
    memcpy(tx_item.payload, payload, payload_len);
    tx_item.payload_len = payload_len;
    osMessageQueuePut(uart_tx_queue, &tx_item, 0U, 10U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
    if(huart->Instance == USART1){
        osMessageQueuePut(uart_rx_queue, &s_rx_byte, 0U, 0U);
        HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    }
}