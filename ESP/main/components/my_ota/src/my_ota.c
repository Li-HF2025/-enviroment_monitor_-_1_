#include "my_ota.h"

#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <ctype.h>

// 标准C库头文件
#include <string.h>
#include <inttypes.h>

// FreeRTOS操作系统头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// ESP-IDF系统核心头文件
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"

// OTA固件升级相关头文件
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

// JSON解析库头文件
#include "cJSON.h"

#include "my_nvs.h"
#define NVS_KEY_APP_VERSION "app_version"

#define ONENET_PRODUCT_ID CONFIG_ONENET_PRODUCT_ID
#define ONENET_ACCESS_KEY CONFIG_ONENET_ACCESS_KEY
#define ONENET_DEVICE_NAME CONFIG_ONENET_DEVICE_NAME

#define     MAX_DATA_BUFF   1024
//ota基础url
#define     ONENET_OTA_URL  "http://iot-api.heclouds.com/fuse-ota"
// token有效期（秒）
#define     TOKEN_EXPIRE_SECONDS     86400
//接收到的http 数据
static uint8_t data_buff[MAX_DATA_BUFF];
//接收到的http数据长度
static size_t   data_buff_len = 0;

static char target_version[64];    //目标版本号
static char task_id = 0;    //ota任务id

static const char *TAG = "ATO";

static int64_t get_token_expire_time(void)
{
    return (int64_t)(time(NULL) + TOKEN_EXPIRE_SECONDS);
}


static const char *sign_method_to_str(int sign_method)
{
    switch (sign_method) {
        case 0: return "md5";
        case 1: return "sha1";
        case 2: return "sha256";
        default: return NULL;
    }
}

static void url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;

    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; ++si) {
        unsigned char c = (unsigned char)src[si];
        int safe = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';

        if (safe) {
            dst[di++] = (char)c;
        } else {
            if (di + 3 >= dst_size) break;
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0x0F];
        }
    }
    dst[di] = '\0';
}

void dev_token_generate(char *token,
                        int sign_method,
                        int64_t et,
                        const char *res,
                        const char *version,
                        const char *access_key_b64)
{
    const char *method = sign_method_to_str(sign_method);
    const char *ver = (version && version[0] != '\0') ? version : "2018-10-31";

    if (!token || !res || !access_key_b64 || !method) {
        if (token) token[0] = '\0';
        return;
    }

    unsigned char key_bin[128];
    size_t key_bin_len = 0;
    if (mbedtls_base64_decode(key_bin, sizeof(key_bin), &key_bin_len,
                              (const unsigned char *)access_key_b64,
                              strlen(access_key_b64)) != 0) {
        token[0] = '\0';
        return;
    }

    char string_to_sign[256];
    snprintf(string_to_sign, sizeof(string_to_sign),
             "%lld\n%s\n%s\n%s",
             (long long)et, method, res, ver);

    const mbedtls_md_info_t *md_info = NULL;
    switch (sign_method) {
        case 0: md_info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5); break;
        case 1: md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1); break;
        case 2: md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); break;
        default: token[0] = '\0'; return;
    }

    unsigned char digest[32];
    unsigned char sign_b64[128];
    size_t sign_b64_len = 0;

    if (mbedtls_md_hmac(md_info,
                        key_bin, key_bin_len,
                        (const unsigned char *)string_to_sign,
                        strlen(string_to_sign),
                        digest) != 0) {
        token[0] = '\0';
        return;
    }

    size_t digest_len = mbedtls_md_get_size(md_info);
    if (mbedtls_base64_encode(sign_b64, sizeof(sign_b64), &sign_b64_len,
                              digest, digest_len) != 0) {
        token[0] = '\0';
        return;
    }
    sign_b64[sign_b64_len] = '\0';

    char res_enc[256];
    char sign_enc[256];
    url_encode(res, res_enc, sizeof(res_enc));
    url_encode((const char *)sign_b64, sign_enc, sizeof(sign_enc));

    snprintf(token, 1024, "version=%s&res=%s&et=%lld&method=%s&sign=%s",
             ver, res_enc, (long long)et, method, sign_enc);
}

