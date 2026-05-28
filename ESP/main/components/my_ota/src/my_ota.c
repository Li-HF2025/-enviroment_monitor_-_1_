#include "my_ota.h"

// C standard library
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// ESP-IDF core
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

// ESP-IDF peripherals & protocols
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"

// Third-party
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

// Project components
#include "my_nvs.h"

/* ==========================================================================
 * Compile-time constants
 * ========================================================================== */

// OneNET platform config (from Kconfig / sdkconfig)
#define ONENET_PRODUCT_ID   CONFIG_ONENET_PRODUCT_ID
#define ONENET_ACCESS_KEY   CONFIG_ONENET_ACCESS_KEY
#define ONENET_DEVICE_NAME  CONFIG_ONENET_DEVICE_NAME

// OTA service endpoint & settings
#define ONENET_OTA_URL          "http://iot-api.heclouds.com/fuse-ota"
#define TOKEN_EXPIRE_SECONDS    86400

// Buffer sizes
#define MAX_DATA_BUFF           1024

/* ==========================================================================
 * Module-level state
 * ========================================================================== */

static const char *TAG = "ATO";

// HTTP response buffer
static uint8_t  data_buff[MAX_DATA_BUFF];
static size_t   data_buff_len = 0;

// OTA task state
static char target_version[64];
static char ota_download_url[256];
static char firmware_md5[33];
static int  task_id        = 0;
static int  firmware_size  = 0;
static int  s_ota_progress = 0;
static bool ota_running    = false;

/* ==========================================================================
 * Static utility helpers — token generation & HTTP transport
 * ========================================================================== */

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

static int64_t get_token_expire_time(void)
{
    return (int64_t)(time(NULL) + TOKEN_EXPIRE_SECONDS);
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
        case HTTP_EVENT_ERROR:
            //ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            //ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
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
                memcpy(&data_buff[data_buff_len],evt->data,copy_len);
                data_buff_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            data_buff_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            data_buff_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            //ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

static esp_err_t onenet_ota_http_connect(const char* url, esp_http_client_method_t method, char* post_data)
{
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
    if (token == NULL) {
        ESP_LOGE(TAG, "malloc token failed");
        esp_http_client_cleanup(http_client);
        return ESP_ERR_NO_MEM;
    }
    memset(token,0,512);
    char request_res[256];
    snprintf(request_res, sizeof(request_res), "products/%s", ONENET_PRODUCT_ID);
    dev_token_generate(token, 2, get_token_expire_time(), request_res, "2022-05-01", ONENET_ACCESS_KEY);
    ESP_LOGI(TAG, "user token:%s", token);

    esp_http_client_set_method(http_client, method);
    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_header(http_client, "Authorization", token);
    esp_http_client_set_header(http_client, "host", "iot-api.heclouds.com");
    if(post_data)
    {
        ESP_LOGI(TAG, "post data:%s", post_data);
        esp_http_client_set_post_field(http_client, post_data, strlen(post_data));
    }
    data_buff_len = 0;
    memset(data_buff, 0, sizeof(data_buff));
    esp_err_t err = esp_http_client_perform(http_client);
    free(token);
    esp_http_client_cleanup(http_client);
    return err;
}

/* ==========================================================================
 * Public API — Version query
 * ========================================================================== */

const char *ota_get_current_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc != NULL && app_desc->version[0] != '\0') {
        return app_desc->version;
    }
    return "unknown";
}

/* ==========================================================================
 * Public API — OneNET OTA protocol
 * ========================================================================== */

esp_err_t onenet_ota_upload_version(void)
{
    char version_info[256];
    char url[256];
    esp_err_t ret = ESP_FAIL;

    const char* version = ota_get_current_version();
    snprintf(version_info, sizeof(version_info), "{\"s_version\":\"%s\", \"f_version\": \"%s\"}", version, version);
    snprintf(url, 256, ONENET_OTA_URL"/%s/%s/version", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_POST, version_info))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root, "code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Upload version fail!");
        return ret;
    }
    return ret;
}

esp_err_t onenet_ota_check_task(const char* type, const char* version)
{
    char url[256];
    esp_err_t ret = ESP_FAIL;
    snprintf(url, 256, ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, type, version);
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_GET, NULL))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root, "code");
            cJSON *data_js = cJSON_GetObjectItem(root, "data");
            cJSON* target_js = cJSON_GetObjectItem(data_js, "target");
            cJSON* tid_js = cJSON_GetObjectItem(data_js, "tid");
            cJSON* size_js = cJSON_GetObjectItem(data_js, "size");
            cJSON* md5_js = cJSON_GetObjectItem(data_js, "md5");
            if(md5_js){
                snprintf(firmware_md5, sizeof(firmware_md5), "%s", cJSON_GetStringValue(md5_js));
            }
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
            {
                if(target_js && tid_js)
                {
                    snprintf(target_version, sizeof(target_version), "%s", cJSON_GetStringValue(target_js));
                    task_id = cJSON_GetNumberValue(tid_js);
                    snprintf(ota_download_url, sizeof(ota_download_url),
                             ONENET_OTA_URL"/%s/%s/%d/download",
                             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, task_id);
                    if (size_js) {
                        firmware_size = (int)cJSON_GetNumberValue(size_js);
                    }
                    ret = ESP_OK;
                }
            }
            else
            {
                ESP_LOGI(TAG, "Check ota task invaild code");
            }
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGI(TAG, "Check ota task fail!");
            return ret;
        }
    }
    else
    {
        return ret;
    }
    return ret;
}

