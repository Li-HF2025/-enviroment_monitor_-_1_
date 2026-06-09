#include "wifi_scan.h"
#include <string.h>
#include <stdlib.h>

/* ---- 回调注册 ---- */
static wifi_scan_result_cb_t s_result_cb = NULL;
static wifi_scan_done_cb_t    s_done_cb   = NULL;

void wifi_scan_set_result_callback(wifi_scan_result_cb_t cb) { s_result_cb = cb; }
void wifi_scan_set_done_callback(wifi_scan_done_cb_t cb)    { s_done_cb   = cb; }
esp_err_t start_local_scan(void){
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE
    };
    return esp_wifi_scan_start(&scan_config, true);
}

void display_scan_results(){
    uint16_t ap_num=0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));//获取扫描到的AP数量
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);//分配内存存储AP记录
    if (ap_records == NULL) {
        ESP_LOGE("扫描结果", "内存分配失败");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));//获取扫描到的AP记录
    for (int i = 0; i < ap_num; i++) {
        ESP_LOGI("扫描结果", "AP %d: SSID=%s, RSSI=%d", i, ap_records[i].ssid, ap_records[i].rssi);
    }
    free(ap_records);
}
// 后台任务：在非 LVGL 线程调用（可以阻塞）
void wifi_scan_worker_task(void *pv)
{
    // 同步扫描（在后台任务里可以阻塞）
    esp_err_t scan_err = start_local_scan();
    if (scan_err != ESP_OK) {
    if (s_result_cb) {
        char *msg = strdup("WiFi scan failed");
        if (msg) s_result_cb(msg);
    }
        vTaskDelete(NULL);
        return;
    }
    uint16_t ap_num = 0;
    if (esp_wifi_scan_get_ap_num(&ap_num) != ESP_OK || ap_num == 0) {
        if (s_result_cb) {
            char *msg = strdup("No networks found");
            if (msg) s_result_cb(msg);
        }
    if (s_done_cb) s_done_cb();
        vTaskDelete(NULL);
        return;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!ap_records) {
        if (s_result_cb) {
            char *msg = strdup("Memory error");
            if (msg) s_result_cb(msg);
        }
        if (s_done_cb) s_done_cb();
        vTaskDelete(NULL);
        return;
    }

    if (esp_wifi_scan_get_ap_records(&ap_num, ap_records) != ESP_OK) {
        free(ap_records);
        if (s_result_cb) {
            char *msg = strdup("Scan error");
            if (msg) s_result_cb(msg);
        }
        if (s_done_cb) s_done_cb();
        vTaskDelete(NULL);
        return;
    }

    // 计算总长度并拼接\n分隔的字符串
    size_t total_len = 1;
    for (int i = 0; i < ap_num; ++i) total_len += strlen((char*)ap_records[i].ssid) + 1;
    char *options = malloc(total_len);
    if (!options) {
        free(ap_records);
        if (s_result_cb) {
            char *msg = strdup("Memory error");
            if (msg) s_result_cb(msg);
        }
        if (s_done_cb) s_done_cb();
        vTaskDelete(NULL);
        return;
    }
    options[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < ap_num; ++i) {
        const char *ssid = (const char*)ap_records[i].ssid;
        int n = snprintf(options + off, total_len - off, "%s%s", ssid, (i == ap_num - 1) ? "" : "\n");
        if (n < 0) break;
        off += n;
    }

    free(ap_records);

    // 把拼好的字符串通过 lv_async_call 发送到 LVGL 主线程，callback 负责 free
    if (s_result_cb) s_result_cb(options);
    else free(options);   // 没有注册回调则释放内存
    if (s_done_cb) s_done_cb();
    vTaskDelete(NULL);
}