static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:    //错误事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:    //连接成功事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:    //发送头事件
            //ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:    //接收头事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:    //接收数据事件
            {
                size_t copy_len = 0;
                ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                printf("HTTP_EVENT_ON_DATA data=%.*s\r\n", evt->data_len,(char*)evt->data);
                if(evt->data_len > MAX_DATA_BUFF - data_buff_len)
                {
                    copy_len = MAX_DATA_BUFF - data_buff_len;
                }
                else
                {
                    copy_len = evt->data_len;
                }
                //将数据存到data_buff里面
                memcpy(&data_buff[data_buff_len],evt->data,copy_len);
                data_buff_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:    //会话完成事件
            data_buff_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:    //断开事件
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            data_buff_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            //ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}
static esp_err_t onenet_ota_http_connect(const char* url,esp_http_client_method_t method,char* post_data){
    esp_http_client_config_t config =
        {
            .url = url,
            .event_handler = http_client_event_handler,
        };
    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if(http_client == NULL)
    {
        ESP_LOGE(TAG, "无法初始化HTTP客户端");
        return ESP_FAIL;
    }

    char* token = (char*)malloc(512);
    memset(token,0,512);
    char request_res[256];
    snprintf(request_res, sizeof(request_res), "products/%s", ONENET_PRODUCT_ID);
    //计算token
    dev_token_generate(token,2,get_token_expire_time(),request_res,"2022-05-01",ONENET_ACCESS_KEY);
    ESP_LOGI(TAG,"user token:%s",token);

    //设置发送请求头
    esp_http_client_set_method(http_client, method);
    esp_http_client_set_header(http_client,"Content-Type","application/json");
    esp_http_client_set_header(http_client,"Authorization",token);
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");
    if(post_data)
    {
        ESP_LOGI(TAG,"post data:%s",post_data);
        esp_http_client_set_post_field(http_client,post_data,strlen(post_data));
    }
    data_buff_len = 0;
    memset(data_buff,0,sizeof(data_buff));
    //esp_http_client_perform这句函数会阻塞，直到完整的http请求结束才返回
    esp_err_t err  = esp_http_client_perform(http_client);
    free(token);
    //清理操作
    esp_http_client_cleanup(http_client);
    return err;
}

static const char *get_app_verion(void)
{
    static char version[64] = {0};
    size_t len = sizeof(version);

    version[0] = '\0';
    my_nvs_get_value(NVS_KEY_APP_VERSION, version, &len);

    if (version[0] != '\0') {
        return version;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL && app_desc->version[0] != '\0') {
        return app_desc->version;
    }

    return "unknown";
}

esp_err_t onenet_ota_upload_version(void)
{
    //格式：{"s_version":"V1.3", "f_version": "V2.0"}
    char version_info[256];
    char url[256];
    esp_err_t ret = ESP_FAIL;
    //获取版本号
    const char* version = get_app_verion();
    //生成消息体内容（版本号）
    snprintf(version_info,sizeof(version_info),"{\"s_version\":\"%s\", \"f_version\": \"%s\"}",version,version);
    //计算url
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/version",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,version_info))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload version fail!");
        return ret;
    }
    return ret;
}
esp_err_t  onenet_ota_check_task(const char* type,const char* version)
{
    char url[256];
    esp_err_t ret = ESP_FAIL;
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,type,version);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_GET,NULL))
    {
        cJSON *root =  cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");    //错误代码
            cJSON *data_js = cJSON_GetObjectItem(root,"data");
            cJSON* target_js = cJSON_GetObjectItem(data_js,"target");
            cJSON* tid_js = cJSON_GetObjectItem(data_js,"tid");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
            {
                if(target_js && tid_js)    //我们感兴趣的只有任务id和目标版本号
                {
                    snprintf(target_version,sizeof(target_version),"%s",cJSON_GetStringValue(target_js));
                    task_id = cJSON_GetNumberValue(tid_js);    //取出任务id
                    ret = ESP_OK;
                }
            }
            else 
            {
                ESP_LOGI(TAG,"Check ota task invaild code");
            }
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGI(TAG,"Check ota task fail!");
            return ret;
        }
    }
    else
    {
        return ret;
    }
    return ret;
}