esp_err_t onenet_ota_upload_status(int tid, int step)
{
    char url[256];
    char payload[32];
    esp_err_t ret = ESP_FAIL;
    snprintf(url, 256, ONENET_OTA_URL"/%s/%s/%d/status", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);
    snprintf(payload, sizeof(payload), "{\"step\":%d}", step);
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_POST, payload))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root, "code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Upload status fail!");
        return ret;
    }
    return ret;
}

/* ==========================================================================
 * Static — OTA breakpoint resume
 * ========================================================================== */

static uint32_t ota_resume_get_written_len(const char *md5)
{
    if (md5 == NULL || md5[0] == '\0') return 0;

    nvs_handle_t h = my_nvs_get_handle("ota_resumption");
    if(h == 0) {
        ESP_LOGW(TAG, "断点续传: namespace ota_resumption 未打开");
        return 0;
    }
    size_t len = 33;
    char saved_md5[33] = {0};
    if (nvs_get_str(h, "ota_md5", saved_md5, &len) != ESP_OK) {
        ESP_LOGW(TAG, "断点续传: NVS中无有效断点数据，从头下载");
        return 0;
    }
    if (strcmp(saved_md5, md5) != 0) {
        ESP_LOGI(TAG, "断点MD5不匹配（新升级任务），从头下载");
        return 0;
    }

    uint32_t wr_len = 0;
    nvs_get_u32(h, "ota_wr_len", &wr_len);
    ESP_LOGI(TAG, "断点续传: 上次已写入 %"PRIu32" 字节", wr_len);
    return wr_len;
}

static void ota_resume_save_progress(const char *md5, uint32_t wr_len)
{
    nvs_handle_t h = my_nvs_get_handle("ota_resumption");
    if (h == 0) return;

    nvs_set_str(h, "ota_md5", md5);
    nvs_set_u32(h, "ota_wr_len", wr_len);
    nvs_commit(h);
}

static void ota_resume_cleanup(void)
{
    my_nvs_erase_all_ns("ota_resumption");
}

/* ==========================================================================
 * Static — OTA pipeline helpers
 * ========================================================================== */

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static esp_err_t ota_http_client_init_cb(esp_http_client_handle_t http_client)
{
    char token_buf[512];
    memset(token_buf, 0, sizeof(token_buf));
    char request_res[256];
    snprintf(request_res, sizeof(request_res), "products/%s", ONENET_PRODUCT_ID);
    dev_token_generate(token_buf, 2, get_token_expire_time(), request_res, "2022-05-01", ONENET_ACCESS_KEY);
    esp_http_client_set_header(http_client, "Authorization", token_buf);
    return ESP_OK;
}

/* ==========================================================================
 * Static — OTA download task
 * ========================================================================== */

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "OTA任务开始执行, URL: %s", ota_download_url);
    esp_err_t err;
    esp_err_t ota_finish_err = ESP_OK;

    esp_wifi_set_ps(WIFI_PS_NONE);

    if (ota_download_url[0] == '\0') {
        ESP_LOGE(TAG, "下载地址为空，无法启动OTA");
        ota_running = false;
        vTaskDelete(NULL);
    }
    int last_reported_step = 0;
    uint32_t last_saved_wr_len = 0;
    uint32_t wr_len = 0;
    wr_len = ota_resume_get_written_len(firmware_md5);
    if (firmware_size > 0) {
        s_ota_progress = (wr_len * 100) / firmware_size;
        last_reported_step = s_ota_progress;
    }

    esp_http_client_config_t config = {
        .url = ota_download_url,
        .cert_pem = NULL,
        .timeout_ms = 5000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = ota_http_client_init_cb,
        .ota_image_bytes_written = wr_len,
        .ota_resumption = true,
        .partial_http_download = false
    };
    esp_https_ota_handle_t https_ota_handle = NULL;

    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        onenet_ota_upload_status(task_id, 104);
        ota_running = false;
        vTaskDelete(NULL);
    }

    onenet_ota_upload_status(task_id, 0);

    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed");
        onenet_ota_upload_status(task_id, 206);
        goto ota_end;
    }

    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        onenet_ota_upload_status(task_id, 204);
        goto ota_end;
    }

    while(1){
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        if (firmware_size > 0) {
            size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
            int step = (len * 100) / firmware_size;
            s_ota_progress = step;

            if (step - last_reported_step >= 10) {
                onenet_ota_upload_status(task_id, step);
                last_reported_step = step;
            }

            if (len - last_saved_wr_len >= 64 * 1024) {
                ota_resume_save_progress(firmware_md5, len);
                last_saved_wr_len = len;
            }

            static int last_logged_step = -1;
            if (step != last_logged_step) {
                ESP_LOGI(TAG, "Download: %d%% (%d/%d)", step, len, firmware_size);
                last_logged_step = step;
            }
        }
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "数据接收不完整，断点已保存，下次重试");
        onenet_ota_upload_status(task_id, 106);
        goto ota_end;
    }

    onenet_ota_upload_status(task_id, 100);

    ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err == ESP_OK) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_app_desc_t *app_desc = esp_app_get_description();
        if(running && app_desc) {
            nvs_handle_t h = my_nvs_get_handle("ota");
            if (h != 0) {
                nvs_set_str(h, "prev_label", running->label);
                nvs_set_str(h, "prev_version", app_desc->version);
                nvs_commit(h);
            }
        }
        ota_resume_cleanup();
        ESP_LOGI(TAG, "OTA完成，断点已清理");
        onenet_ota_upload_status(task_id, 201);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA finish failed 0x%x, 断点已保留", ota_finish_err);
        if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
            onenet_ota_upload_status(task_id, 205);
        }
        goto ota_end;
    }

