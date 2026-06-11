#ifndef __MY_WIFI_H__
#define __MY_WIFI_H__
// 字符串操作函数库（用于拷贝、比较SSID/密码等字符串）
#include <string.h>
// FreeRTOS内核核心头文件（ESP-IDF基于FreeRTOS实时操作系统）
#include "freertos/FreeRTOS.h"
// FreeRTOS任务管理头文件（任务创建、延时等）
#include "freertos/task.h"
// FreeRTOS事件组头文件（用于WiFi连接状态的同步、信号通知）
#include "freertos/event_groups.h"
// ESP32系统库（系统初始化、错误码等）
#include "esp_system.h"
// ESP32 WiFi驱动库（核心：WiFi初始化、连接、配置等）
#include "esp_wifi.h"
// ESP32事件循环库（处理WiFi/IP等异步事件）
#include "esp_event.h"
// ESP32日志库（打印日志、调试信息）
#include "esp_log.h"
// 非易失性闪存库（NVS，用于存储WiFi配置，必须初始化）
#include "nvs_flash.h"

// LwIP轻量TCP/IP协议栈头文件（网络通信、IP地址处理）
#include "lwip/err.h"
#include "lwip/sys.h"

void wifi_start(void);
bool wifi_connect(const char* ssid, const char* password);
void wifi_disconnect(void);

typedef enum {
    MY_WIFI_EVENT_CONNECTED,
    MY_WIFI_EVENT_DISCONNECTED,
    MY_WIFI_EVENT_CONNECT_FAILED,
}my_wifi_event_id_t;

ESP_EVENT_DECLARE_BASE(MY_WIFI_EVENT_BASE);
#endif /* __MY_WIFI_H__ */