#include "my_wifi.h"
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
static EventGroupHandle_t wifi_event_group;

static uint8_t s_retry_num = 0;

/**
 * @brief Wi-Fi事件处理程序
 */
static void wifi_event_handler(void* arg,esp_event_base_t event_base
                            ,int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        if(event_id == WIFI_EVENT_STA_START){
            esp_wifi_connect();//Wi-Fi启动后尝试连接
        }else if(event_id == WIFI_EVENT_STA_DISCONNECTED){
            if(s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY){
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI("WIFI事件", "连接失败，正在重试... (%d/%d)", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
            }else{
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);//连接失败事件
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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            // 认证模式阈值（匹配路由器加密方式）
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,              // WPA3认证模式
            // .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,   // WPA3标识符
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));//设置Wi-Fi为STA模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));//配置Wi-Fi连接参数（SSID、密码等）
    ESP_ERROR_CHECK(esp_wifi_start());//启动Wi-Fi驱动程序
    ESP_LOGI("WIFI初始化", "初始化完成. SSID:%s password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
        pdFALSE, pdFALSE, 
        portMAX_DELAY);//等待连接结果事件

    if(bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI("WIFI连接", "成功连接到AP SSID:%s", ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI("WIFI连接", "连接AP失败 SSID:%s", ESP_WIFI_SSID);
    } else {
        ESP_LOGE("WIFI连接", "发生未知错误");
    }
}



void wifi_start(){
    esp_err_t ret = nvs_flash_init();//初始化闪存
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());//擦除闪存
        ret = nvs_flash_init();//重新初始化闪存
    }
    ESP_ERROR_CHECK(ret);//检查闪存初始化结果

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);//设置Wi-Fi组件的日志级别（根据配置文件）
    }

    ESP_LOGI("WIFI初始化", "正在启动Wi-Fi STA模式...");
    wifi_init_sta();

}