ota_end:
    ota_running = false;
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    vTaskDelete(NULL);
}

/* ==========================================================================
 * Static — System event handler
 * ========================================================================== */

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
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
        ESP_LOGI(TAG, "WiFi已连接");
    }
}

/* ==========================================================================
 * Public API — Lifecycle
 * ========================================================================== */

void ato_init(void)
{
    ESP_LOGI(TAG, "ATO模块初始化开始");
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    my_nvs_open_ns("ota_resumption");
    my_nvs_open_ns("ota");

    // 清理旧版脏数据（此前用URL代替MD5保存，URL长度>33字节会导致nvs_get_str失败）
    nvs_handle_t h = my_nvs_get_handle("ota_resumption");
    if (h != 0) {
        size_t len = 33;
        char test[33];
        if (nvs_get_str(h, "ota_md5", test, &len) != ESP_OK) {
            ESP_LOGW(TAG, "检测到旧版断点数据格式不兼容，自动清理");
            my_nvs_erase_all_ns("ota_resumption");
        }
    }
}

void ato_start(void)
{
    if (ota_download_url[0] == '\0') {
        ESP_LOGE(TAG, "未获取到固件下载地址，请先调用check_task");
        return;
    }
    if (ota_running) {
        ESP_LOGW(TAG, "OTA已在执行中，忽略重复启动");
        return;
    }
    ota_running = true;
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}

void ato_validate_app(void)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "固件验证通过，已取消回滚");
        }
    }
#endif
}

/* ==========================================================================
 * Public API — Getters
 * ========================================================================== */

const char *ota_get_target_version(void)
{
    return target_version;
}

int ota_get_task_id(void)
{
    return task_id;
}

int ota_get_firmware_size(void)
{
    return firmware_size;
}

int ota_get_progress(void)
{
    return s_ota_progress;
}

bool ota_is_running(void)
{
    return ota_running;
}

/* ==========================================================================
 * Public API — Manual rollback
 * ========================================================================== */

const char *ota_get_previous_version(void)
{
    nvs_handle_t h = my_nvs_get_handle("ota");
    if (h == 0) return NULL;

    static char prev_version[64];
    size_t len = sizeof(prev_version);
    memset(prev_version, 0, sizeof(prev_version));
    if (nvs_get_str(h, "prev_version", prev_version, &len) != ESP_OK) {
        return NULL;
    }
    return prev_version;
}

esp_err_t ota_rollback_to_previous(void)
{
    nvs_handle_t h = my_nvs_get_handle("ota");
    if (h == 0) {
        ESP_LOGE(TAG, "无法获取NVS句柄，回滚失败");
        return ESP_ERR_NOT_FOUND;
    }
    char prev_label[17];
    size_t len = sizeof(prev_label);
    if (nvs_get_str(h, "prev_label", prev_label, &len) != ESP_OK) {
        ESP_LOGE(TAG, "无法获取上一个固件，回滚失败");
        return ESP_ERR_NOT_FOUND;
    }
    const esp_partition_t *prev = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev_label);
    if (prev == NULL) {
        ESP_LOGE(TAG, "手动回滚: 找不到分区 %s", prev_label);
        return ESP_ERR_NOT_FOUND;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    if(running && strcmp(running->label, prev->label) == 0) {
        ESP_LOGW(TAG, "当前固件已经是上一个版本，无需回滚");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "手动回滚: 设置启动分区为 %s，即将重启", prev_label);
    esp_err_t err = esp_ota_set_boot_partition(prev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "手动回滚: esp_ota_set_boot_partition 失败 0x%x", err);
        return err;
    }

    nvs_erase_key(h, "prev_label");
    nvs_erase_key(h, "prev_version");
    nvs_commit(h);

    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}
