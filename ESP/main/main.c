#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_serial.h"
#include "my_wifi.h"
#include "my_mqtt.h"
#include "my_screen.h"
#include "main_task.h"
#include "my_ota.h"
#include "my_nvs.h"
void app_main(void)
{
    uart_init();
    main_task_init();
    my_nvs_init();
    wifi_start();
    // mqtt_app_start();
    screen_init();
    ato_init();

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}