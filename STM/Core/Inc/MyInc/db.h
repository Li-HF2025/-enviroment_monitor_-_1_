#ifndef DB_H
#define DB_H
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include <string.h>

extern osThreadId_t decibelTaskHandle;

void DB_RTOS_Init(void);
void DB_Init(void);
void DB_DeInit(void);
#endif