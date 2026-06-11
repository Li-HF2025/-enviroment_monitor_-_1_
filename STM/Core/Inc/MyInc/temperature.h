#ifndef __TEMPERATURE_H
#define __TEMPERATURE_H
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"

/* DHT22 错误值 / 有效范围宏 */
#define DHT22_ERROR_TEMP       (-1000.0f)
#define DHT22_ERROR_HUMI       (-1.0f)
#define DHT22_VALID_TEMP_MIN   (-40.0f)
#define DHT22_VALID_TEMP_MAX   (80.0f)
#define DHT22_VALID_HUMI_MIN   (0.0f)
#define DHT22_VALID_HUMI_MAX   (100.0f)

#ifdef __cplusplus
extern "C" {
#endif

void DHT22_Init(void);
void DHT22_Send_Start(void);
int8_t DHT22_Wait_Ack(void);
int8_t DHT22_Read_Bit(void);
int16_t DHT22_Read_Byte(void);
int8_t DHT22_Read_Raw(uint8_t data[5]);
int8_t DHT22_Parse_Data(uint8_t data[5], float *temperature, float *humidity);
void DHT22_RTOS_Init(void);
void DHT22_DeInit(void);
#ifdef __cplusplus
}
#endif

#endif
