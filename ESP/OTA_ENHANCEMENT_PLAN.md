# OTA 功能增强计划表

对比官方 `advanced_https_ota` 示例，你的 OTA 模块缺少以下功能。按优先级从高到低排列。

> **注意**：本文档基于新版 `my_nvs` API（支持多 namespace 统一管理）编写。不再手动调用 `nvs_open`/`nvs_close`，改为 `my_nvs_open_ns` / `my_nvs_get_handle` / `my_nvs_close_ns`。

> **已完成的改动**：
> - `my_nvs` 多 namespace 统一管理 ✓
> - `ato_init()` 打开 `"ota"` 和 `"ota_resumption"` 两个 namespace ✓
> - `ota_task()` 中 `esp_wifi_set_ps(WIFI_PS_NONE)` 关闭省电 ✓
> - `ota_task()` 中 `buffer_size` 从 1024 提升到 4096 ✓
> - 日志仅在进度百分比变化时打印 ✓
> - `partial_http_download = false`（OneNET 不支持 Range 请求）✓
>
> **待修复（严重 bug）**：断点续传用 URL 做匹配，但 OneNET 每次 check_task 分配新 task_id → URL 变化 → 断点匹配永远失败。需改为用 MD5 匹配。

---

## 优先级 1：OTA 断点续传 (Resume)

**价值**: 设备在远程网络不稳定环境运行时，下载到 90% 断网后无需从头开始。

**原理**:
- 用独立 namespace `"ota_resumption"` 保存 `(固件MD5, 已写入字节数)` 
- **关键**：不能用 URL 做匹配！OneNET 每次 `check_task` 都分配新的 `task_id`，导致下载 URL 变化（`.../1414771/download` → `.../1414784/download`）。URL 一变，断点匹配失败就从 0 开始
- 同一次升级任务的固件 `md5` 是固定的，用它做匹配标识才是正确的
- 每次 `esp_https_ota_perform()` 循环中定期保存已写入长度
- 下次启动 OTA 时查表 —— MD5 匹配则设置 `.ota_image_bytes_written`，底层自动从断点继续
- OTA 成功后清空整个 `"ota_resumption"` namespace

### 1.1 新增全局变量 `firmware_md5`

在 `my_ota.c` 顶部全局变量区域（`firmware_size` 附近）新增：

```c
static char firmware_md5[33];  // 本次OTA固件的MD5，check_task时赋值
```

### 1.2 修改 `onenet_ota_check_task()` —— 提取 md5 字段

OneNET check_task 返回的 JSON 包含 `md5` 字段：
```json
{"data":{"target":"1.4.2","tid":1414784,"size":1462352,"md5":"28786b6923645a59f8683839c36a33eb",...}}
```

需要在解析 `size_js` 的同级位置提取 `md5`，存入全局变量 `firmware_md5`：

```c
// 在 check_task 函数的 cJSON 解析部分，找到 size_js 那几行，后面插入：
cJSON* md5_js = cJSON_GetObjectItem(data_js, "md5");
if (md5_js) {
    snprintf(firmware_md5, sizeof(firmware_md5), "%s",
             cJSON_GetStringValue(md5_js));
}
```

**位置**：约第 355 行 `if (size_js) { firmware_size = ... }` 之后。

### 1.3 修改 `ato_init()` —— 打开两个 namespace

```c
// my_ota.c — ato_init() 修改后
void ato_init(void)
{
    ESP_LOGI(TAG, "ATO模块初始化开始");
    ESP_ERROR_CHECK(esp_event_handler_register(
        ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    my_nvs_open_ns("ota");              // OTA 通用状态（已存在，不变）
    my_nvs_open_ns("ota_resumption");   // 新增：断点续传专用
}
```

### 1.4 重写三个断点续传辅助函数（用 MD5 代替 URL）