esp_err_t onenet_ota_upload_status(int tid,int step)
{
    char url[256];
    char payload[32];
    esp_err_t ret = ESP_FAIL;
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/%d/status",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,tid);
    snprintf(payload,sizeof(payload),"{\"step\":%d}",step);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,payload))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload status fail!");
        return ret;
    }
    return ret;
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    // 检查输入参数是否为空
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 获取当前正在运行的固件分区
    const esp_partition_t *running = esp_ota_get_running_partition();
    // 获取当前运行固件的应用描述信息
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    // 如果没有启用跳过版本检查，则比较新旧固件的版本号
#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    // 如果版本号完全相同，则不进行升级
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }
#endif

    // 所有验证通过
    return ESP_OK;
}
static void ota_task(void *pvParameter){
    ESP_LOGI(TAG, "OTA任务开始执行");
    esp_err_t err;
    esp_err_t ota_finish_err = ESP_OK;

    //配置HTTP客户端
    esp_http_client_config_t config = {
        .url = CONFIG_OTA_URL,
        .cert_pem = NULL, 
        .timeout_ms = 5000,
        .keep_alive_enable = true,
        .buffer_size = 1024
    };
    esp_https_ota_config_t ota_config={
        .http_config = &config,
        // .http_client_init_cb = _http_client_init_cb //可以添加HTTP客户端初始化回调函数，进行一些定制化的设置，ONENET平台不需要
    };
    esp_https_ota_handle_t https_ota_handle = NULL;
    // 开始HTTPS OTA升级流程
    // 这一步会初始化OTA分区、连接服务器、验证固件头部
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        vTaskDelete(NULL);
    }

    // 获取新固件的应用描述信息
    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed");
        goto ota_end;
    }

    // 验证新固件的头部信息
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        goto ota_end;
    }


    while(1){
        err = esp_https_ota_perform(https_ota_handle);
        // 如果返回值不是ESP_ERR_HTTPS_OTA_IN_PROGRESS，说明下载已完成或出错
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        const size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
        ESP_LOGD(TAG, "获取字段长度: %d", len);
        if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
            // 数据未完整接收，可能是网络中断或服务器错误
            ESP_LOGE(TAG, "Complete data was not received.");
        } else {
            // 完成OTA升级，验证固件并更新引导分区
            ota_finish_err = esp_https_ota_finish(https_ota_handle);
            if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
                // OTA升级成功，重启设备
                ESP_LOGI(TAG, "OTA更新完成");
                vTaskDelay(1000 / portTICK_PERIOD_MS);  // 等待日志输出完成
                esp_restart();
            } else {
                // OTA升级失败
                if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                    // 固件验证失败，可能是固件损坏或签名不正确
                    ESP_LOGE(TAG, "OTA更新失败");
                }
                ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
                vTaskDelete(NULL);
            }
        }
    }


ota_end:
    // 中止OTA升级流程，释放资源
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    vTaskDelete(NULL);
}

static void ato_start(void)
{
    ESP_LOGI(TAG, "开始执行 OTA");
}


static void event_handler(void* arg,esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == ESP_HTTPS_OTA_EVENT){
        switch(event_id){
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA升级开始");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "已连接到OTA服务器");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "正在获取固件描述信息");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
                // 正在验证固件是否支持当前芯片版本
                ESP_LOGI(TAG, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "正在解密固件数据");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGI(TAG, "正在写入固件数据到闪存");
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "正在更新引导分区");
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA升级完成");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "OTA升级中止");
                break;
        }
    }else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi已连接,准备OTA连接");
        ato_start();
    }
}


void ato_init(void){
    ESP_LOGI(TAG, "ATO模块初始化开始");
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));// 注册OTA事件处理程序

    onenet_ota_upload_version();    //上报版本号到平台

    //@TODO:检测WIFI连接状态，如果未连接则不启动OTA升级，或者在事件处理程序里根据需要处理OTA事件

}