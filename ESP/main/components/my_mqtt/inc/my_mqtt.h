#ifndef __MY_MQTT_H__
#define __MY_MQTT_H__
// 标准C库：输入输出、字符串、内存操作
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// ESP32 系统核心库
#include "esp_system.h"
// NVS闪存库（存储配置，必须初始化）
#include "nvs_flash.h"
// ESP32事件循环库
#include "esp_event.h"
// 网络接口库（TCP/IP协议栈）
#include "esp_netif.h"
// 示例通用工具库（简化WiFi/以太网连接）
// ESP32日志库
#include "esp_log.h"
// ESP-IDF官方MQTT客户端库（核心！）
#include "mqtt_client.h"

#include "my_wifi.h" // 依赖WiFi连接功能

#include "esp_timer.h"

#include "mqtt_report.h"
void mqtt_app_start(void);
void mqtt_send_message(const char *topic, const char *data);
void mqtt_subscribe(const char *topic);
void mqtt_unsubscribe(const char *topic);
void mqtt_publish_all_report(void);
void mqtt_report_request_publish(void);
#endif /* __MY_MQTT_H__ */