```c
// ===== 辅助函数 1：读取断点 =====
// 返回上次已写入字节数。MD5 不匹配返回 0（从头下载）
static uint32_t ota_resume_get_written_len(const char *md5)
{
    if (md5 == NULL || md5[0] == '\0') return 0;

    nvs_handle_t h = my_nvs_get_handle("ota_resumption");
    if (h == 0) return 0;

    // 读取并对比 MD5，不匹配则断点无效（说明是另一次升级任务）
    size_t len = 33;
    char saved_md5[33] = {0};
    if (nvs_get_str(h, "ota_md5", saved_md5, &len) != ESP_OK) {
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

// ===== 辅助函数 2：保存断点 =====
static void ota_resume_save_progress(const char *md5, uint32_t wr_len)
{
    if (md5 == NULL) return;

    nvs_handle_t h = my_nvs_get_handle("ota_resumption");
    if (h == 0) return;

    nvs_set_str(h, "ota_md5", md5);
    nvs_set_u32(h, "ota_wr_len", wr_len);
    nvs_commit(h);  // 立即落盘，防止断电丢失
}

// ===== 辅助函数 3：清除断点（OTA 成功后调用）=====
static void ota_resume_cleanup(void)
{
    my_nvs_erase_all_ns("ota_resumption");  // 清空整个 namespace
}
```

### 1.5 重写 `ota_task()` —— 加入断点续传逻辑

修改后完整流程：

```
ato_start()
  └─ xTaskCreate(ota_task)
        │
        ├─ 1. 用 my_nvs_get_handle("ota_resumption") 获取断点 handle（已由 ato_init 打开）
        │
        ├─ 2. ota_resume_get_written_len(firmware_md5)
        │      └─ MD5 匹配 → 拿到上次已写入字节数
        │      └─ MD5 不匹配 / 首次 → 返回 0，从头下载
        │
        ├─ 3. esp_https_ota_begin()  ← 关键配置：
        │      .ota_resumption = true
        │      .ota_image_bytes_written = wr_len  （断点偏移量，首次为 0）
        │      .partial_http_download = true       （配合优先级 4）
        │      .max_http_request_size = 256 * 1024
        │
        ├─ 4. while (1) { esp_https_ota_perform() }
        │      │
        │      ├─ 下载中 → 周期性保存断点
        │      │     ota_resume_save_progress(firmware_md5, len)
        │      │     更新 s_ota_progress（UI 用）
        │      │     更新上报 step（每 10% 上报一次）
        │      │
        │      └─ 不在下载中 → break
        │
        ├─ 5. 数据完整性检查
        │
        ├─ 6. esp_https_ota_finish()
        │      └─ 成功:
        │           ota_resume_cleanup()        ← 清掉断点数据
        │           onenet_ota_upload_status(100)
        │           onenet_ota_upload_status(201)
        │           esp_restart()
        │      └─ 失败:
        │           保留断点数据（不调 cleanup）  ← 关键：下次重试用
        │           goto ota_end
        │
        └─ ota_end:
             ota_running = false
             esp_https_ota_abort()
             vTaskDelete(NULL)
```

关键代码段：

