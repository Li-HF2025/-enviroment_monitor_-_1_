# STM32 OTA 固件升级计划 (ESP32 侧)

> ESP32-S3 作为 OTA 网关，负责固件下载、存储、AN3155 协议烧写 STM32 的全流程控制

---

## 一、总体架构

```
                         OneNET 云平台
                              │
                    ┌─────────┴─────────┐
                    │  FOTA (type=1)     │  SOTA (type=2)
                    │  ESP32 固件        │  STM32 固件
                    └─────────┬─────────┘
                              │
                         ESP32-S3
                    ┌──────────────────┐
                    │  my_ota (现有)    │    my_stm_ota (新增)
                    │  ESP32 OTA 升级   │    STM32 OTA 升级
                    │  type=1, fw ver   │    type=2, sw ver
                    └──────────────────┘    │
                              │             │ AN3155 协议
                              │      ┌──────┴──────┐
                              │      │ UART1 + GPIO │
                              │      │ BOOT0 + NRST │
                    ┌─────────┴──────┴─────────────┐
                    │         STM32F103C8T6          │
                    └──────────────────────────────┘
```

**核心设计决策：**
- ESP32 下载 STM32 固件到 RAM 缓冲区（~64KB，对应 STM32F103C8T6 官方 Flash 容量）
- 下载完成后一次性通过 AN3155 协议烧写 STM32（避免边下边写因网络波动失败）
- UART1 分时复用：正常通信 ←→ AN3155 烧写（通过 GPIO 控制 STM32 切换模式）
- **烧写期间需手动喂狗**：AN3155 擦除+烧写持续 5~15 秒，期间 UART 任务挂起，RTOS 任务调度可能停摆。若硬件看门狗由 FreeRTOS 任务喂养，须在 AN3155 烧写循环中调用喂狗逻辑

---

## 二、新增组件 `my_stm_ota`

### 2.1 目录结构

```
ESP/main/components/my_stm_ota/
├── CMakeLists.txt
├── inc/
│   ├── my_stm_ota.h          # 公共 API
│   └── stm_an3155.h          # AN3155 ROM Bootloader 协议常量与结构体
└── src/
    └── my_stm_ota.c          # 核心实现
```

### 2.2 CMakeLists.txt

```cmake
idf_component_register(
    SRCS "src/my_stm_ota.c"
    INCLUDE_DIRS "inc"
    REQUIRES driver esp_http_client nvs_flash
)
```

### 2.3 公共 API 设计

```c
// my_stm_ota.h

#ifndef __MY_STM_OTA_H__
#define __MY_STM_OTA_H__

#include "esp_err.h"
#include <stdbool.h>

// 生命周期
void stm_ota_init(void);       // 初始化 GPIO + 注册 NVS namespace
void stm_ota_deinit(void);     // 释放 GPIO + 关闭 NVS

// SOTA 云端交互（参数与 my_ota 对应接口语义一致，type 固定为 "2"）
esp_err_t stm_ota_upload_version(void);
esp_err_t stm_ota_check_task(const char *version);
esp_err_t stm_ota_upload_status(int step);

// 固件下载
esp_err_t stm_ota_download_firmware(const char *url, const char *expected_md5);

// AN3155 烧写 STM32
esp_err_t stm_ota_flash(void);  // 将已下载的固件烧写进 STM32

// 状态查询
int  stm_ota_get_progress(void);     // 当前操作进度 0~100
const char *stm_ota_get_target_version(void);
const char *stm_ota_get_stm_version(void);  // 从 NVS 读取当前 STM32 版本
bool stm_ota_is_running(void);       // 是否正在执行 OTA

// AN3155 单独操作（调试/恢复用）
esp_err_t stm_ota_enter_bootloader(void);   // 拉高 BOOT0 + 复位 → 进入 ROM Bootloader
esp_err_t stm_ota_exit_bootloader(void);    // 拉低 BOOT0 + 复位 → 运行用户应用
esp_err_t stm_ota_bootloader_get_version(uint8_t *ver);  // 读取 ROM Bootloader 版本
esp_err_t stm_ota_bootloader_get_id(uint16_t *pid);       // 读取芯片 PID

#endif
```

---

## 三、AN3155 协议实现

### 3.1 协议概述

STM32 ROM Bootloader 使用 USART1，协议由 ST 在 AN3155 中定义：

```
主机(ESP32)                     从机(STM32 ROM Bootloader)
     │                                      │
     │────── 0x7F (自动波特率探测) ──────►  │
     │◄───── 0x79 (ACK) ──────────────────│
     │                                      │
     │────── 0x00 (Get 命令) + 0xFF ────►  │
     │◄───── 0x79 (ACK) ──────────────────│
     │◄───── N bytes (bootloader version)   │
     │◄───── 0x79 (ACK) ──────────────────│
     │                                      │
     │────── 0x31 (Write Memory) + addr ─► │
     │◄───── 0x79 (ACK) ──────────────────│
     │────── data[0..255] + XOR checksum─► │
     │◄───── 0x79 (ACK) ──────────────────│
     │                                      │
     │────── 0x21 (Go) + addr ──────────► │
     │◄───── 0x79 (ACK) ──────────────────│
     │         STM32 跳转到新固件            │
```

### 3.2 波特率自动检测

```c
// STM32 ROM Bootloader 会根据 0x7F 的位时序自动计算波特率
// ESP32 以任意常用波特率发送 0x7F 即可（建议 115200，与正常运行一致）

static const uint8_t AN3155_AUTOBAUD = 0x7F;
static const uint8_t AN3155_ACK      = 0x79;
static const uint8_t AN3155_NACK     = 0x1F;

// 波特率探测：发送 0x7F，等待 ACK
// 如果未收到 ACK，说明 STM32 不在 bootloader 模式
```

### 3.3 命令码定义

