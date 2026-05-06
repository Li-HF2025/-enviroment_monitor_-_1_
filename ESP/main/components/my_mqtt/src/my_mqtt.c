#include "my_mqtt.h"

#include "mqtt_report.h"

#ifdef CONFIG_BROKER_URL
#define BROKER_URL CONFIG_BROKER_URL
#else
#define BROKER_URL ""
#endif

#ifdef CONFIG_ONENET_PRODUCT_ID
#define ONENET_PRODUCT_ID CONFIG_ONENET_PRODUCT_ID
#else
#define ONENET_PRODUCT_ID ""
#endif

#ifdef CONFIG_ONENET_DEVICE_NAME
#define ONENET_DEVICE_NAME CONFIG_ONENET_DEVICE_NAME
#else
#define ONENET_DEVICE_NAME ""
#endif

#ifdef CONFIG_ONENET_TOKEN
#define ONENET_TOKEN CONFIG_ONENET_TOKEN
#else
#define ONENET_TOKEN ""
#endif

#ifdef CONFIG_ONENET_HEARTBEAT_PAYLOAD
#define ONENET_HEARTBEAT_PAYLOAD CONFIG_ONENET_HEARTBEAT_PAYLOAD
#else
#define ONENET_HEARTBEAT_PAYLOAD ""
#endif

static char s_onenet_cmd_topic[128];
static char s_onenet_dp_post_topic[128];

esp_mqtt_client_handle_t client = NULL; // 全局MQTT客户端句柄
static bool s_mqtt_connected = false;

static TaskHandle_t s_mqtt_report_task_handle = NULL;

void mqtt_report_request_publish(void)
{
    if (s_mqtt_report_task_handle) {
        xTaskNotifyGive(s_mqtt_report_task_handle);
    }
}
static void mqtt_report_task(void *arg)
{
    while (1) {
        // 等待通知，超时也会醒来做一次周期上报
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
        mqtt_publish_all_report();
        vTaskDelay(pdMS_TO_TICKS(200)); // 发布完后稍微延迟一下，避免过于频繁的发布
    }
}