```c
static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "OTA任务开始, URL: %s", ota_download_url);
    esp_err_t err;
    int last_reported_step = 0;
    uint32_t last_saved_wr_len = 0;

    // ===== 步骤 1-2：查询断点（用 MD5 匹配，避免 URL 中 task_id 变化导致匹配失败）=====
    uint32_t ota_wr_len = ota_resume_get_written_len(firmware_md5);
    if (ota_wr_len > 0) {
        ESP_LOGI(TAG, "从断点继续: %"PRIu32" 字节", ota_wr_len);
    }
    // 从断点进度恢复 UI 显示
    if (firmware_size > 0) {
        s_ota_progress = (ota_wr_len * 100) / firmware_size;
        last_reported_step = s_ota_progress;
    }

    // ===== 步骤 3：配置并开始 OTA =====
    esp_http_client_config_t config = {
        .url = ota_download_url,
        .timeout_ms = 5000,
        .keep_alive_enable = true,
        .buffer_size = 1024,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = ota_http_client_init_cb,
        .ota_resumption = true,                     // 启用断点续传
        .ota_image_bytes_written = ota_wr_len,       // 断点偏移，首次为 0
    };
    esp_https_ota_handle_t https_ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed");
        onenet_ota_upload_status(task_id, 104);
        ota_running = false;
        vTaskDelete(NULL);
    }

    onenet_ota_upload_status(task_id, 0);

    // 验证固件头（同原逻辑，省略...）
    // ...

    // ===== 步骤 4：循环下载 + 定期保存断点 =====
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        if (firmware_size > 0) {
            size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
            int step = (len * 100) / firmware_size;
            s_ota_progress = step;

            // 每变化 10% 上报平台
            if (step - last_reported_step >= 10) {
                onenet_ota_upload_status(task_id, step);
                last_reported_step = step;
            }

            // 每写入 64KB 保存一次断点（避免过于频繁写 NVS）
            if (len - last_saved_wr_len >= 64 * 1024) {
                ota_resume_save_progress(firmware_md5, len);
                last_saved_wr_len = len;
            }

            ESP_LOGI(TAG, "Download: %d%% (%d/%d)", step, len, firmware_size);
        }
    }

    // ===== 步骤 5-6：完成处理 =====
    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "数据接收不完整，断点已保存，下次重试");
        onenet_ota_upload_status(task_id, 106);
        // 不调 cleanup，保留断点
        goto ota_end;
    }

    onenet_ota_upload_status(task_id, 100);

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err == ESP_OK) {
        ota_resume_cleanup();  // ← 成功后清断点
        ESP_LOGI(TAG, "OTA完成，断点已清理");
        onenet_ota_upload_status(task_id, 201);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        // 失败保留断点，下次重试
        ESP_LOGE(TAG, "OTA finish failed 0x%x, 断点已保留", ota_finish_err);
        if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
            onenet_ota_upload_status(task_id, 205);
        }
        goto ota_end;
    }

ota_end:
    ota_running = false;
    esp_https_ota_abort(https_ota_handle);
    vTaskDelete(NULL);
}
```

### 1.6 清理旧代码

删除 `my_ota.c` 中以下不再需要的行：
- 第 48-50 行：`NVS_NAMESPACE_OTA_RESUMPTION` / `NVS_KEY_OTA_WR_LENGTH` / `NVS_KEY_SAVED_URL` 三个宏（key 名改为直接写在辅助函数里，或保留宏但指向 "ota_resumption"）
- 第 62 行：空的 `ota_resume_get_written_len()` 函数体

---

## 优先级 2：固件失败自动回滚 (App Rollback)

**价值**: 新固件有 bug 时，bootloader 自动切回旧固件，防止设备变砖。

**原理**:
- Bootloader 烧写新固件后标记为 `ESP_OTA_IMG_PENDING_VERIFY`
- 新固件启动后，在"检查点"主动调用 `esp_ota_mark_app_valid_cancel_rollback()` 确认正常
- 若在超时前崩溃，bootloader 自动回滚

### 2.1 启用 bootloader 回滚

在 `sdkconfig.defaults` 中添加：
```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=n
```

### 2.2 在主程序中添加检查点

在 `app_main()` 中，WiFi 连接成功 + MQTT 连接成功 + 传感器初始化完成后调用：

```c
// main.c — app_main() 中，所有初始化完成后
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI("MAIN", "固件验证通过，已取消回滚");
        }
    }
#endif
```

> **检查点位置建议**：放在所有关键功能初始化完成后。不要放在 WiFi 连接之前，因为 WiFi 连不上说明固件可能有问题。

---

## 优先级 3：WiFi 省电优化

**价值**: 关闭 WiFi 省电模式可显著提升 OTA 下载速度。

### 3.1 在 `ota_task()` 中动态切换

