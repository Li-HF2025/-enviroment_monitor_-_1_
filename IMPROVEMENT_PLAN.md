# 项目改进计划

> 基于 2026-06-04 全面代码审查生成，融合原有计划与深度分析结果。
> 总预计工时：**10~15 天**

---

## 总览：问题按严重度分级

| 级别 | 数量 | 说明 |
|------|------|------|
| 🔴 严重 | 2 | 通信环路、数据错误上报 |
| 🟠 重要 | 4 | 紧耦合、malloc 碎片、数据格式不一致 |
| 🟡 改善 | 8 | 轮询改通知、魔术数字、命名规范 |
| 🟢 增强 | 4 | 离线缓存、下行命令、调试宏 |

---

## 第一阶段：修复严重 Bug（预计 1 天）⚠️ 最高优先级

### 1.1 🔴 STM32：删除接收帧后的多余回传（通信环路）

**文件：** `STM/Core/Src/MySrc/mySerial.c` 第 255 行  
**影响：** `uart_rx_task` 收到 ESP32 下发的请求帧后，通过 `msg_Report()` 原样回传，造成**通信环路**——ESP32 发请求 → STM32 收到后立即回传 → ESP32 再收到。

```c
// ❌ 删除这一行（在 RX_READ_EOF2 状态中，osMessageQueuePut 之前）
msg_Report(item.cmd, item.payload, item.payload_len);
```

### 1.2 🔴 ESP32：增加数据有效性过滤（防止错误值上报）

**文件：** `ESP/main/components/my_screen/src/detail_temp_logic.c`  
**影响：** DHT22 读取失败时返回 `temp=-1000.0f, hum=-1.0f`，当前代码**无条件**将此错误值写入 MQTT 上报缓存并触发发布。

```c
// detail_temp_logic.c — temp_task 中，mqtt_report_set_* 之前新增：
if (temp_value <= -100.0f || humidity_value < 0.0f) {
    // 无效数据，不转发给 MQTT（可记录错误计数用于诊断）
    continue;
}
// detail_dB_logic.c — dB_task 中同样增加范围校验：
if (dB_value < 0.0f || dB_value > 180.0f) {
    continue;  // 超出物理量程，丢弃
}
```

### 1.3 🟡 STM32：`HAL_Delay` 改为 `osDelay`

**文件：** `STM/Core/Src/MySrc/db.c` 第 122 行

```c
// 改前
HAL_Delay(100);
// 改后
osDelay(100);
```

### 1.4 🟢 STM32：删除重复 include

**文件：** `STM/Core/Src/freertos.c` 第 32 行 — 删除重复的 `#include "OLED.h"`。

### 1.5 🟢 STM32：修正 DHT22 读取间隔注释

**文件：** `STM/Core/Src/MySrc/temperature.c` 第 253 行

```c
// 改前
osDelay(1000); // 间隔1分钟读取一次
// 改后
osDelay(2000); // 每2秒读取一次（DHT22最大采样率0.5Hz）
```

### 1.6 🟡 ESP32：修正文件名拼写 + CMake 引用

**文件：** `ESP/main/components/my_serial/src/mian_task.c`  
重命名为 `main_task.c`，同步更新 `CMakeLists.txt` 中的 `SRCS` 引用。

### 1.7 🟢 ESP32：触摸日志降级

**文件：** `ESP/main/components/my_screen/src/my_screen.c`

```c
// 改前
ESP_LOGI(TAG, "Touch at mapped(%d, %d) ...");
// 改后
ESP_LOGD(TAG, "Touch at mapped(%d, %d) ...");
```

---

## 第二阶段：架构解耦（预计 3~5 天）🏗️ 改动最大

### ⚠️ 为什么有 CMake 依赖循环（必须先理解）

当前各组件 `CMakeLists.txt` 的 `REQUIRES` 形成了循环：

