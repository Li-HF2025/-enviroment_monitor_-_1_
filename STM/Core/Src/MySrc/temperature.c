#include "temperature.h"
#include "OLED.h"
#include "mySerial.h"
#include "main.h"

#include <stdio.h>
#include <stdbool.h>

#define DHT22_PORT GPIOB
#define DHT22_PIN  GPIO_PIN_0

static uint32_t g_us_ticks_per_loop = 0;
static int8_t g_dht22_last_error = 0;
static volatile bool g_dht22_enabled = false;

enum {
    DHT22_OK = 0,
    DHT22_ERR_ACK_TIMEOUT = -1,
    DHT22_ERR_BIT_TIMEOUT = -2,
    DHT22_ERR_CHECKSUM = -3,
};
osThreadAttr_t dht22_task_attr = {
    .name = "DHT22Task",
    .stack_size = 128 * 4,
    .priority = (osPriority_t) osPriorityAboveNormal,
};

static void DHT22_Pin_Output(void) {
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = DHT22_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_PORT, &gpio_init);
}

static void DHT22_Pin_Input(void) {
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = DHT22_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_PORT, &gpio_init);
}


// 微秒级延时
void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * g_us_ticks_per_loop;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
    }
}

void DHT22_Send_Start(void) {
    DHT22_Pin_Output();
    // 拉低总线18ms（官方推荐值，确保传感器唤醒）
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_RESET);
    delay_us(18000);
    
    // 释放总线（拉高），等待传感器响应
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    DHT22_Pin_Input();
    delay_us(40);
}

int8_t DHT22_Wait_Ack(void) {
    uint32_t timeout = 0;
    uint32_t timeout_limit = 300U;
    
    // 等待传感器拉低总线（应答开始）
    while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) == GPIO_PIN_SET) {
        delay_us(1);
        if (++timeout > timeout_limit) return DHT22_ERR_ACK_TIMEOUT; // 超时无应答
    }
    
    timeout = 0;
    // 等待传感器释放总线（应答结束）
    while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) == GPIO_PIN_RESET) {
        delay_us(1);
        if (++timeout > timeout_limit) return DHT22_ERR_ACK_TIMEOUT;
    }
    
    timeout = 0;
    // 等待传感器再次拉低总线（准备发送数据）
    while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) == GPIO_PIN_SET) {
        delay_us(1);
        if (++timeout > timeout_limit) return DHT22_ERR_ACK_TIMEOUT;
    }
    
    return 0;
}

int8_t DHT22_Read_Bit(void) {
    uint32_t timeout_cycles = g_us_ticks_per_loop * 120U;
    uint32_t start = DWT->CYCCNT;
    
    // 等待低电平结束（每一位的起始标志）
    while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) == GPIO_PIN_RESET) {
        if ((uint32_t)(DWT->CYCCNT - start) > timeout_cycles) {
            return DHT22_ERR_BIT_TIMEOUT;
        }
    }
    
    // 测量高电平持续时间
    start = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(DHT22_PORT, DHT22_PIN) == GPIO_PIN_SET) {
        if ((uint32_t)(DWT->CYCCNT - start) > timeout_cycles) {
            return DHT22_ERR_BIT_TIMEOUT; // 超时
        }
    }
    
    // 高电平>40μs判定为1（留余量避免误判）
    return ((uint32_t)(DWT->CYCCNT - start) > (g_us_ticks_per_loop * 40U)) ? 1 : 0;
}

int16_t DHT22_Read_Byte(void) {
    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        int8_t bit = DHT22_Read_Bit();
        if (bit < 0) return -1;
        byte = (byte << 1) | bit;
    }
    return byte;
}

int8_t DHT22_Read_Raw(uint8_t data[5]) {
    int8_t status = DHT22_OK;

    // 发送起始信号
    DHT22_Send_Start();

    __disable_irq();

    do {
        // 等待应答
        status = DHT22_Wait_Ack();
        if (status != DHT22_OK) {
            break;
        }

        // 读取5个字节
        for (uint8_t i = 0; i < 5; i++) {
            int16_t byte = DHT22_Read_Byte();
            if (byte < 0) {
                status = DHT22_ERR_BIT_TIMEOUT;
                break;
            }
            data[i] = (uint8_t)byte;
        }
    } while (0);

    __enable_irq();

    if (status != DHT22_OK) {
        return status;
    }
    
    return 0;
}

