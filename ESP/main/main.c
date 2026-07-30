#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_serial.h"
#include "my_wifi.h"
#include "my_mqtt.h"
#include "mqtt_report_dispatcher.h"
#include "my_screen.h"
#include "detail_dB_logic.h"   // dB_start()
#include "detail_temp_logic.h"  // temp_start()
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
    mqtt_report_dispatcher_init();
    mqtt_app_start();                    // 启动 MQTT 客户端 + 上报定时器
    screen_init();
    dB_start();          // 强制启动数据采集（不依赖 UI 页面打开）
    temp_start();        // 强制启动数据采集（不依赖 UI 页面打开）
    ato_init();
    // 初始化完成后验证固件存活（自动回滚检查点）
    ato_validate_app();
    stm_ota_init();

    // stm_ota_self_test();
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}