```
┌─────────────────────────────────────────────────┐
│  问题依赖链（已修复的用 ✅ 标记）                 │
│                                                   │
│  my_wifi ──REQUIRES──→ my_mqtt     ← ✅ 已移除   │
│  my_wifi ──REQUIRES──→ my_ota      ← ✅ 已移除   │
│  my_wifi ──REQUIRES──→ my_screen   ← ✅ 已移除   │
│  my_wifi ──REQUIRES──→ my_nvs       (保留，实际需要) │
│                                                   │
│  my_mqtt ──REQUIRES──→ my_wifi                   │
│  my_screen ─REQUIRES──→ my_wifi                   │
│  my_ui ────REQUIRES──→ my_screen                 │
│                                                   │
│  ❌ 旧循环：my_wifi → my_mqtt → my_wifi          │
│  ❌ 旧循环：my_wifi → my_screen → my_wifi        │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  🔴 当前残留问题：wifi_scan.c                    │
│                                                   │
│  wifi_scan.c 是 my_wifi 组件的源文件，但代码里：  │
│    #include "lvgl.h"             ← LVGL 依赖     │
│    extern lv_obj_t *ui_WIFIChoice ← UI 对象依赖  │
│    lv_async_call(...)            ← LVGL API     │
│                                                   │
│  当 my_wifi 的 CMakeLists 不再 REQUIRES my_screen │
│  后，编译 wifi_scan.c 时找不到 lvgl.h → 报错！   │
│                                                   │
│  解决方案：回调模式解耦（见 2.1.2）               │
│  原则：my_wifi 只发数据，不碰任何 UI 代码         │
└─────────────────────────────────────────────────┘
```

### 组件依赖清单（现状）

| 组件 | REQUIRES | 是否存在问题 |
|------|---------|------------|
| `my_serial` | `freertos driver esp_driver_uart vfs log` | ✅ 无依赖 |
| `my_nvs` | `freertos nvs_flash ...` | ✅ 无依赖 |
| `my_wifi` | `freertos esp_wifi esp_event log nvs_flash lwip my_nvs` | ⚠️ wifi_scan.c 残留 lvgl |
| `my_mqtt` | `... mqtt my_wifi` | ✅ 正常（需要 WiFi 信息） |
| `my_ota` | `... my_nvs` (PRIV) | ✅ 无依赖 |
| `my_stm_ota` | `... my_nvs my_ota` | ✅ 正常 |
| `my_screen` | `... my_ui my_wifi my_serial my_mqtt` | ⚠️ 依赖较多，后续优化 |
| `my_ui` | `lvgl ...` | ✅ 正常 |

### 2.1 WiFi 组件事件化改造 ✅ 大部分已完成

| 步骤 | 说明 | 状态 |
|------|------|------|
| 移除 `my_wifi` CMake 对 `my_mqtt/my_ota/my_screen` 的依赖 | `my_wifi/CMakeLists.txt` | ✅ |
| 清理 `my_wifi.c` 中多余 include | 删除 `my_mqtt.h/my_ota.h/detail_time_logic.h` | ✅ |
| 用 `esp_event_post(MY_WIFI_EVENT_BASE, ...)` 替代硬编码调用 | `ip_event_handler` 等 | ✅ |
| 各业务组件注册事件订阅 | `my_mqtt/my_ota/detail_time_logic` | ✅ |

---

### 2.1.2 🔴 wifi_scan.c 解耦 LVGL（编译阻断项 — 待执行）

**根因：** `wifi_scan.c` 属于 `my_wifi` 组件，但代码里 `#include "lvgl.h"`。当 `my_wifi` 不再 `REQUIRES my_screen` 后，编译器找不到 `lvgl.h`。

**方案：** 回调模式 — `wifi_scan` 只扫描+回调，UI 层在回调中用 `lv_async_call` 更新界面。

```
解耦前：wifi_scan.c ──直接调用──▶ lvgl.h / ui_WIFIChoice / lv_async_call
解耦后：wifi_scan.c ──回调──▶ s_result_cb / s_done_cb
                            ▲
        ui_setting.c ──注册──┘  (回调中用 lv_async_call 更新 UI)
```

**三步改造：**

| 文件 | 做什么 |
|------|--------|
| `wifi_scan.h` | 新增回调类型 `wifi_scan_result_cb_t`/`wifi_scan_done_cb_t` + 注册函数 |
| `wifi_scan.c` | 删除 `#include "lvgl.h"` 和所有 LVGL/UI 调用，改为调用回调函数指针 |
| `ui_setting.c` | 实现两个回调 + 在 `screen_init` 末尾注册 |

**核心原则：`my_wifi` 组件的任何 `.c` 文件都不能 `#include` LVGL 或 UI 相关头文件。**

> 详细实现参见 `docs/ESP32_DEV_GUIDE.md` 中的回调模式章节。

---

### 2.2 传感器层与 MQTT 完全解耦 + 新增上报调度器

**当前问题：**
```
detail_dB_logic ──直接调用──→ mqtt_report_set_float("dB_value", ...)
                ──直接调用──→ mqtt_report_request_publish()
```
即**每个传感器数据点**（dB 每 150ms、温度每 2s）都触发一次 MQTT 发布，无聚合、无节流。

