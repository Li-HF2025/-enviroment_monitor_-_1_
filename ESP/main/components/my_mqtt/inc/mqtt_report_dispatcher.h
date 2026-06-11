#ifndef __MQTT_REPORT_DISPATCHER_H__
#define __MQTT_REPORT_DISPATCHER_H__
#include "esp_event.h"
typedef enum {
    SENSOR_DB_UPDATED,
    SENSOR_TEMP_UPDATED,
    SENSOR_HUMI_UPDATED,
    SENSOR_LIGHT_UPDATED,
} sensor_event_id_t;// 上报项定义
ESP_EVENT_DECLARE_BASE(SENSOR_EVENT_BASE);

void mqtt_report_dispatcher_init(void);

#endif // __MQTT_REPORT_DISPATCHER_H__