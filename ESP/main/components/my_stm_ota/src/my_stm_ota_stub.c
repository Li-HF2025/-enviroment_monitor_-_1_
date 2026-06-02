#include "my_stm_ota.h"
#include "stm_an3155.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "my_nvs.h"
#include "my_ota.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "mbedtls/md5.h"
#define STM_OTA_BOOT0_GPIO GPIO_NUM_14 // 控制STM32进入Bootloader模式(高电平)
#define STM_OTA_RESET_GPIO GPIO_NUM_13 // 控制STM32复位(低电平)
#define UART_PORT UART_NUM_1

#define STM_FW_MAX_SIZE (64 * 1024)    // STM32F103C8T6 官方 Flash 64KB

static uint8_t *s_fw_buffer     = NULL;   // 固件 RAM 缓冲区
static size_t   s_fw_size       = 0;      // 实际下载的字节数
static char     s_target_ver[32] = {0};   // 目标版本号
static char     s_fw_md5[33]    = {0};    // 固件 MD5
static char     s_download_url[256] = {0};// 下载地址
static int      s_stm_task_id   = 0;      // OneNET 任务 ID
static int      s_fw_capacity   = 0;      // 固件预期大小
static volatile int  s_progress = 0;      // 进度 0-100
static volatile bool s_stm_ota_running = false;  // OTA 进行中


static const uint8_t AN3155_AUTOBAUD = 0x7F;// 自动波特率同步字节
static const uint8_t AN3155_ACK      = 0x79;//成功 响应字节
// static const uint8_t AN3155_NACK     = 0x1F;// 失败 响应字节

static volatile bool g_an3155_in_use = false;// 标志位，表示 通讯间禁止使用串口

extern TaskHandle_t uart_rx_task_handle;
extern TaskHandle_t uart_tx_task_handle;

static const char *TAG = "STM_OTA";
static char s_stm_version[32] = "V0.0.0";  // 从 NVS 读取，默认 V0.0.0

