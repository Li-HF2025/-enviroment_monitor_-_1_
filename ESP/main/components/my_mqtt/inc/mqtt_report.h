// mqtt_report.h
#ifndef MQTT_REPORT_H
#define MQTT_REPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MQTT_REPORT_BOOL,
    MQTT_REPORT_FLOAT,
    MQTT_REPORT_INT,
} mqtt_report_type_t;

typedef struct {
    const char *key;
    mqtt_report_type_t type;
    union {
        bool b;
        float f;
        int i;
    } value;
    bool valid;
} mqtt_report_item_t;

void mqtt_report_init(void);
void mqtt_report_set_bool(const char *key, bool value);
void mqtt_report_set_float(const char *key, float value);
void mqtt_report_set_int(const char *key, int value);
const mqtt_report_item_t *mqtt_report_get_all(int *count);

#endif /* MQTT_REPORT_H */