int8_t DHT22_Parse_Data(uint8_t data[5], float *temperature, float *humidity) {
    // 1. 验证校验和
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (checksum != data[4]) {
        return DHT22_ERR_CHECKSUM; // 校验失败，数据无效
    }
    
    // 2. 解析湿度
    uint16_t hum_raw = (data[0] << 8) | data[1];
    *humidity = (float)hum_raw / 10.0f;
    
    // 3. 解析温度（严格按照官方符号位+绝对值格式）
    uint16_t temp_raw = (data[2] << 8) | data[3];
    if (temp_raw & 0x8000) {
        *temperature = -(float)(temp_raw & 0x7FFF) / 10.0f;
    } else {
        *temperature = (float)temp_raw / 10.0f;
    }
    
    return 0; // 解析成功
}

void DHT22_Read(float *temperature, float *humidity) {
    uint8_t data[5];
    g_dht22_last_error = DHT22_OK;
    *temperature = DHT22_ERROR_TEMP;
    *humidity = DHT22_ERROR_HUMI;
    int8_t raw_status = DHT22_Read_Raw(data);
    if (raw_status == DHT22_OK) {
        int8_t parse_status = DHT22_Parse_Data(data, temperature, humidity);
        if (parse_status != DHT22_OK) {
            g_dht22_last_error = parse_status;
            *temperature = DHT22_ERROR_TEMP;
            *humidity = DHT22_ERROR_HUMI;
        }
    } else {
        g_dht22_last_error = raw_status;
        *temperature = DHT22_ERROR_TEMP;
        *humidity = DHT22_ERROR_HUMI;
    }
}

void DHT22_Send_Report(float temperature, float humidity) {
    int16_t temp10 = (int16_t)(temperature * 10.0f + (temperature >= 0.0f ? 0.5f : -0.5f));
    uint16_t hum10 = (uint16_t)(humidity * 10.0f + 0.5f);

    sensor_report(CMD_TEMPERATURE, 0x01, temp10, 0x00);
    sensor_report(CMD_TEMPERATURE, 0x04, hum10, 0x00);
    
}

void DHT22_Task(void *argument) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (g_dht22_enabled) {
            OLED_WriteString(0, 0, "Temp:       ");
            OLED_WriteString(1, 0, "Hum:        ");
            float temperature, humidity;
            DHT22_Read(&temperature, &humidity);

            if (temperature > DHT22_VALID_TEMP_MIN && humidity >= DHT22_VALID_HUMI_MIN) {
                char temp_str[24], hum_str[24];
                int temp10 = (int)(temperature * 10.0f + (temperature >= 0.0f ? 0.5f : -0.5f));
                int hum10 = (int)(humidity * 10.0f + 0.5f);
                int temp_int = temp10 / 10;
                int temp_frac = temp10 < 0 ? -(temp10 % 10) : (temp10 % 10);
                int hum_int = hum10 / 10;
                int hum_frac = hum10 % 10;

                snprintf(temp_str, sizeof(temp_str), "Temp: %d.%dC", temp_int, temp_frac);
                snprintf(hum_str, sizeof(hum_str), "Hum: %d.%d%%", hum_int, hum_frac);
                OLED_WriteString(0, 0, temp_str);
                OLED_WriteString(1, 0, hum_str);
                DHT22_Send_Report(temperature, humidity);

            } else {
                char err_str[24];
                snprintf(err_str, sizeof(err_str), "ERR:%d", (int)g_dht22_last_error);
                OLED_WriteString(0, 0, err_str);
                OLED_WriteString(1, 0, err_str);
                // 单次读取失败不报告故障，避免误触发 ESP32 端告警
            }

            osDelay(2000);
        }
    }
}

static TaskHandle_t dht22TaskHandle = NULL;

void DHT22_RTOS_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_us_ticks_per_loop = SystemCoreClock / 1000000U;
    if (g_us_ticks_per_loop == 0U) {
        g_us_ticks_per_loop = 1U;
    }
    DHT22_Pin_Output();
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    dht22TaskHandle = osThreadNew(DHT22_Task, NULL, &dht22_task_attr);
    if(dht22TaskHandle == NULL) {
        OLED_WriteString(4, 0, "DHT22 Task Err");
    }
    g_dht22_enabled = true;
    xTaskNotifyGive(dht22TaskHandle);   // 默认自启，确保重启后不会卡死
}

void DHT22_Init(void) {
    // 如果已运行，不重复初始化
    if (g_dht22_enabled) return;
    DHT22_Pin_Output();
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    g_dht22_enabled = true;
    xTaskNotifyGive(dht22TaskHandle);
}

void DHT22_DeInit(void) {
    g_dht22_enabled = false;
    DHT22_Pin_Input();
}