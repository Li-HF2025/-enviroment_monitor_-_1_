#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_serial.h"
#include "my_wifi.h"
#include "my_mqtt.h"
#include "mqtt_report_dispatcher.h"
#include "my_screen.h"
#include "main_task.h"
#include "my_ota.h"
#include "my_nvs.h"
#include "my_stm_ota.h"
void app_main(void)
{
    uart_init();
    main_task_init();
    my_nvs_init();
    wifi_start();
    screen_init();
    ato_init();
    // 初始化完成后验证固件存活（自动回滚检查点）
    ato_validate_app();
    stm_ota_init();

    // stm_ota_self_test();
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}