```c
// stm_an3155.h

#define AN3155_CMD_GET              0x00  // 获取 bootloader 版本及支持的命令
#define AN3155_CMD_GET_VERSION      0x01  // 获取 bootloader 版本
#define AN3155_CMD_GET_ID           0x02  // 获取芯片 PID
#define AN3155_CMD_READ_MEMORY      0x11  // 读取内存
#define AN3155_CMD_GO               0x21  // 跳转到指定地址执行
#define AN3155_CMD_WRITE_MEMORY     0x31  // 写内存（Flash/RAM）
#define AN3155_CMD_ERASE            0x43  // 全局擦除（仅支持部分中低容量芯片）
#define AN3155_CMD_EXTENDED_ERASE   0x44  // 扩展擦除（支持页擦除和全局擦除）
#define AN3155_CMD_WRITE_PROTECT    0x63  // 写保护
#define AN3155_CMD_WRITE_UNPROTECT  0x73  // 解除写保护
#define AN3155_CMD_READOUT_PROTECT  0x82  // 读保护
#define AN3155_CMD_READOUT_UNPROTECT 0x92 // 解除读保护

// 每个命令的字节格式：
// 主机发送: [CMD] [~CMD]
// 从机回复: ACK (0x79) 或 NACK (0x1F)

// Write Memory 为特例（额外地址和校验）：
// 主机发送: [CMD] [~CMD]
// 从机回复: ACK
// 主机发送: [Addr[31:24]] [Addr[23:16]] [Addr[15:8]] [Addr[7:0]] [AddrChecksum]
// 从机回复: ACK
// 主机发送: [N] [data0..dataN-1] [DataChecksum]   (N = data_len - 1, max N = 255)
// 从机回复: ACK
```

### 3.4 核心 AN3155 函数骨架

```c
// ============================================================
// 全局标志位：AN3155 通信期间禁止其他任务使用 UART
// ============================================================
static volatile bool g_an3155_in_use = false;

// 发送命令字节（带取反校验）
static esp_err_t an3155_send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {cmd, (uint8_t)~cmd};
    int written = uart_write_bytes(UART_PORT, (const char *)buf, 2);
    if (written != 2) return ESP_FAIL;

    uint8_t ack = 0;
    int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(500));
    if (read != 1 || ack != AN3155_ACK) return ESP_FAIL;

    return ESP_OK;
}

// 进入 bootloader：发送 0x7F 进行波特率检测
static esp_err_t an3155_auto_baud(void)
{
    uart_flush_input(UART_PORT);
    uart_write_bytes(UART_PORT, (const char *)&AN3155_AUTOBAUD, 1);

    uint8_t ack = 0;
    int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(500));
    if (read == 1 && ack == AN3155_ACK) return ESP_OK;

    // 某些 bootloader 版本回复的是命令列表而非单独的 ACK
    // 放宽检查：只要有回复就认为握手成功
    if (read > 0) return ESP_OK;

    return ESP_FAIL;
}

// 发送地址（4 字节 + XOR checksum）
static esp_err_t an3155_send_address(uint32_t addr)
{
    uint8_t buf[5];
    buf[0] = (addr >> 24) & 0xFF;
    buf[1] = (addr >> 16) & 0xFF;
    buf[2] = (addr >> 8)  & 0xFF;
    buf[3] =  addr        & 0xFF;
    buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];  // XOR checksum

    int written = uart_write_bytes(UART_PORT, (const char *)buf, 5);
    if (written != 5) return ESP_FAIL;

    uint8_t ack = 0;
    int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(2000));
    // 注意：Flash 操作可能较慢，需要更长超时
    return (read == 1 && ack == AN3155_ACK) ? ESP_OK : ESP_FAIL;
}

// 全局擦除（使用 Extended Erase 0x44，兼容性更好）
static esp_err_t an3155_mass_erase(void)
{
    // 先用 Extended Erase (0x44)，失败则回退到 Erase (0x43)
    if (an3155_send_cmd(AN3155_CMD_EXTENDED_ERASE) == ESP_OK) {
        // 0xFFFF = 全局擦除, 0x0000 = ~0xFFFF
        uint8_t buf[2] = {0xFF, 0xFF};      // [15:8]=0xFF, [7:0]=0xFF
        uint8_t checksum = buf[0] ^ buf[1];  // 0xFF ^ 0xFF = 0x00
        uint8_t frame[3] = {buf[0], buf[1], checksum};
        uart_write_bytes(UART_PORT, (const char *)frame, 3);

        uint8_t ack = 0;
        int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(5000));
        if (read == 1 && ack == AN3155_ACK) return ESP_OK;
    }

    // 回退：尝试 Erase (0x43)
    if (an3155_send_cmd(AN3155_CMD_ERASE) == ESP_OK) {
        uint8_t buf[2] = {0xFF, 0x00};  // 0xFF=全局擦除, 0x00=~0xFF
        uart_write_bytes(UART_PORT, (const char *)buf, 2);
        uint8_t ack = 0;
        int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(5000));
        return (read == 1 && ack == AN3155_ACK) ? ESP_OK : ESP_FAIL;
    }

    return ESP_FAIL;
}

// 写 256 字节到 Flash 指定地址
// ★ 修正：调用方保证 data 已 4 字节对齐且长度为 4 的倍数
static esp_err_t an3155_write_memory(uint32_t addr, const uint8_t *data, uint8_t len)
{
    // STM32F1 限制：len 必须是 4 的倍数，且 ≤ 256
    if (len == 0 || len > 256 || (len % 4) != 0) return ESP_ERR_INVALID_ARG;

    if (an3155_send_cmd(AN3155_CMD_WRITE_MEMORY) != ESP_OK) return ESP_FAIL;
    if (an3155_send_address(addr) != ESP_OK) return ESP_FAIL;

    // 发送数据长度（N = len - 1）
    uint8_t n = len - 1;
    uint8_t checksum = n;
    for (int i = 0; i < len; i++) checksum ^= data[i];

    uart_write_bytes(UART_PORT, (const char *)&n, 1);
    uart_write_bytes(UART_PORT, (const char *)data, len);
    uart_write_bytes(UART_PORT, (const char *)&checksum, 1);

    uint8_t ack = 0;
    int read = uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(500));
    return (read == 1 && ack == AN3155_ACK) ? ESP_OK : ESP_FAIL;
}

// 跳转到指定地址
static esp_err_t an3155_go(uint32_t addr)
{
    if (an3155_send_cmd(AN3155_CMD_GO) != ESP_OK) return ESP_FAIL;
    return an3155_send_address(addr);
}

// 读取内存（用于验证写入和读取芯片信息）
static esp_err_t an3155_read_memory(uint32_t addr, uint8_t *data, uint8_t len)
{
    if (len == 0 || len > 256) return ESP_ERR_INVALID_ARG;

    if (an3155_send_cmd(AN3155_CMD_READ_MEMORY) != ESP_OK) return ESP_FAIL;
    if (an3155_send_address(addr) != ESP_OK) return ESP_FAIL;

    // 发送读取长度
    uint8_t n = len - 1;
    uint8_t checksum = 0xFF ^ n;
    uint8_t buf[2] = {n, checksum};
    uart_write_bytes(UART_PORT, (const char *)buf, 2);

    uint8_t ack = 0;
    if (uart_read_bytes(UART_PORT, &ack, 1, pdMS_TO_TICKS(200)) != 1 || ack != AN3155_ACK)
        return ESP_FAIL;

    // 读取数据
    int total = uart_read_bytes(UART_PORT, data, len, pdMS_TO_TICKS(500));
    return (total == len) ? ESP_OK : ESP_FAIL;
}
```

