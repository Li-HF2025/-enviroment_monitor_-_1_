#ifndef __DETAIL_TEMP_LOGIC_H__
#define __DETAIL_TEMP_LOGIC_H__
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
void temp_init();
void temp_deinit();
void temp_start();
bool temp_get_latest_valid(void);
float temp_get_latest_value(void);
float temp_get_latest_humidity(void);
QueueHandle_t temp_logic_get_queue(void);
#endif