#ifndef __WIFI_SCAN_H__
#define __WIFI_SCAN_H__

#include "my_wifi.h"

esp_err_t start_local_scan(void);
void display_scan_results(void);
void wifi_scan_worker_task(void *pv);
typedef void (*wifi_scan_result_cb_t)(const char *options);

/** @brief 扫描任务结束回调 */
typedef void (*wifi_scan_done_cb_t)(void);

void wifi_scan_set_result_callback(wifi_scan_result_cb_t cb);
void wifi_scan_set_done_callback(wifi_scan_done_cb_t cb);
#endif // __WIFI_SCAN_H__