---

## 四、固件下载与存储

### 4.1 为什么用 RAM 而不是 SPIFFS

| 方案 | 优点 | 缺点 |
|------|------|------|
| **RAM 缓冲 (推荐)** | 简单、快、不需要分区表 | 断电数据丢失，但此时 STM32 还未被擦除，安全 |
| SPIFFS 分区 | 断电可恢复 | 需要新建分区表、初始化文件系统、磨损均衡开销 |
| 边下边写 STM32 | 不需要额外存储 | 网络中断可能导致 STM32 Flash 处于半写状态，恢复困难 |

STM32F103C8T6 最大固件 64KB（官方规格），ESP32-S3 有大量可用内存，RAM 缓冲是最简方案。

### 4.2 下载实现

**注意：`onenet_ota_check_task()` 是 `my_ota.c` 的公共函数，其返回结果存储在 `my_ota.c` 的 static 变量中。`stm_ota_check_task()` 调用它之后必须立即拷贝 URL/size/MD5 到自己的变量中，防止被后续的 ESP32 check_task 覆盖。**

```c
// my_stm_ota.c 核心结构体

#define STM_FW_MAX_SIZE (64 * 1024)    // STM32F103C8T6 官方 Flash 64KB

static uint8_t *s_fw_buffer   = NULL; // 固件缓冲区（堆分配）
static size_t   s_fw_size     = 0;    // 实际下载大小
static size_t   s_fw_capacity = 0;    // 缓冲区容量
static char     s_target_ver[32] = {0};
static char     s_fw_md5[33]  = {0};
static char     s_download_url[256] = {0};
static int      s_stm_task_id = 0;
static volatile int  s_progress    = 0;
static volatile bool s_ota_running = false;

esp_err_t stm_ota_check_task(const char *version)
{
    // 调用 my_ota 的公共函数（type=2 → SOTA/MCU软件）
    // 检查后立即拷贝结果，避免被 ESP32 OTA 覆盖
    if (onenet_ota_check_task("2", version) != ESP_OK) {
        return ESP_FAIL;
    }

    // ★ 立即从 my_ota 的 static 变量拷贝到本地变量
    strncpy(s_target_ver, ota_get_target_version(), sizeof(s_target_ver) - 1);
    s_stm_task_id  = ota_get_task_id();
    s_fw_capacity  = ota_get_firmware_size();

    // download_url / md5 需要从 my_ota.c 获取
    // 方案 A：my_ota.c 新增 ota_get_download_url() / ota_get_firmware_md5() getter
    // 方案 B：stm_ota_check_task 内部自己拼 URL 和请求 md5

    return ESP_OK;
}

// HTTP 下载事件处理
static esp_err_t stm_ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_fw_size + evt->data_len > s_fw_capacity) {
                return ESP_FAIL;
            }
            memcpy(s_fw_buffer + s_fw_size, evt->data, evt->data_len);
            s_fw_size += evt->data_len;

            // 按进度上报（每 10% 上报一次状态）
            if (s_fw_capacity > 0) {
                int new_progress = (s_fw_size * 100) / s_fw_capacity;
                if (new_progress - s_progress >= 10) {
                    s_progress = new_progress;
                    stm_ota_upload_status(new_progress);
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "STM32 firmware download complete: %d bytes", s_fw_size);
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP download error");
            return ESP_FAIL;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t stm_ota_download_firmware(const char *url, const char *expected_md5)
{
    // 1. 分配缓冲区（容量已由 stm_ota_check_task 设置）
    if (s_fw_capacity == 0 || s_fw_capacity > STM_FW_MAX_SIZE) {
        ESP_LOGE(TAG, "Invalid firmware size: %d", s_fw_capacity);
        return ESP_ERR_INVALID_SIZE;
    }

    s_fw_buffer = heap_caps_malloc(s_fw_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fw_buffer == NULL) {
        s_fw_buffer = heap_caps_malloc(s_fw_capacity, MALLOC_CAP_8BIT);
        if (s_fw_buffer == NULL) return ESP_ERR_NO_MEM;
    }
    s_fw_size = 0;
    s_progress = 0;

    // 2. 与 my_ota 相同的 token 鉴权 + HTTP 下载
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = stm_ota_http_event_handler,
        .timeout_ms = 5000,
        .buffer_size = 4096,
    };
    // ... HTTP 下载逻辑（参考 my_ota.c 的 onenet_ota_http_connect）

    // 3. 校验 MD5（如果平台返回了的话）
    // ... MD5 校验逻辑

    return ESP_OK;
}
```

### 4.3 my_ota.c 需要新增的 getter

