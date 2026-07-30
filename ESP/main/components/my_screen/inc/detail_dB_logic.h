#ifndef __DETAIL_DB_LOGIC_H__
#define __DETAIL_DB_LOGIC_H__
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
void dB_init();
void dB_deinit();
void dB_start();

bool dB_get_latest_valid(void);
float dB_get_latest_value(void);
float dB_get_avg(void);
float dB_get_min(void);
float dB_get_max(void);

QueueHandle_t dB_logic_get_queue(void);
#endif