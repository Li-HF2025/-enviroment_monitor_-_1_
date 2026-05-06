#ifndef MAIN_TASK_H
#define MAIN_TASK_H

#include "mySerial.h"
#include "cmsis_os.h"

extern osThreadId_t mainTaskHandle;

void main_task(void *pvParameters);
void main_task_RTOS_Init(void);
#endif /* MAIN_TASK_H */