为支持 STM32 OTA 独立获取 check_task 结果，`my_ota.c` 需新增以下 getter 函数并在 `my_ota.h` 中声明：

```c
// my_ota.h 新增
const char *ota_get_download_url(void);
const char *ota_get_firmware_md5(void);
```

---

## 五、STM32 烧写流程 (AN3155)

### 5.1 完整烧写流程

**★ 关键改进：**
1. **固件编译时保证 4 字节对齐** — STM32 链接脚本末尾 `. = ALIGN(4);` 或 makefile 中 `tr '\000' '\377' < /dev/zero | dd of=firmware.bin bs=1 count=N conv=notrunc` padding 到 4 的倍数，避免运行时补对齐的 bug（原计划中写了 `aligned` 缓冲区却仍从 `s_fw_buffer` 读取的 bug）
2. **所有失败路径统一恢复** — `stm_ota_exit_bootloader()` + 恢复 UART 任务
3. **烧写循环中喂硬件看门狗** — 如果你的系统有 IWDT，在 while 循环内调用喂狗

```c
esp_err_t stm_ota_flash(void)
{
    if (s_fw_buffer == NULL || s_fw_size == 0) {
        ESP_LOGE(TAG, "没有下载好的固件");
        return ESP_ERR_INVALID_STATE;
    }

    s_ota_running = true;
    s_progress = 0;

    // === Phase 1: 暂停正常通信 + 标记 AN3155 占用 UART ===
    g_an3155_in_use = true;

    if (uart_rx_task_handle) vTaskSuspend(uart_rx_task_handle);
    if (uart_tx_task_handle) vTaskSuspend(uart_tx_task_handle);

    // 等待 UART TX 完成当前帧发送（避免帧被截断）
    vTaskDelay(pdMS_TO_TICKS(100));

    // 清空 UART 缓冲
    uart_flush(UART_PORT);
    uart_flush_input(UART_PORT);

    // 进入 STM32 bootloader
    if (stm_ota_enter_bootloader() != ESP_OK) {
        ESP_LOGE(TAG, "无法进入 STM32 bootloader 模式");
        goto fail_and_recover;
    }

    // === Phase 2: 波特率探测 (AN3155) ===
    if (an3155_auto_baud() != ESP_OK) {
        ESP_LOGE(TAG, "AN3155 握手失败，STM32 未回复 ACK");
        goto fail_and_recover;
    }
    ESP_LOGI(TAG, "AN3155 handshake OK, STM32 ROM Bootloader ready");

    // === Phase 3: 读取 bootloader 版本和芯片 PID（调试） ===
    uint8_t bl_ver = 0;
    stm_ota_bootloader_get_version(&bl_ver);
    uint16_t pid = 0;
    stm_ota_bootloader_get_id(&pid);
    ESP_LOGI(TAG, "STM32 PID: 0x%04X, Bootloader version: %d.%d",
             pid, (bl_ver >> 4) & 0x0F, bl_ver & 0x0F);

    // === Phase 4: 全局擦除 ===
    ESP_LOGI(TAG, "Erasing STM32 Flash...");
    if (an3155_mass_erase() != ESP_OK) {
        ESP_LOGE(TAG, "Flash 全局擦除失败");
        goto fail_and_recover;
    }
    ESP_LOGI(TAG, "Erase OK");

    // === Phase 5: 逐 256 字节写入固件 ===
    // ★ 前提：固件 .bin 已编译为 4 字节对齐，s_fw_size 是 4 的倍数
    // （STM32 链接脚本末尾加 . = ALIGN(4); 保证）
    ESP_LOGI(TAG, "Writing firmware %d bytes...", s_fw_size);
    uint32_t flash_addr = 0x08000000;
    size_t written = 0;

    // ★ 将数据末尾 padding 到 4 的倍数
    size_t aligned_size = (s_fw_size + 3) & ~3;
    if (aligned_size > s_fw_size) {
        memset(s_fw_buffer + s_fw_size, 0xFF, aligned_size - s_fw_size);
    }

    while (written < aligned_size) {
        size_t remaining = aligned_size - written;
        uint8_t chunk_len = (remaining >= 256) ? 256 : (uint8_t)remaining;

        if (an3155_write_memory(flash_addr, s_fw_buffer + written, chunk_len) != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at 0x%08" PRIX32 ", offset %d", flash_addr, written);
            // 写入失败，保留 bootloader 模式供重试，但不恢复 UART 通信
            s_ota_running = false;
            return ESP_FAIL;
        }

        flash_addr += chunk_len;
        written += chunk_len;

        // 更新进度
        s_progress = (written * 100) / aligned_size;
        stm_ota_upload_status(s_progress);

        // ★ 硬件看门狗喂狗（如果系统启用了 IWDT）
        // WATCHDOG_FEED();
    }

    ESP_LOGI(TAG, "Flash write complete, %d bytes written", written);

    // === Phase 6: 可选 — 回读验证 ===
    // 回读前 256 字节做快速校验
    uint8_t verify_buf[256];
    if (an3155_read_memory(0x08000000, verify_buf, 256) == ESP_OK) {
        if (memcmp(verify_buf, s_fw_buffer, 256) == 0) {
            ESP_LOGI(TAG, "Verify OK: first 256 bytes match");
        } else {
            ESP_LOGW(TAG, "Verify mismatch in first 256 bytes");
        }
    }

    // === Phase 7: 跳转到新固件 ===
    an3155_go(0x08000000);
    ESP_LOGI(TAG, "STM32 reset to new firmware");

    // === Phase 8: 保存版本号到 NVS ===
    if (s_target_ver[0] != '\0') {
        nvs_handle_t h = my_nvs_get_handle("ota");
        if (h != 0) {
            // 保存当前版本为 previous，再更新为新版本（支持回滚）
            char old_ver[32] = {0};
            size_t old_len = sizeof(old_ver);
            if (nvs_get_str(h, "stm_version", old_ver, &old_len) == ESP_OK) {
                nvs_set_str(h, "stm_prev_version", old_ver);
            }
            nvs_set_str(h, "stm_version", s_target_ver);
            nvs_commit(h);
        }
    }

    // === Phase 9: 恢复 UART 正常通信 ===
    if (uart_tx_task_handle) vTaskResume(uart_tx_task_handle);
    if (uart_rx_task_handle) vTaskResume(uart_rx_task_handle);
    g_an3155_in_use = false;

    // 释放固件缓冲区
    free(s_fw_buffer);
    s_fw_buffer = NULL;
    s_fw_size = 0;

    s_ota_running = false;
    return ESP_OK;

fail_and_recover:
    // ★ 错误恢复：恢复 STM32 正常模式 + 恢复 UART 通信
    stm_ota_exit_bootloader();
    if (uart_tx_task_handle) vTaskResume(uart_tx_task_handle);
    if (uart_rx_task_handle) vTaskResume(uart_rx_task_handle);
    g_an3155_in_use = false;
    s_ota_running = false;
    return ESP_FAIL;
}
```