// 发布所有上报数据的函数
void mqtt_publish_all_report(void)
{
    if (!client || !s_mqtt_connected) return;

    int count = 0;
    const mqtt_report_item_t *items = mqtt_report_get_all(&count);

    char payload[1024];
    int offset = 0;
    long long ms = esp_timer_get_time() / 1000LL;
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                       "{\"id\":\"%lld\",\"version\":\"1.0\",\"params\":{", ms);

    bool first = true;
    for (int i = 0; i < count; i++) {
        const mqtt_report_item_t *it = &items[i];
        if (!it->key || !it->valid) continue;

        if (!first) {
            offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
        }
        first = false;

        if (it->type == MQTT_REPORT_BOOL) {
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"%s\":{\"value\":%s}", it->key, it->value.b ? "true" : "false");
        } else if (it->type == MQTT_REPORT_FLOAT) {
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"%s\":{\"value\":%.2f}", it->key, it->value.f);
        } else if (it->type == MQTT_REPORT_INT) {
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                               "\"%s\":{\"value\":%d}", it->key, it->value.i);
        }

        if (offset >= (int)sizeof(payload) - 128) break; // 防溢出保护
    }

    offset += snprintf(payload + offset, sizeof(payload) - offset, "}}");

    mqtt_send_message(s_onenet_dp_post_topic, payload);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    ESP_LOGD("MQTT","事件类型：%s,事件ID:%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    // 处理不同的MQTT事件
    switch((esp_mqtt_event_id_t)event_id){
        case MQTT_EVENT_CONNECTED://连接成功事件
            ESP_LOGI("MQTT","连接成功");
            s_mqtt_connected = true;

            if (esp_mqtt_client_subscribe(event->client, s_onenet_cmd_topic, 1) >= 0) {
                ESP_LOGI("MQTT", "已自动订阅主题:%s", s_onenet_cmd_topic);
            } else {
                ESP_LOGW("MQTT", "自动订阅失败, 主题:%s", s_onenet_cmd_topic);
            }

            char post_reply_topic[128];
            snprintf(post_reply_topic, sizeof(post_reply_topic), "$sys/%s/%s/thing/property/post/reply",
                    ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
            if (esp_mqtt_client_subscribe(event->client, post_reply_topic, 1) >= 0) {
                ESP_LOGI("MQTT", "已自动订阅主题:%s", post_reply_topic);
            } else {
                ESP_LOGW("MQTT", "自动订阅失败, 主题:%s", post_reply_topic);
            }

            char prop_set_topic[128];
            snprintf(prop_set_topic, sizeof(prop_set_topic), "$sys/%s/%s/thing/property/set",
                    ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
            if (esp_mqtt_client_subscribe(event->client, prop_set_topic, 1) >= 0) {
                ESP_LOGI("MQTT", "已自动订阅主题:%s", prop_set_topic);
            } else {
                ESP_LOGW("MQTT", "自动订阅失败, 主题:%s", prop_set_topic);
            }

            mqtt_publish_all_report();
            break;
        case MQTT_EVENT_DISCONNECTED://断开连接事件
            ESP_LOGI("MQTT","断开连接");
            s_mqtt_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED://订阅成功事件
            ESP_LOGI("MQTT","订阅成功，消息ID:%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED://取消订阅事件
            ESP_LOGI("MQTT","取消订阅，消息ID:%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED://发布成功事件
            // ESP_LOGI("MQTT","发布成功，消息ID:%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA://接收到数据事件
            // ESP_LOGI("MQTT","接收到数据，主题:%.*s,数据:%.*s", event->topic_len, event->topic, event->data_len, event->data);
            // 可以在这里处理接收到的数据，例如根据主题进行不同的处理
            //@TODO: 根据实际需求处理接收到的数据
            break;
        case MQTT_EVENT_ERROR://错误事件
            ESP_LOGI("MQTT","发生错误");
            break;
        default:
            ESP_LOGI("MQTT","未知事件ID:%d", event_id);
    }
}

static void mqtt_init(){
    static char mqtt_client_id[64];
    // 必填项检查：请在 menuconfig -> My MQTT Configuration 中填写
    if (BROKER_URL[0] == '\0' || ONENET_PRODUCT_ID[0] == '\0' ||
        ONENET_DEVICE_NAME[0] == '\0' || ONENET_TOKEN[0] == '\0') {
        ESP_LOGE("MQTT", "ONENET配置不完整：请填写 BROKER_URL / ONENET_PRODUCT_ID / ONENET_DEVICE_NAME / ONENET_TOKEN");
        return;
    }

    // ONENET MQTT认证三要素：clientId=deviceName, username=productId, password=token
    snprintf(mqtt_client_id, sizeof(mqtt_client_id), "%s", ONENET_DEVICE_NAME);
    snprintf(s_onenet_cmd_topic, sizeof(s_onenet_cmd_topic), "$sys/%s/%s/cmd/#", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    snprintf(s_onenet_dp_post_topic, sizeof(s_onenet_dp_post_topic), "$sys/%s/%s/thing/property/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    esp_mqtt_client_config_t mqtt_cfg={
        .broker.address.uri = BROKER_URL,
        .credentials = {
            .client_id = mqtt_client_id,
            .username = ONENET_PRODUCT_ID,
            .authentication = {
                .use_secure_element = false, // 不使用安全元素
                .password = ONENET_TOKEN,
            }
        }
    };//获取mqtt配置（主要是服务端地址）
    ESP_LOGI("MQTT","地址: %s", BROKER_URL);
    client = esp_mqtt_client_init(&mqtt_cfg);//初始化mqtt客户端
    //注册mqtt事件回调函数
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_mqtt_client_start(client);//启动mqtt客户端

    ESP_LOGI("MQTT","MQTT客户端已启动");
    if (s_mqtt_report_task_handle == NULL) {
        xTaskCreate(mqtt_report_task, "mqtt_report_task", 4096, NULL, 5, &s_mqtt_report_task_handle);
    }
}

void mqtt_app_start(){
    ESP_LOGI("MQTT","MQTT应用开始");
    mqtt_report_init();
    mqtt_init();
}

void mqtt_send_message(const char *topic, const char *data){
    if (client == NULL) {
        ESP_LOGW("MQTT", "客户端未初始化，发布失败");
        return;
    }
    if (!s_mqtt_connected) {
        ESP_LOGW("MQTT", "尚未连接MQTT Broker，发布失败");
        return;
    }
    if (topic == NULL || topic[0] == '\0') {
        ESP_LOGW("MQTT", "topic为空，发布失败");
        return;
    }
    if (data == NULL) {
        ESP_LOGW("MQTT", "payload为空，发布失败");
        return;
    }

    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0); // 发布消息
    if (msg_id < 0) {
        ESP_LOGW("MQTT", "发布失败, topic:%s", topic);
    }
}

void mqtt_subscribe(const char *topic){
    if (client == NULL) {
        ESP_LOGW("MQTT", "客户端未初始化，订阅失败");
        return;
    }
    if (!s_mqtt_connected) {
        ESP_LOGW("MQTT", "尚未连接MQTT Broker，订阅失败");
        return;
    }
    if (topic == NULL || topic[0] == '\0') {
        ESP_LOGW("MQTT", "topic为空，订阅失败");
        return;
    }

    int msg_id = esp_mqtt_client_subscribe(client, topic, 1); // 订阅主题
    if (msg_id < 0) {
        ESP_LOGW("MQTT", "订阅失败, topic:%s", topic);
    }
}

void mqtt_unsubscribe(const char *topic){
    if (client == NULL) {
        ESP_LOGW("MQTT", "客户端未初始化，取消订阅失败");
        return;
    }
    if (!s_mqtt_connected) {
        ESP_LOGW("MQTT", "尚未连接MQTT Broker，取消订阅失败");
        return;
    }
    if (topic == NULL || topic[0] == '\0') {
        ESP_LOGW("MQTT", "topic为空，取消订阅失败");
        return;
    }

    int msg_id = esp_mqtt_client_unsubscribe(client, topic); // 取消订阅主题
    if (msg_id < 0) {
        ESP_LOGW("MQTT", "取消订阅失败, topic:%s", topic);
    }
}