void stm_ota_init(void)
{
    gpio_config_t boot0_config = {
        .pin_bit_mask = BIT64(STM_OTA_BOOT0_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot0_config);
    gpio_config_t reset_config = {
        .pin_bit_mask = BIT64(STM_OTA_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&reset_config);
    gpio_set_level(STM_OTA_RESET_GPIO,1);
    gpio_set_level(STM_OTA_BOOT0_GPIO, 0);
    ESP_LOGI(TAG, "STM32_OTA_GPIO 初始化成功");
    nvs_handle_t h = my_nvs_get_handle("ota");
    size_t len = sizeof(s_stm_version);
    nvs_get_str(h, "stm_version", s_stm_version, &len);
}

static void stm_ota_enter_bootloader(){
    gpio_set_level(STM_OTA_BOOT0_GPIO,1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(STM_OTA_RESET_GPIO,0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(STM_OTA_RESET_GPIO,1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void stm_ota_exit_bootloader(){
    gpio_set_level(STM_OTA_BOOT0_GPIO,0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(STM_OTA_RESET_GPIO,0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(STM_OTA_RESET_GPIO,1);
}

static void uart_switch_to_an3155(){
    g_an3155_in_use = true;
    //将串口任务挂起，防止干扰之后的an3155通讯
    vTaskSuspend(uart_rx_task_handle);
    vTaskSuspend(uart_tx_task_handle);
    //延迟等待最后的数据帧发送完成
    vTaskDelay(pdMS_TO_TICKS(100));
    //清空原有缓存区
    uart_flush_input(UART_PORT);
    uart_set_parity(UART_PORT, UART_PARITY_EVEN);//切换检验模式
}

static void uart_switch_to_normal(){
    uart_flush_input(UART_PORT);
    g_an3155_in_use = false;
    vTaskResume(uart_rx_task_handle);
    vTaskResume(uart_tx_task_handle);
    uart_set_parity(UART_PORT, UART_PARITY_DISABLE);
}

static esp_err_t an3155_check_ack(){
    uint8_t ack = 0;
    int read = uart_read_bytes(UART_PORT,&ack,1,pdMS_TO_TICKS(500));
    if(read != 1 || ack != AN3155_ACK) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t an3155_send_cmd(uint8_t cmd){
    uart_flush_input(UART_PORT);

    uint8_t buf[2] = {cmd , (uint8_t)~cmd};
    int written = uart_write_bytes(UART_PORT,buf,2);
    if(written != 2) return ESP_FAIL;

    return an3155_check_ack();
}

static esp_err_t an3155_auto_baud(){
    uart_flush_input(UART_PORT);
    int written = uart_write_bytes(UART_PORT,&AN3155_AUTOBAUD,1);
    if(written != 1) return ESP_FAIL; 

    uart_flush_input(UART_PORT);//清掉 bootloader 可能多发的残留字节

    return an3155_check_ack();
}

static esp_err_t an3155_send_address(uint32_t addr){
    uint8_t buf[5];
    buf[0] = (addr >> 24) & 0xFF; // 最高字节
    buf[1] = (addr >> 16) & 0xFF;
    buf[2] = (addr >> 8)  & 0xFF;
    buf[3] =  addr        & 0xFF; // 最低字节
    buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];

    int written = uart_write_bytes(UART_PORT,buf,5);
    if(written != 5) return ESP_FAIL;

    return an3155_check_ack();
}

static esp_err_t an3155_mass_erase(void)
{
    // 优先尝试 Extended Erase (0x44)，兼容性更好
    if (an3155_send_cmd(AN3155_CMD_EXTENDED_ERASE) == ESP_OK) {
        // 全局擦除码: 0xFFFF, XOR校验 = 0xFF ^ 0xFF = 0x00
        const uint8_t buf[] = {0xFF, 0xFF, 0x00};
        int written = uart_write_bytes(UART_PORT, buf, 3);
        if (written == 3 && an3155_check_ack() == ESP_OK) {
            ESP_LOGI(TAG, "全局擦除成功 (Extended Erase 0x44)");
            return ESP_OK;
        }
    }

    // 回退: 普通 Erase (0x43), 部分早期芯片只支持这个
    if (an3155_send_cmd(AN3155_CMD_ERASE) == ESP_OK) {
        // 0x43 的校验是取反(~), 不是 XOR
        const uint8_t buf[] = {0xFF, 0x00};  // 全局擦除 + ~0xFF
        int written = uart_write_bytes(UART_PORT, buf, 2);
        if (written == 2 && an3155_check_ack() == ESP_OK) {
            ESP_LOGI(TAG, "全局擦除成功 (Erase 0x43)");
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "全局擦除失败 (0x44 和 0x43 均失败)");
    return ESP_FAIL;
}

static esp_err_t an3155_write_memory(uint32_t addr, uint8_t* data, uint16_t len){
    if (len < 1 || len > 256) return ESP_FAIL;

    esp_err_t err;
    err = an3155_send_cmd(AN3155_CMD_WRITE_MEMORY);
    if(err != ESP_OK) return ESP_FAIL;

    err = an3155_send_address(addr);
    if(err != ESP_OK) return ESP_FAIL;

    uint8_t n = (uint8_t)(len - 1);
    uint8_t checksum = n;
    for (uint16_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }

    if (uart_write_bytes(UART_PORT, &n, 1) != 1)          return ESP_FAIL;
    if (uart_write_bytes(UART_PORT, data, len) != len)     return ESP_FAIL;
    if (uart_write_bytes(UART_PORT, &checksum, 1) != 1)   return ESP_FAIL;

    return an3155_check_ack();
}


static esp_err_t an3155_read_memory(uint32_t addr, uint8_t *data, uint16_t len){
    if (len < 1 || len > 256) return ESP_FAIL;

    if(an3155_send_cmd(AN3155_CMD_READ_MEMORY) == ESP_FAIL) return ESP_FAIL;

    if(an3155_send_address(addr) == ESP_FAIL) return ESP_FAIL;

    uint8_t n = (uint8_t)(len - 1);
    uint8_t buf[2] = {n, (uint8_t)~n};

    if(uart_write_bytes(UART_PORT,&buf,2) != 2) return ESP_FAIL;

    if(an3155_check_ack() != ESP_OK) return ESP_FAIL;

    if(uart_read_bytes(UART_PORT,data,len,pdMS_TO_TICKS(500)) != len) return ESP_FAIL;

    return ESP_OK;
}

esp_err_t stm_ota_self_test(void)
{
    // 测试数据: "Hello STM32! AN3155..." 凑满 256 字节
    static const uint8_t test_pattern[256] =
        "Hello STM32! AN3155 self-test passed! "
        "0123456789ABCDEF0123456789ABCDEF"
        "0123456789ABCDEF0123456789ABCDEF"
        "0123456789ABCDEF0123456789ABCDEF"
        "0123456789ABCDEF0123456789ABCDEF"
        "0123456789ABCDEF0123456789ABCDEF"
        "0123456789ABCDEF0123456789ABCDEF";

    uint8_t read_buf[256] = {0};
    uint8_t bl_ver = 0;
    uint16_t pid = 0;

    ESP_LOGI(TAG, "========== AN3155 自检开始 ==========");

    // Phase 1: 切换 UART 模式 + 进入 bootloader
    uart_switch_to_an3155();
    ESP_LOGI(TAG, "Phase 1: 进入 STM32 bootloader...");
    stm_ota_enter_bootloader();

    // Phase 2: 波特率探测
    ESP_LOGI(TAG, "Phase 2: 波特率探测...");
    if (an3155_auto_baud() != ESP_OK) {
        ESP_LOGE(TAG, "波特率探测失败，STM32 未响应");
        goto fail;
    }
    ESP_LOGI(TAG, "握手成功");

    // Phase 3: 读取 bootloader 版本和芯片 PID
    ESP_LOGI(TAG, "Phase 3: 读取芯片信息...");
    if (stm_ota_bootloader_get_version(&bl_ver) == ESP_OK) {
        ESP_LOGI(TAG, "Bootloader 版本: %d.%d", (bl_ver >> 4) & 0x0F, bl_ver & 0x0F);
    } else {
        ESP_LOGW(TAG, "读取 bootloader 版本失败（继续）");
    }
    if (stm_ota_bootloader_get_id(&pid) == ESP_OK) {
        ESP_LOGI(TAG, "芯片 PID: 0x%04X", pid);
    } else {
        ESP_LOGW(TAG, "读取 PID 失败（继续）");
    }

    // Phase 4: 全局擦除
    ESP_LOGI(TAG, "Phase 4: 全局擦除 Flash...");
    if (an3155_mass_erase() != ESP_OK) {
        ESP_LOGE(TAG, "全局擦除失败");
        goto fail;
    }

    // Phase 5: 写入测试数据
    ESP_LOGI(TAG, "Phase 5: 写入测试数据到 0x08000000...");
    if (an3155_write_memory(0x08000000, (uint8_t *)test_pattern, 256) != ESP_OK) {
        ESP_LOGE(TAG, "写入 Flash 失败");
        goto fail;
    }

    // Phase 6: 回读验证
    ESP_LOGI(TAG, "Phase 6: 回读验证...");
    if (an3155_read_memory(0x08000000, read_buf, 256) != ESP_OK) {
        ESP_LOGE(TAG, "读取 Flash 失败");
        goto fail;
    }

    if (memcmp(test_pattern, read_buf, 256) == 0) {
        ESP_LOGI(TAG, "验证通过！写入数据与读取数据完全一致");
    } else {
        ESP_LOGE(TAG, "验证失败！写入与读取不一致");
        // 打印前 32 字节对比
        ESP_LOGE(TAG, "期望: %.*s", 32, test_pattern);
        ESP_LOGE(TAG, "实际: %.*s", 32, read_buf);
        goto fail;
    }

    // Phase 7: 恢复 STM32 正常运行
    ESP_LOGI(TAG, "Phase 7: 恢复正常模式...");
    stm_ota_exit_bootloader();
    uart_switch_to_normal();

    ESP_LOGI(TAG, "========== AN3155 自检通过 ==========");
    return ESP_OK;

fail:
    stm_ota_exit_bootloader();
    uart_switch_to_normal();
    ESP_LOGE(TAG, "========== AN3155 自检失败 ==========");
    return ESP_FAIL;
}


static esp_err_t an3155_go(uint32_t addr){
    esp_err_t err;
    err = an3155_send_cmd(AN3155_CMD_GO);
    if(err != ESP_OK) return ESP_FAIL;

    return an3155_send_address(addr);
}

esp_err_t stm_ota_bootloader_get_version(uint8_t *ver){

    uart_flush_input(UART_PORT);

    esp_err_t err;
    err = an3155_send_cmd(AN3155_CMD_GET_VERSION);
    if(err != ESP_OK) return ESP_FAIL;

    int read = uart_read_bytes(UART_PORT,ver,1,pdMS_TO_TICKS(500));
    if(read != 1) return ESP_FAIL;

    uint8_t dummy;
    while (uart_read_bytes(UART_PORT, &dummy, 1, pdMS_TO_TICKS(100)) == 1) {
        if (dummy == AN3155_ACK) return ESP_OK;  // 找到尾部 ACK
    }
    return ESP_FAIL;
}

esp_err_t stm_ota_bootloader_get_id(uint16_t *pid){
    uart_flush_input(UART_PORT);
    esp_err_t err = an3155_send_cmd(AN3155_CMD_GET_ID);
    if(err != ESP_OK) return ESP_FAIL;

    uint8_t n;
    if (uart_read_bytes(UART_PORT, &n, 1, pdMS_TO_TICKS(500)) != 1) return ESP_FAIL;

    uint8_t buf[2];
    if (uart_read_bytes(UART_PORT, buf, 2, pdMS_TO_TICKS(500)) != 2) return ESP_FAIL;
    
    *pid = ((uint16_t)buf[0] << 8) | buf[1];

    return an3155_check_ack();
}


void stm_ota_deinit(void)
{
    ESP_LOGW(TAG, "stm_ota_deinit: not implemented");
}

esp_err_t stm_ota_upload_version(void)
{
    return onenet_ota_upload_version_separate(
        ota_get_current_version(),   
        s_stm_version
    );
}

esp_err_t stm_ota_check_task(const char *version)
{
    if(onenet_ota_check_task("2",version) != ESP_OK) return ESP_FAIL;

    strncpy(s_target_ver,ota_get_target_version(),sizeof(s_target_ver)-1);
    strncpy(s_fw_md5, ota_get_firmware_md5(), sizeof(s_fw_md5)-1);
    strncpy(s_download_url, ota_get_download_url(), sizeof(s_download_url)-1);
    s_stm_task_id = ota_get_task_id();      
    s_fw_capacity = ota_get_firmware_size();

    return ESP_OK;
}

esp_err_t stm_ota_upload_status(int step)
{
    return onenet_ota_upload_status(s_stm_task_id, step);
}

esp_err_t stm_ota_download_firmware(const char *url, const char *expected_md5)
{

    if(s_fw_capacity > STM_FW_MAX_SIZE || s_fw_capacity < 1){
        ESP_LOGW(TAG,"数据包大于默认缓存区");
        return ESP_FAIL;
    }

    s_fw_buffer = heap_caps_malloc(STM_FW_MAX_SIZE,MALLOC_CAP_SPIRAM);
    if(s_fw_buffer == NULL){
        s_fw_buffer = heap_caps_malloc(STM_FW_MAX_SIZE,MALLOC_CAP_8BIT);
        if(s_fw_buffer == NULL) return ESP_ERR_NO_MEM;
    }

    if(onenet_ota_fetch_firmware(url,s_fw_buffer,STM_FW_MAX_SIZE,&s_fw_size) != ESP_OK){
        free(s_fw_buffer);
        s_fw_buffer = NULL;
        s_fw_size = 0;
        return ESP_FAIL;
    }

    // MD5 校验（如果 OneNET 返回了 md5 值）
    if (expected_md5 != NULL && expected_md5[0] != '\0') {
        unsigned char digest[16];
        mbedtls_md5_context md5_ctx;
        mbedtls_md5_init(&md5_ctx);
        mbedtls_md5_starts(&md5_ctx);
        mbedtls_md5_update(&md5_ctx, s_fw_buffer, s_fw_size);
        mbedtls_md5_finish(&md5_ctx, digest);
        mbedtls_md5_free(&md5_ctx);

        char computed[33];
        for (int i = 0; i < 16; i++) {
            snprintf(&computed[i * 2], 3, "%02x", digest[i]);
        }
        computed[32] = '\0';

        if (strcasecmp(computed, expected_md5) != 0) {
            ESP_LOGE(TAG, "MD5 校验失败: 期望=%s, 计算=%s", expected_md5, computed);
            free(s_fw_buffer);
            s_fw_buffer = NULL;
            s_fw_size = 0;
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "MD5 校验通过");
    }

    s_progress =100;
    return ESP_OK;

}

esp_err_t stm_ota_flash(void)
{
    uint8_t bl_ver = 0;
    uint16_t pid = 0;
    if(s_fw_buffer == NULL || s_fw_size <= 0){
        ESP_LOGW(TAG,"固件还未下载");
        return ESP_FAIL;
    }
    s_stm_ota_running = true;
    // Phase 1: 切换 UART 模式 + 进入 bootloader
    uart_switch_to_an3155();
    ESP_LOGI(TAG, "Phase 1: 进入 STM32 bootloader...");
    stm_ota_enter_bootloader();

    // Phase 2: 波特率探测
    ESP_LOGI(TAG, "Phase 2: 波特率探测...");
    if (an3155_auto_baud() != ESP_OK) {
        ESP_LOGE(TAG, "波特率探测失败，STM32 未响应");
        goto fail;
    }
    ESP_LOGI(TAG, "握手成功");

    // Phase 3: 读取 bootloader 版本和芯片 PID
    ESP_LOGI(TAG, "Phase 3: 读取芯片信息...");
    if (stm_ota_bootloader_get_version(&bl_ver) == ESP_OK) {
        ESP_LOGI(TAG, "Bootloader 版本: %d.%d", (bl_ver >> 4) & 0x0F, bl_ver & 0x0F);
    } else {
        ESP_LOGW(TAG, "读取 bootloader 版本失败（继续）");
    }
    if (stm_ota_bootloader_get_id(&pid) == ESP_OK) {
        ESP_LOGI(TAG, "芯片 PID: 0x%04X", pid);
    } else {
        ESP_LOGW(TAG, "读取 PID 失败（继续）");
    }
    // Phase 4: 全局擦除
    ESP_LOGI(TAG, "Phase 4: 全局擦除 Flash...");
    if (an3155_mass_erase() != ESP_OK) {
        ESP_LOGE(TAG, "全局擦除失败");
        goto fail;
    }
    // Phase 5: 写入数据
    ESP_LOGI(TAG, "Phase 5: 写入测试数据到 0x08000000...");
    size_t written = 0;
    uint32_t flash_addr = 0x08000000;
    while(written < s_fw_size){
        size_t over_buf = s_fw_size - written;
        size_t buf = (over_buf >= 256) ? 256 : over_buf;

        if (an3155_write_memory(flash_addr,s_fw_buffer + written,buf) != ESP_OK) {
            ESP_LOGE(TAG, "写入失败 at 0x%08" PRIX32, flash_addr);
            goto fail;
        }

        flash_addr += buf;
        written += buf;

        s_progress = (written * 100)/s_fw_size;
    }

    // Phase 6: 保存版本，释放资源
    nvs_handle_t h = my_nvs_get_handle("ota");
    if (h != 0) {
        nvs_set_str(h, "stm_version", s_target_ver);
        nvs_commit(h);
        strncpy(s_stm_version, s_target_ver, sizeof(s_stm_version) - 1);
    }
    free(s_fw_buffer);
    s_fw_buffer = NULL;

    // Phase 7: 恢复正常模式
    ESP_LOGI(TAG, "Phase 7: 恢复正常模式...");
    an3155_go(0x08000000);
    stm_ota_exit_bootloader();
    uart_switch_to_normal();
    s_stm_ota_running = false;
    ESP_LOGI(TAG, "STM固件下载成功");
    return ESP_OK;
fail:
    stm_ota_exit_bootloader();
    uart_switch_to_normal();
    s_stm_ota_running = false;
    ESP_LOGE(TAG, "STM固件下载失败");
    return ESP_FAIL;
}

/* ==========================================================================
 * OTA 任务 — 独立 FreeRTOS 线程，不阻塞 UI
 * ========================================================================== */

static void stm_ota_task(void *pvParameter)
{
    (void)pvParameter;

    ESP_LOGI(TAG, "STM32 OTA 任务开始");

    if (stm_ota_download_firmware(s_download_url, s_fw_md5) != ESP_OK) {
        ESP_LOGE(TAG, "STM32 OTA: 固件下载失败");
        s_stm_ota_running = false;
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "STM32 OTA: 开始烧写固件");
    stm_ota_flash();
    // stm_ota_flash 内部已处理 s_stm_ota_running 和清理
    vTaskDelete(NULL);
}

void stm_ota_start(void)
{
    if (s_stm_ota_running) {
        ESP_LOGW(TAG, "STM32 OTA 已在运行，忽略重复启动");
        return;
    }
    if (s_download_url[0] == '\0') {
        ESP_LOGE(TAG, "未查询到 STM32 OTA 任务，请先 check_task");
        return;
    }
    s_progress = 0;
    xTaskCreate(&stm_ota_task, "stm_ota_task", 8192, NULL, 5, NULL);
}

int stm_ota_get_progress(void)  { return s_progress; }
const char *stm_ota_get_target_version(void) { return s_target_ver; }
int stm_ota_get_firmware_size(void) { return s_fw_capacity; }
bool stm_ota_is_running(void) { return s_stm_ota_running; }
const char *stm_ota_get_stm_version(void){ return s_stm_version;}