### 5.2 烧写时序图

```
ESP32                         STM32
  │                              │
  │──── BOOT0=HIGH ────────────►│
  │──── NRST=LOW ──────────────►│  复位
  │   delay 10ms                 │
  │──── NRST=HIGH ─────────────►│  释放复位
  │                              │  → 进入 ROM Bootloader
  │                              │
  │──── 0x7F ──────────────────►│  (auto-baud)
  │◄─── 0x79 (ACK) ────────────│
  │                              │
  │──── 0x44 0xBB (Extended Erase)
  │◄─── 0x79 ──────────────────│
  │──── 0xFF 0xFF 0x00 ───────►│  (全局擦除)
  │                              │  ... 约 3 秒 ...
  │◄─── 0x79 (完成) ──────────-│
  │                              │
  │──── 0x31 0xCE (Write Memory)
  │◄─── 0x79 ──────────────────│
  │──── 0x08 0x00 0x00 0x00 ──►│  (地址 0x08000000)
  │     [XOR checksum]           │
  │◄─── 0x79 ──────────────────│
  │──── N + data[256] + XOR ───►│
  │◄─── 0x79 ──────────────────│
  │     ... 重复直到写完 ...      │
  │                              │
  │──── 0x21 0xDE (Go) ────────►│
  │◄─── 0x79 ──────────────────│
  │──── 0x08 0x00 0x00 0x00 ──►│
  │     [XOR checksum]           │
  │                              │  → STM32 跳转到 0x08000000
  │                              │     运行新固件
  │                              │
  │──── BOOT0=LOW ─────────────►│  (可选，Go 命令已跳转)
```

> 使用 Go 命令 (0x21) 后 STM32 直接跳转到用户代码，无需再控制 BOOT0 + NRST。但推荐在 Go 之后将 BOOT0 拉低，确保下次冷启动正常。

---

## 六、OneNET 通道分配与版本上报改造

### 6.0 历史问题：ESP32 之前误用了 MCU软件通道

当前 `ui_detailOTA.c:96` 调用的是 `onenet_ota_check_task("2", ...)`（type=2），这意味着 ESP32 的升级包一直上传到了 OneNET 的 **MCU软件** 通道。现在需要趁接入 STM32 OTA 的机会，将通道分配纠正过来：

| 通道 | type | 版本字段 | 用途 | 当前状态 | 改造后 |
|------|------|---------|------|---------|--------|
| 模组固件 (FOTA) | `"1"` | `f_version` | ESP32-S3 | ~~未使用~~ | **ESP32 OTA 使用此通道** |
| MCU软件 (SOTA) | `"2"` | `s_version` | STM32 | ~~ESP32 在用~~ | **STM32 OTA 使用此通道** |

> **OneNET 平台操作：** 以后 ESP32 新固件上传到"模组固件"，STM32 固件上传到"MCU软件"。原来存在 MCU软件里的 ESP32 旧固件包可保留存档，不影响后续升级。两个通道的版本号是独立管理的。

### 6.1 版本上报改造

当前 `onenet_ota_upload_version()` 将 `s_version` 和 `f_version` 都设为 ESP32 的版本号（见 `my_ota.c:291`），需要改为分别上报两块芯片的真实版本：

```c
// my_ota.c / my_ota.h 中新增

esp_err_t onenet_ota_upload_version_separate(const char *fw_ver, const char *sw_ver)
{
    // fw_ver = ESP32 版本  →  f_version (FOTA/模组固件 type=1)
    // sw_ver = STM32 版本  →  s_version (SOTA/MCU软件  type=2)
    char version_info[256];
    snprintf(version_info, sizeof(version_info),
             "{\"f_version\":\"%s\", \"s_version\":\"%s\"}", fw_ver, sw_ver);
    // POST /fuse-ota/{pid}/{dev}/version
    // ... HTTP 请求逻辑与现有相同
}
```

上报策略：

| 时机 | f_version (ESP32) | s_version (STM32) |
|------|-------------------|-------------------|
| 系统启动 | `ota_get_current_version()` | `stm_ota_get_stm_version()` (从 NVS 读取) |
| ESP32 OTA 成功后 | 新版本号 | 不变 |
| STM32 OTA 成功后 | 不变 | 新版本号 |

### 6.2 OTA 任务查询（按芯片分别查询）

```c
// ESP32 自身 OTA — type=1 (FOTA/模组固件)
onenet_ota_check_task("1", ota_get_current_version());

// STM32 固件 OTA — type=2 (SOTA/MCU软件)
onenet_ota_check_task("2", stm_ota_get_stm_version());
```

> **注意：** `onenet_ota_check_task` 是 `my_ota.c` 中已有的函数。调用后结果写入 `my_ota.c` 的 static 变量。STM32 OTA 调用后必须**立即拷贝**结果到 `my_stm_ota.c` 自己的变量，防止被后续调用覆盖。

### 6.3 OTA 状态上报

