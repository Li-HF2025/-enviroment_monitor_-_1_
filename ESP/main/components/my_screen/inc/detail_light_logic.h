#ifndef __DETAIL_LIGHT_LOGIC_H__
#define __DETAIL_LIGHT_LOGIC_H__
#include <stdio.h>
#include <stdbool.h>
void light_init(void);
void light_deinit(void);
void light_start(void);

bool light_get_latest_valid(void);
float light_get_latest_value(void);
#endif