**目标架构：**
```
detail_dB_logic ──esp_event_post(SENSOR_DB_UPDATED)──→ data_report_dispatcher
                                                         │
                                    ┌────────────────────┤
                                    │ 聚合（5s 或 N条）   │
                                    │ 写入 mqtt_report    │
                                    │ 触发批量发布         │
                                    │ 离线时写本地缓存     │
                                    └────────────────────┘
```

**步骤：**

1. 新建 `data_report_dispatcher` 组件（路径：`ESP/main/components/data_report_dispatcher/`）

```c
// data_report_dispatcher.h
typedef enum {
    SENSOR_DB_UPDATED,
    SENSOR_TEMP_UPDATED,
    SENSOR_LIGHT_UPDATED,
} sensor_event_id_t;

ESP_EVENT_DECLARE_BASE(SENSOR_EVENT_BASE);

void data_report_dispatcher_init(void);
```

2. `data_report_dispatcher.c` 核心逻辑：

```c
// 每收到一个传感器事件 → 更新内部缓存 → 达到条件（5s间隔或积累N条）→ 写入 mqtt_report → 发布
static void sensor_event_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data) {
    // 根据 event_id 更新对应的缓存值
    // 检查是否需要立即发布
    uint32_t now = xTaskGetTickCount();
    if ((now - last_publish_tick) > pdMS_TO_TICKS(5000)) {
        mqtt_publish_all_report();
        last_publish_tick = now;
    }
}
```

3. `detail_dB_logic.c` / `detail_temp_logic.c` 改造：

```c
// ❌ 删掉这三行：
mqtt_report_set_float("dB_value", dB_value);
mqtt_report_set_bool("connect_status", true);
mqtt_report_request_publish();

// ✅ 改为发事件
esp_event_post(SENSOR_EVENT_BASE, SENSOR_DB_UPDATED, &dB_value, sizeof(float), 0);
```

---

### 2.3 消除 extern 跨组件直接访问

| 当前位置 | extern 变量 | 改进方案 |
|---------|-----------|---------|
| `mian_task.c` | `extern QueueHandle_t temp_queue` | 通过函数 `temp_logic_get_queue()` 获取 |
| `mian_task.c` | `extern QueueHandle_t dB_queue` | 通过函数 `dB_logic_get_queue()` 获取 |
| `mian_task.c` | `extern QueueHandle_t uart_tx_queue` | 已有 `msg_Request/Response/Report()`，直接使用 |
| `my_stm_ota` | `extern TaskHandle_t uart_rx_task_handle` | `my_serial` 提供 `uart_suspend()` / `uart_resume()` |
| `detail_*_logic` | `extern lv_obj_t *ui_xxx` | 通过 `ui_get_xxx_label()` 封装函数 |

---

## 第三阶段：数据处理规范化（预计 2~3 天）📐

### 3.1 统一传感器数据编码为结构化二进制

**当前问题：** 温度用 4 字节二进制（`int16_t temp×10, uint16_t hum×10`），分贝用 ASCII 字符串（如 `"45.3"`），光照未实现。同一协议中混用两种编码，ESP32 端需 `atof()` 解析分贝，增加不一致性。

**方案：** 所有传感器响应/上报统一为 6 字节固定结构：

```c
// my_serial.h 两端同步新增
typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;   // 0x01=温度, 0x02=分贝, 0x03=光照, 0x04=湿度
    uint8_t  status;        // bit0=数据有效, bit1=超量程, bit2=传感器故障
    int16_t  value_x10;     // 数值 × 10（有符号，支持负温度）
    uint16_t reserved;      // 预留扩展
} SensorDataBin;  // 固定 6 字节
```

**影响文件：**
| 端 | 文件 | 改动 |
|----|------|------|
| STM32 | `temperature.c` `DHT22_Send_Report()` | 改为填充 `SensorDataBin` 结构 |
| STM32 | `db.c` `StartDecibelTask()` | `snprintf` 改 `SensorDataBin` |
| ESP32 | `detail_temp_logic.c` | 解析二进制 → 提取 `value_x10 / 10.0f` |
| ESP32 | `detail_dB_logic.c` | 删除 `atof()` 调用 |

---

### 3.2 协议载荷子命令码替代字符串比较

**当前问题：** `strcmp(buf, "DB Init")` / `strcmp(buf, "DHT22 DeInit")` 效率低且易因空格/大小写出错。

```c
// my_serial.h 两端同步新增
typedef enum {
    SUB_CMD_INIT    = 0x01,  // 初始化/使能传感器
    SUB_CMD_DEINIT  = 0x02,  // 反初始化/停用传感器
    SUB_CMD_STATUS  = 0x03,  // 查询传感器状态
} ProtocolSubCmd;
```