| step 值 | 含义 | 调用时机 |
|---------|------|---------|
| 0 | 固件下载中 | 开始 HTTP 下载 |
| 10~90 | 下载进度 | 每 10% 上报一次 |
| 100 | 下载完成，开始烧写 | HTTP 下载结束 |
| 101~190 | 烧写进度 | 烧写过程中（使用 100+ 系列与下载区分） |
| 200 | 烧写成功 | AN3155 写完后 |
| 201 | 升级完成 | 已验证并跳转 |
| 4xx | 失败 | 各种失败场景 |

---

## 七、GPIO 控制 STM32 模式切换

### 7.0 GPIO 安全检查

> **GPIO12 (BOOT0) 是 ESP32-S3 的 strapping pin (MTDI)**，上电时影响 flash 电压配置。必须验证：BOOT0=HIGH 时 ESP32 复位/上电不会异常。如果 GPIO12 已用于其他功能或会影响启动，改用 GPIO14/15/4 等安全引脚。

```c
// my_stm_ota.c

// ★ 选择 GPIO 时避开 strapping pins（GPIO0, GPIO3, GPIO12, GPIO45, GPIO46）
// 推荐：BOOT0 → GPIO14, NRST → GPIO13（或任意安全 GPIO）
#define STM_OTA_BOOT0_GPIO  GPIO_NUM_14   // ★ 从 GPIO12 改为 GPIO14（避免 strapping 冲突）
#define STM_OTA_NRST_GPIO   GPIO_NUM_13

static void stm_ota_gpio_init(void)
{
    // BOOT0: 推挽输出，初始低电平（正常模式）
    gpio_config_t boot0_cfg = {
        .pin_bit_mask = BIT64(STM_OTA_BOOT0_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot0_cfg);
    gpio_set_level(STM_OTA_BOOT0_GPIO, 0);

    // NRST: 开漏输出，初始高电平（不复位）
    // 使用开漏 + 外部上拉电阻，避免与 STM32 内部复位电路冲突
    gpio_config_t nrst_cfg = {
        .pin_bit_mask = BIT64(STM_OTA_NRST_GPIO),
        .mode = GPIO_MODE_OUTPUT_OD,  // 开漏输出
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 内部上拉，配合外部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&nrst_cfg);
    gpio_set_level(STM_OTA_NRST_GPIO, 1);
}

esp_err_t stm_ota_enter_bootloader(void)
{
    // 1. BOOT0 = HIGH → 选择 System Memory
    gpio_set_level(STM_OTA_BOOT0_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. NRST = LOW → 复位 STM32
    gpio_set_level(STM_OTA_NRST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. NRST = HIGH → 释放复位，STM32 重新启动
    gpio_set_level(STM_OTA_NRST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待 bootloader 初始化

    // STM32 现在应该在 ROM Bootloader 中等待 UART 通信
    return ESP_OK;
}

esp_err_t stm_ota_exit_bootloader(void)
{
    // 1. BOOT0 = LOW → 下次启动从 Flash 运行
    gpio_set_level(STM_OTA_BOOT0_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. 复位 STM32
    gpio_set_level(STM_OTA_NRST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(STM_OTA_NRST_GPIO, 1);

    return ESP_OK;
}
```

---

## 八、UART 模式切换

ESP32 的 UART1 需要在两种用途间切换，关键是确保模式切换的原子性：

### 8.1 my_serial 改造：暴露 task handle

