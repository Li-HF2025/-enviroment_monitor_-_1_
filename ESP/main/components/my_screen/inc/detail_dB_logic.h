#ifndef __DETAIL_DB_LOGIC_H__
#define __DETAIL_DB_LOGIC_H__
#include <stdio.h>
#include <stdbool.h>
void dB_init();
void dB_deinit();
void dB_start();

bool dB_get_latest_valid(void);
float dB_get_latest_value(void);
#endif