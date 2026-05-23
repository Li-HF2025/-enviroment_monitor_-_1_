#ifndef __MY_ATO_H
#define __MY_ATO_H
#include "esp_err.h"
void ato_init(void);
void ato_start(void);
esp_err_t onenet_ota_upload_version(void);
#endif