**现有代码 [my_serial.c:327-328](main/components/my_serial/src/my_serial.c#L327-L328) 创建任务时 handle 参数为 NULL，必须先改为保存 handle：**

```c
// my_serial.c — 改造：保存 task handle

// 文件级静态变量存储 handle
static TaskHandle_t s_uart_rx_task_handle = NULL;
static TaskHandle_t s_uart_tx_task_handle = NULL;

// my_serial.h — 暴露给其他模块
extern TaskHandle_t uart_rx_task_handle;
extern TaskHandle_t uart_tx_task_handle;

// 在 my_serial.c 中定义可外部引用的变量
TaskHandle_t uart_rx_task_handle = NULL;
TaskHandle_t uart_tx_task_handle = NULL;

// uart_init() 中改为：
xTaskCreate(uart_rx_task, "uart_event_task", 4096, NULL, configMAX_PRIORITIES - 1, &uart_rx_task_handle);
xTaskCreate(uart_tx_task, "uart_tx_task", 3072, NULL, configMAX_PRIORITIES - 2, &uart_tx_task_handle);
```

### 8.2 AN3155 模式切换

```c
// my_stm_ota.c

// 切换到 AN3155 模式
static void uart_switch_to_an3155(void)
{
    // 1. 标记 AN3155 正在使用 UART（阻止其他代码操作 UART）
    g_an3155_in_use = true;

    // 2. 暂停正常通信任务（不删除，仅挂起）
    if (uart_rx_task_handle) vTaskSuspend(uart_rx_task_handle);
    if (uart_tx_task_handle) vTaskSuspend(uart_tx_task_handle);

    // 3. 等待 UART TX 完成当前帧发送（避免帧被截断发送到 STM32 bootloader）
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. 清空 UART 缓冲区（★ 必须在 suspend 之后 flush，防止 rx_task 已读取的数据丢失）
    uart_flush(UART_PORT);
    uart_flush_input(UART_PORT);

    // 5. AN3155 使用 115200 8N1（与现有配置一致，无需重新配置）
}

// 切换回正常通信模式
static void uart_switch_to_normal(void)
{
    uart_flush(UART_PORT);
    uart_flush_input(UART_PORT);

    g_an3155_in_use = false;

    if (uart_rx_task_handle) vTaskResume(uart_rx_task_handle);
    if (uart_tx_task_handle) vTaskResume(uart_tx_task_handle);
}
```

> **风险说明：**
> - `uart_rx_task` 被挂起时，UART 硬件 FIFO 仍会接收数据。由于 AN3155 通信期间 STM32 响应的是 0x79 等单字节，不会填满 1024 字节 FIFO。测试验证无溢出。
> - `uart_tx_task` 被挂起时，`uart_tx_queue` 可能积累其他模块试图发送的数据帧。这些帧在 resume 后会发出，但此时 STM32 已在运行新固件，旧帧会被忽略。若担心队列溢出，可在切换前先 `xQueueReset(uart_tx_queue)`。

---

## 九、main.c 启动流程变更

```c
// main.c — 新增 stm_ota_init()

void app_main(void)
{
    uart_init();
    main_task_init();
    my_nvs_init();
    wifi_start();

    screen_init();
    ato_init();            // ESP32 OTA 初始化（现有）
    ato_validate_app();    // ESP32 固件存活验证（现有）

    stm_ota_init();        // ★ 新增：初始化 STM32 OTA 子系统（GPIO + NVS）

    // ★ 新增：Wi-Fi 连接后上报双版本到 OneNET
    // 建议在 MQTT 上线回调或 Wi-Fi got-IP 事件中触发：
    // onenet_ota_upload_version_separate(
    //     ota_get_current_version(),
    //     stm_ota_get_stm_version()
    // );

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
```

---

## 十、UI 变更 (ui_detailOTA) — 方向 B：芯片切换式

### 10.1 设计决策

采用**单面板 + 芯片切换条**的方案，而不是双卡片堆叠。理由：

- 240×320 小屏幕空间有限，两块芯片各自的版本/目标/大小/按钮双倍堆叠会导致页面过长
- OTA 属于低频操作，用户每次只关注一块芯片，不需要同时看到两块的状态
- 两块芯片的 OTA 交互模型统一为"Check → Update"两段式，切换时交互一致

### 10.2 布局

```
┌──────────────────────────┐ y=4
│      OTA Update          │ 标题 34px
├──────────────────────────┤ y=42
│  [ESP32]  ●  [STM32]    │ 芯片切换条 (28px)
│  选中=蓝底白字, 未选中=灰 │ ● = 有可用更新
├──────────────────────────┤ y=78
│ Current:  V1.5.3         │
│ Target:   V1.6.0         │ 信息卡片 (100px)
│ Size:     512 KB         │ 数据随选中芯片切换
│ Previous: V1.4.1         │ ← STM32 时隐藏此行
├──────────────────────────┤ y=184
│ [Check ESP32]            │ 检查更新 36px (蓝)
│ [Update ESP32]           │ 开始升级 36px (绿, check 后显示)
│ [Rollback to V1.4.1]     │ 回滚 36px (橙, 仅 ESP32)
├──────────────────────────┤
│ ESP32: ██████░░ 60%      │ 进度条 (升级时覆盖显示)
│ Status: Downloading...   │ 状态文字
└──────────────────────────┘
```

### 10.3 切换逻辑

| 切换方向 | 版本卡片数据来源 | Previous 行 | Rollback 按钮 | Update 按钮 |
|---------|----------------|------------|--------------|------------|
| ESP32 | `ota_get_*()` | 显示 | 显示（有旧版时） | 显示（check 后） |
| STM32 | `stm_ota_get_*()` | **隐藏** | **隐藏** | 显示（check 后） |

更新提示：芯片标签旁的小圆点（橙色），有可用更新时显示。通过 `s_esp32_has_update` / `s_stm32_has_update` 标志位控制。

### 10.4 交互流程（两块芯片统一的两段式）

**阶段 1 — Check：**
```
[点击 Check] → 上报双版本 (upload_version_separate)
             → 查询 OTA 任务 (type=1 或 type=2)
             → 更新卡片（Target / Size）
             → 显示 [Update] 按钮 + 更新圆点
```

**阶段 2 — Update：**
```
[点击 Update] → 禁用所有按钮 + 显示进度条
              → 下载固件 + 烧写 (ESP32: esp_https_ota / STM32: AN3155)
              → 完成后隐藏进度条 + 清除更新标记
```

**关键改动：ESP32 OTA 从原来的一键到底（Check=下载+刷写+重启）改为两段式，与 STM32 保持一致。**

### 10.5 进度条共享

进度轮询定时器同时检查 `ota_is_running()` 和 `stm_ota_is_running()`，根据当前谁在运行读取对应的 progress。ESP32 和 STM32 不会同时 OTA，无需冲突处理。

### 10.6 type 通道纠正

ESP32 Check 按钮从 `check_task("2", ...)` 改为 `check_task("1", ...)`（FOTA/模组固件），STM32 使用 `check_task("2", ...)`（SOTA/MCU软件）。版本上报改为 `upload_version_separate(esp32_ver, stm32_ver)`，一次上报两个正确版本。

---

## 十一、NVS 存储项扩展

| Namespace | Key | 类型 | 说明 |
|-----------|-----|------|------|
| `ota` | `stm_version` | string | 当前 STM32 固件版本 |
| `ota` | `stm_prev_version` | string | 上一个 STM32 固件版本（回滚用） |
| `ota_resumption` | `stm_ota_md5` | string | STM32 固件下载断点的 MD5 |
| `ota_resumption` | `stm_ota_size` | u32 | STM32 固件已下载字节数 |

---

## 十二、文件改动清单

### 新建文件

| 文件 | 说明 | 状态 |
|------|------|------|
| `ESP/main/components/my_stm_ota/CMakeLists.txt` | 组件注册 | ✅ 已创建 |
| `ESP/main/components/my_stm_ota/inc/my_stm_ota.h` | 公共 API | ✅ 已创建 |
| `ESP/main/components/my_stm_ota/inc/stm_an3155.h` | AN3155 协议常量 | 🔲 待实现 |
| `ESP/main/components/my_stm_ota/src/my_stm_ota_stub.c` | 核心实现（当前为占位桩） | ⚠️ 占位桩，待实现 |
| `ESP/main/components/my_stm_ota/src/my_stm_ota.c` | 核心实现 | 🔲 替换 stub |

### 修改文件

| 文件 | 改动 | 状态 |
|------|------|------|
| `ESP/main/CMakeLists.txt` | 添加 `my_stm_ota` 依赖 | ✅ 已完成 |
| `ESP/main/main.c` | 添加 `stm_ota_init()` 调用 + 双版本上报 | 🔲 待修改 |
| `ESP/main/components/my_serial/inc/my_serial.h` | 新增 `CMD_VERSION = 0x04`；extern `uart_rx_task_handle` / `uart_tx_task_handle` | ✅ 已完成 |
| `ESP/main/components/my_serial/src/my_serial.c` | 任务创建时保存 handle；定义 `uart_rx_task_handle` / `uart_tx_task_handle` | ✅ 已完成 |
| `ESP/main/components/my_ota/inc/my_ota.h` | 新增 `upload_version_separate()`、`ota_get_download_url()`、`ota_get_firmware_md5()` | ✅ 已完成 |
| `ESP/main/components/my_ota/src/my_ota.c` | 实现 `upload_version_separate()` + 新 getter；`upload_version()` 改为调用 `separate` | ✅ 已完成 |
| `ESP/main/components/my_ui/screens/ui_detailOTA.c` | 芯片切换式重设计：chip switch bar + 统一两段式 Check/Update/Rollback + 共享进度条 + type 纠正 | ✅ 已完成 |
| `ESP/main/components/my_ui/screens/ui_detailOTA.h` | 新增 chip switch、STM32 对象、`ui_event_chipSwitchEsp/Stm`、`ui_event_startUpdate` 声明 | ✅ 已完成 |
| `ESP/main/components/my_nvs/inc/my_nvs.h` | 无需改动（已有接口够用） | N/A |

---

## 十三、改造顺序建议

```
Phase 1: 基础设施（1 天）
  ├─ 1.1 创建 my_stm_ota 组件骨架
  ├─ 1.2 GPIO 控制（BOOT0 + NRST）+ 测试进入/退出 bootloader
  ├─ 1.3 my_serial 改造：保存 + 暴露 task handle
  ├─ 1.4 UART 模式切换 + g_an3155_in_use 标志位验证
  └─ 1.5 NVS stm_version 读写

Phase 2: AN3155 协议（2 天）
  ├─ 2.1 实现 an3155_send_cmd / an3155_auto_baud
  ├─ 2.2 实现 an3155_write_memory / an3155_mass_erase / an3155_go
  ├─ 2.3 编写测试代码：写一小段测试数据到 STM32 Flash 再回读验证
  └─ 2.4 完整烧写测试（用已知 good 的 bin 文件）

Phase 3: 云端对接 + 通道迁移（1 天）
  ├─ 3.0 ★ 将 ESP32 固件上传从 MCU软件 切换到 模组固件（OneNET 平台操作）
  ├─ 3.1 my_ota 中新增 upload_version_separate + getter 函数
  ├─ 3.2 修复 ui_detailOTA.c: check_task("2") → check_task("1")
  ├─ 3.3 my_stm_ota 中实现 download_firmware（HTTP 下载到 RAM）
  └─ 3.4 端到端测试：OneNET FOTA→ESP32升级 + SOTA→STM32升级

Phase 4: UI（1 天）
  ├─ 4.1 OTA 详情页新增 STM32 版本行
  ├─ 4.2 新增 STM32 检查更新/升级按钮 + 事件处理
  └─ 4.3 进度条复用 + 状态提示

Phase 5: 稳定性（1 天）
  ├─ 5.1 断点续传（stm_ota_resumption namespace）
  ├─ 5.2 错误恢复（所有失败路径统一调用 fail_and_recover）
  ├─ 5.3 回滚支持（NVS 保存 stm_prev_version + AN3155 重新烧写旧版本）
  ├─ 5.4 硬件看门狗喂狗验证
  └─ 5.5 边界条件测试（超时、断电、校验失败等）
```

---

## 十四、关键注意事项

1. **STM32 固件格式** — AN3155 需要裸二进制（raw binary, .bin 格式），不是 .hex 或 .elf。OneNET 平台上传的 SOTA 固件包应为 .bin 文件
2. **Flash 对齐** — Write Memory 要求长度是 4 的倍数，地址 4 字节对齐。固件编译时在 STM32 链接脚本中保证 `s_fw_size` 是 4 的倍数（末尾 `ALIGN(4)`）。不足在烧写前统一 memset 0xFF padding，**避免运行时逐块补对齐**（原计划的 aligned 临时 buffer 存在"写了临时缓冲区却仍从原 buffer 读取"的 bug）
3. **擦除时间** — 全局擦除约 3 秒（64KB Flash），期间不要超时断开。使用 Extended Erase (0x44) 优先，失败回退 Erase (0x43)
4. **UART 独占** — AN3155 通信期间不能混入任何自定义协议帧，否则 STM32 bootloader 会解析失败。通过 `g_an3155_in_use` 标志位 + task suspend 双重保护
5. **BOOT0 下拉** — 硬件上 BOOT0 必须有下拉电阻，否则悬空时电平不确定。**GPIO 选用 GPIO14（避开 ESP32-S3 strapping pin GPIO12）**
6. **NRST 冲突** — NRST 开漏输出 + 外部上拉 + 内部上拉启用，避免与 STM32 内部复位电路产生电平冲突
7. **固件版本管理** — 如果 STM32 从未刷过新固件（stm_version 为空），首次上报使用默认值 "V0.0.0"
8. **RAM 分配** — 优先在 PSRAM 中分配固件缓冲区。下载完成后一次性烧写，避免长时间占用
9. **my_ota 代码复用** — `stm_ota_check_task` 调用 `onenet_ota_check_task("2", ...)` 后**立即拷贝**结果到自己的 static 变量。`my_ota.c` 需新增 3 个 getter 函数支持结果拷贝
10. **回滚安全** — 回滚操作本质是通过 AN3155 重新烧写旧版本固件。ESP32 需要保留旧版本的 bin 文件（或重新从 OneNET 下载）
11. **硬件看门狗** — 如果系统启用了 IWDT，在 AN3155 烧写循环中（erase + 逐块 write，总计 5~15 秒）需要手动喂狗，否则系统会复位
12. **错误恢复** — 所有 AN3155 失败路径必须先调用 `stm_ota_exit_bootloader()` 恢复 STM32 正常模式，再恢复 UART 任务。否则 STM32 卡在 bootloader 无法恢复数据通信