```c
// ota_task() 开始处
esp_wifi_set_ps(WIFI_PS_NONE);   // 关闭省电，全速下载

// ... OTA 流程 ...

// ota_end 标签前 / cleanup 中恢复
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // 恢复普通省电模式
```

或者更简单的方式：在 `event_handler()` 的 `IP_EVENT_STA_GOT_IP` 分支中加一行 `esp_wifi_set_ps(WIFI_PS_NONE);`，但 OTA 期间切换的方案更精细。

---

## 优先级 4：部分 HTTP 下载 (Partial HTTP Download)

**价值**: 配合断点续传，允许分块请求大固件。网络中断后只需重传当前块。

### 4.1 修改 `esp_https_ota_config_t`

在 `ota_task()` 的 OTA 配置中增加 3 个字段：

```c
esp_https_ota_config_t ota_config = {
    .http_config = &config,
    .http_client_init_cb = ota_http_client_init_cb,
    .ota_resumption = true,
    .ota_image_bytes_written = ota_wr_len,
    // === 新增 ===
    .partial_http_download = true,
    .max_http_request_size = 256 * 1024,   // 每次请求 256KB
};
```

**`max_http_request_size` 选择**：
- 网络稳定 → 256K~512K，减少请求次数
- 网络不稳定 → 64K~128K，单次失败代价小

---

## 优先级 5：CA 证书验证（HTTP → HTTPS）

**价值**: 安全加固，防止固件被中间人篡改。当前使用明文 HTTP。

**前提**: 需先确认 OneNET fuse-ota 下载 URL 是否支持 HTTPS。

步骤概要：
1. 获取 OneNET 服务器 CA 证书（`.pem` 格式）放入 `my_ota/` 目录
2. `CMakeLists.txt` 中用 `target_add_binary_data` 嵌入证书
3. `my_ota.c` 中通过 `extern const uint8_t` 引用证书二进制
4. HTTP 配置中设置 `.cert_pem` 并关闭 `.skip_cert_common_name_check`

---

## 优先级 6：Kconfig 可配置选项

**价值**: 通过 `idf.py menuconfig` 可视化配置 OTA 行为，方便调试。

在 `my_ota/Kconfig` 中添加配置项（该文件已存在，追加内容）：
- `CONFIG_OTA_RESUME_ENABLE` (bool, 默认 y) — 是否启用断点续传
- `CONFIG_OTA_PARTIAL_DOWNLOAD` (bool, 默认 n) — 是否启用部分下载
- `CONFIG_OTA_TIMEOUT_MS` (int, 默认 5000) — OTA HTTP 超时
- `CONFIG_OTA_MAX_HTTP_SIZE` (int, 默认 262144) — 单次请求最大字节数

---

## 实施总结

| 步骤 | 功能 | 改动量 | 涉及文件 | 依赖 |
|------|------|--------|----------|------|
| 1 | 断点续传 | ~80 行（新增辅助函数 + 改 ota_task） | `my_ota.c` | 新版 my_nvs |
| 2 | 失败回滚 | 2 行配置 + 6 行代码 | `sdkconfig.defaults`, `main.c` | 无 |
| 3 | WiFi 省电优化 | 2 行代码 | `my_ota.c` | 无 |
| 4 | 部分 HTTP 下载 | 3 行配置 | `my_ota.c` | 步骤 1 完成 |
| 5 | CA 证书 | ~10 行 + 证书文件 | `my_ota.c`, `CMakeLists.txt` | 需确认 OneNET 支持 |
| 6 | Kconfig | 追加若干配置项 | `my_ota/Kconfig` | 无 |

**建议一次性完成步骤 1+2+3+4**，它们改的都是同一个文件 (`my_ota.c`) 的同一个函数 (`ota_task`)，合并做一次改动最高效。步骤 5 需要确认 OneNET HTTPS 支持后再做。步骤 6 可后续逐步添加。