STM32 端 `main_task.c` 改造：

```c
// 改前：
if (strcmp(buf, "DB Init") == 0) { DB_Init(); }

// 改后：
if (item.payload_len >= 1 && item.payload[0] == SUB_CMD_INIT) { DB_Init(); }
```

ESP32 端 `detail_dB_logic.c` 改造：

```c
// 改前：
msg_Request(CMD_DB, (const uint8_t *)"DB Init", 7);

// 改后：
uint8_t sub_cmd = SUB_CMD_INIT;
msg_Request(CMD_DB, &sub_cmd, 1);
```

---

### 3.3 统一校验和命名 + ESP32 端 main_task 避免 malloc

**3.3.1 命名统一：** 代码中 `CRC` / `checksum` / `crc` 混用，统一为 `checksum`（因为算法是累加校验和，不是 CRC）。

```c
// 两端同步
#define PROTOCOL_CHECKSUM_SIZE 2
// 状态机状态名：RX_READ_CRC_L → RX_READ_CHECKSUM_L
```

**3.3.2 ESP32 端 main_task 栈缓冲替代 malloc：**

```c
// mian_task.c 中：
// 改前：
char *payload = (char *)malloc(item.payload_len + 1);
// ... free(payload);

// 改后：
char payload[PROTOCOL_MAX_PAYLOAD + 1];  // 栈上分配，避免堆碎片
memcpy(payload, item.payload, item.payload_len);
payload[item.payload_len] = '\0';
```

---

## 第四阶段：STM32 端改进（预计 2~3 天）🔧

### 4.1 传感器按需启动（不自动使能）

**文件：** `STM/Core/Src/freertos.c` `MX_FREERTOS_Init()`

```c
// 改前：
DHT22_Init();
DB_Init();

// 改后：
// 只创建任务，不自动使能传感器。
// 由 ESP32 通过协议帧下发 Init 命令控制启停。
```

### 4.2 DHT22 任务：轮询改任务通知

**文件：** `STM/Core/Src/MySrc/temperature.c`

```c
void DHT22_Task(void *argument) {
    while (1) {
        // 阻塞等待使能通知（无需轮询 g_dht22_enabled）
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // 执行一次读取和上报
        float temperature, humidity;
        DHT22_Read(&temperature, &humidity);
        // ... 处理数据 ...
        DHT22_Send_Report(temperature, humidity);
    }
}

void DHT22_Init(void) {
    DHT22_Pin_Output();
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    g_dht22_enabled = true;
    xTaskNotifyGive(dht22TaskHandle);  // 唤醒任务
}

void DHT22_DeInit(void) {
    g_dht22_enabled = false;
    DHT22_Pin_Input();
}
```

### 4.3 魔术数字用宏替换

```c
// temperature.h 新增
#define DHT22_ERROR_VALUE_TEMP  (-1000.0f)
#define DHT22_ERROR_VALUE_HUMI  (-1.0f)
#define DHT22_VALID_TEMP_MIN    (-40.0f)
#define DHT22_VALID_TEMP_MAX    (80.0f)
#define DHT22_VALID_HUMI_MIN    (0.0f)
#define DHT22_VALID_HUMI_MAX    (100.0f)
```

### 4.4 统一 msg_* 序列号

**当前问题：** `msg_Report()` / `msg_Response()` / `msg_Request()` 各自维护独立的 `static seq_num`，序列号不同步。

**方案：** 提取为文件级全局序列号：

```c
// mySerial.c
static uint16_t g_seq_num = 0;

void msg_Report(...)   { /* 使用 g_seq_num++ */ }
void msg_Response(...) { /* 使用 g_seq_num++ */ }
void msg_Request(...)  { /* 使用 g_seq_num++ */ }
```

---

## 第五阶段：高级特性（预计 3~4 天）🚀

### 5.1 传感器数据本地缓存（离线补传）

**目标：** WiFi 断开期间不丢数据，恢复后自动补传。

**新建组件：** `sensor_cache`（路径：`ESP/main/components/sensor_cache/`）

```c
// sensor_cache.h
typedef struct {
    uint8_t  sensor_type;    // SENSOR_TYPE_TEMP / DB / LIGHT
    int16_t  value_x10;
    uint32_t timestamp;      // Unix 时间戳
} SensorCacheEntry;

void sensor_cache_init(void);
void sensor_cache_push(uint8_t type, int16_t value_x10);
int  sensor_cache_pending_count(void);           // 待补传条数
int  sensor_cache_flush(mqtt_publish_fn_t pub);  // 批量补传，返回成功条数
```

