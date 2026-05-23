#include "my_wifi.h"
#include "my_mqtt.h"
#include "my_ota.h"
#include "my_nvs.h"
#include <string.h>
#include "detail_time_logic.h"
#ifdef CONFIG_MY_WIFI_SSID
#define ESP_WIFI_SSID CONFIG_MY_WIFI_SSID
#else
#define ESP_WIFI_SSID CONFIG_WIFI_SSID
#endif

#ifdef CONFIG_MY_WIFI_PASSWORD
#define ESP_WIFI_PASS CONFIG_MY_WIFI_PASSWORD
#else
#define ESP_WIFI_PASS CONFIG_WIFI_PASSWORD
#endif

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER "移动热点" // WPA3 H2E标识符，不知道为什么无法识别到标识符，干脆注释掉了，反正也不影响连接成功。可能是因为路由器不支持WPA3或者ESP32的WPA3实现有问题。

#ifdef CONFIG_MY_WIFI_MAXIMUM_RETRY
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_MY_WIFI_MAXIMUM_RETRY
#elif defined(CONFIG_ESP_MAXIMUM_RETRY)
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY
#else
#define EXAMPLE_ESP_MAXIMUM_RETRY 5
#endif


#define WIFI_CONNECTED_BIT BIT(0)
#define WIFI_FAIL_BIT BIT(1)

#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

static EventGroupHandle_t wifi_event_group;

static uint8_t s_retry_num = 0;
static bool s_user_disconnect = false;
static bool s_manual_connecting = false;

/**
 * @brief Wi-Fi事件处理程序
 */
static void wifi_event_handler(void* arg,esp_event_base_t event_base
                            ,int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        if(event_id == WIFI_EVENT_STA_START){
            if(!s_manual_connecting && strlen(ESP_WIFI_SSID) > 0 && strlen(ESP_WIFI_PASS) > 0){
                esp_wifi_connect();// 只有配置了默认账号时才自动连接
            }
        }else if(event_id == WIFI_EVENT_STA_DISCONNECTED){
            if(s_user_disconnect){
                ESP_LOGI("WIFI事件", "用户主动断开连接");
                s_user_disconnect = false;
            }else if(s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY){
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI("WIFI事件", "连接失败，正在重试... (%d/%d)", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
            }else{
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);//连接失败事件
                mqtt_app_stop();
                ESP_LOGI("WIFI事件", "连接失败，达到最大重试次数");
            }
        }
    }
}

/**
 * @brief IP事件处理程序
 */
static void ip_event_handler(void* arg,esp_event_base_t event_base
                            ,int32_t event_id, void* event_data){
    if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("IP事件", "获取IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;//重置重试计数器
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);//连接成功事件

        mqtt_app_start();
    }
}

void wifi_disconnect(void){
    s_user_disconnect = true;           // 标记为用户主动断开
    mqtt_app_stop();                    // 立即停止 MQTT（如果需要）
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW("WIFI断开", "esp_wifi_disconnect 返回 %d", err);
    }
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
}

bool wifi_connect(const char* ssid, const char* password){
    s_user_disconnect = true;
    s_manual_connecting = true;
    s_retry_num = 0;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW("WIFI连接", "esp_wifi_disconnect 返回 %s", esp_err_to_name(err));
    }

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW("WIFI连接", "esp_wifi_stop 返回 %s", esp_err_to_name(err));
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    s_manual_connecting = false;
    s_user_disconnect = false;
    ESP_LOGI("WIFI连接", "正在连接到AP SSID:%s", ssid);
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
        pdFALSE, pdFALSE, 
        portMAX_DELAY);//等待连接结果事件

    if(bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI("WIFI连接", "成功连接到AP SSID:%s", ESP_WIFI_SSID);
        s_user_disconnect = false;
        my_nvs_set_value(NVS_KEY_WIFI_SSID, ssid);
        my_nvs_set_value(NVS_KEY_WIFI_PASS, password);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI("WIFI连接", "连接AP失败 SSID:%s", ESP_WIFI_SSID);
        return false;
    } else {
        ESP_LOGE("WIFI连接", "发生未知错误");
        return false;
    }
}

/**
 * @brief 初始化wifi为STA模式
 */
static void wifi_init_sta(void){
    wifi_event_group = xEventGroupCreate();//事件组创建（用于Wi-Fi连接状态的同步、信号通知）
    ESP_ERROR_CHECK(esp_netif_init());//初始化TCP/IP协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default());//创建默认事件循环(用于处理Wi-Fi事件)
    esp_netif_create_default_wifi_sta();//创建默认Wi-Fi STA网络接口

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();//获取默认Wi-Fi初始化配置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));//初始化Wi-Fi驱动程序

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));//注册Wi-Fi事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &instance_got_ip));//注册IP事件处理程序

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));//设置Wi-Fi为STA模式
    ESP_ERROR_CHECK(esp_wifi_start());//先启动Wi-Fi驱动，保证后续扫描可用

    if(strlen(ESP_WIFI_SSID) > 0 && strlen(ESP_WIFI_PASS) > 0){
        wifi_connect(ESP_WIFI_SSID, ESP_WIFI_PASS);//如果配置了SSID，直接连接
    }else{
        ESP_LOGI("WIFI连接", "未配置SSID和密码");
    }

}


void wifi_start(){
    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);//设置Wi-Fi组件的日志级别（根据配置文件）
    }

    ESP_LOGI("WIFI初始化", "正在启动Wi-Fi STA模式...");
    wifi_init_sta();

}