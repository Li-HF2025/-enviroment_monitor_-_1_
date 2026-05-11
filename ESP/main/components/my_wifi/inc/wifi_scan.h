#ifndef __WIFI_SCAN_H__
#define __WIFI_SCAN_H__

#include "my_wifi.h"

esp_err_t start_local_scan(void);
void display_scan_results(void);
void wifi_scan_worker_task(void *pv);

#endif // __WIFI_SCAN_H__