**存储方案：** NVS blob 或 SPIFFS 小文件，环形覆盖，最多保留 1000 条（~6KB）。

**集成点：** `data_report_dispatcher` 在 WiFi 断开时自动写入缓存，WiFi 恢复时自动补传。

---

### 5.2 MQTT 下行命令完整处理链路

**目标：** 云端 → ESP32 MQTT → 命令分发 → STM32 执行 → 响应回传。

**涉及文件：**

| 文件 | 改动 |
|------|------|
| `my_mqtt.c` `MQTT_EVENT_DATA` 分支 | 解析 OneNET 下行 JSON → 提取 cmd + params |
| `cmd_dispatcher.c`（新建） | 根据 cmd 分发：传感器启停 / OTA 触发 / 参数配置 |
| `my_serial.c` | 通过 `msg_Request()` 转发给 STM32 |
| STM32 `main_task.c` | 已有 CMD_DB/CMD_TEMPERATURE 分支，直接复用 |

**下行命令格式（OneNET 物模型）：**
```json
{
  "id": "xxx",
  "params": {
    "dB_enable": { "value": true },
    "temp_interval": { "value": 5 }
  }
}
```

---

### 5.3 STM32 调试输出用宏包裹

```c
// OLED.h 新增
#ifdef DBG_OLED_ENABLE
  #define DBG_OLED(line, col, str)  OLED_WriteString(line, col, str)
#else
  #define DBG_OLED(line, col, str)  ((void)0)
#endif
```

在 `sdkconfig` 或编译选项中用 `-DDBG_OLED_ENABLE` 控制开关。

---

### 5.4 新增 `light_logic` 光照传感器支持

`detail_light_logic.c` 已有骨架代码，但 STM32 端 `CMD_LIGHT` 未实现。  
在 STM32 端增加光照传感器驱动（如 BH1750 I2C）并通过 `CMD_LIGHT` 上报即可打通。

---

## 实施路线图

```
第 1 天 ─ 第一阶段：严重 Bug 修复
  ├─ 1.1 删除通信环路 msg_Report          ← 🔴 立即修复
  ├─ 1.2 增加数据有效性过滤                ← 🔴 防止脏数据上云
  ├─ 1.3 HAL_Delay → osDelay
  ├─ 1.4 删除重复 include
  ├─ 1.5 修正注释
  ├─ 1.6 文件名拼写修正
  └─ 1.7 日志降级

第 2~4 天 ─ 第二阶段：架构解耦（改动最大）
  ├─ 2.1 WiFi 事件化解耦
  ├─ 2.2 传感器-MQTT 解耦 + data_report_dispatcher
  └─ 2.3 extern 消除 + 接口封装

第 5~6 天 ─ 第三阶段：数据规范化
  ├─ 3.1 统一二进制传感器数据格式（需联调！）
  ├─ 3.2 子命令码替代字符串
  └─ 3.3 命名统一 + malloc 消除

第 7~8 天 ─ 第四阶段：STM32 改进
  ├─ 4.1 传感器按需启动
  ├─ 4.2 DHT22 任务通知
  ├─ 4.3 魔术数字宏化
  └─ 4.4 统一序列号

第 9~12 天 ─ 第五阶段：高级特性
  ├─ 5.1 sensor_cache 离线缓存
  ├─ 5.2 MQTT 下行命令
  ├─ 5.3 调试宏
  └─ 5.4 光照传感器补全
```

---

## 风险提示

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| 第三阶段协议格式变更后 STM32/ESP32 两端不兼容 | 🔴 高 | 修改后立即**联调测试**，保留旧版本固件用于回滚 |
| 第二阶段改动涉及 6+ 组件，可能引入新 bug | 🟠 中 | 每改完一个子阶段就编译 + 基础功能验证 |
| 新建组件（dispatcher/cache）与现有逻辑冲突 | 🟡 低 | 新组件独立编译，通过事件接口集成，不影响现有路径 |
| NVS 频繁写入导致 Flash 寿命问题 | 🟡 低 | sensor_cache 使用内存环形缓冲 + 定时批量刷写 |

---

## 分支策略建议

```bash
# 每个阶段独立分支
git checkout -b fix/phase1-critical-bugs      # 第一阶段
git checkout -b refactor/phase2-decouple       # 第二阶段
git checkout -b refactor/phase3-data-standard  # 第三阶段
git checkout -b improve/phase4-stm32           # 第四阶段
git checkout -b feature/phase5-advanced        # 第五阶段

# 每阶段完成后合并到主分支并打 tag
git tag v1.1-phase1
git tag v1.2-phase2
# ...
```
