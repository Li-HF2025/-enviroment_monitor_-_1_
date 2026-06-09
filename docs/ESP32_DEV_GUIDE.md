# ESP32 端数据处理升级 — 开发者指南

> 适用平台：ESP32-S3 | 框架：ESP-IDF v5.x + CMake
> 前置条件：已完成 IMPROVEMENT_PLAN.md 中第一阶段 Bug 修复 + STM32 端升级

---

## 一、升级目标概览

```
升级前：STM32 数据透传 → 直接显示最新值 → 逐点上报 MQTT
           ↓
升级后：STM32 数据 → 滑动窗口统计 → LVGL 实时显示(瞬时/平均/最大/最小)
                  → 定时聚合上报 → OneNET MQTT(每N秒一条聚合数据)
```

**关键参数：**

| 参数 | 分贝 | 温度 | 湿度 |
|------|------|------|------|
| 采样间隔 | 150ms (STM32) | 2s (STM32) | 2s (STM32) |
| 滑动窗口 | **200 点 (30 秒)** | 10 点 (20 秒) | 10 点 (20 秒) |
| OneNET 上报 | 每 10 秒 | 每 20 秒 | 每 20 秒 |
| UI 刷新 | 每收到新点即刷新 | 每收到新点即刷新 | 每收到新点即刷新 |

---

## 二、涉及文件清单

```
ESP/main/components/my_serial/inc/my_serial.h        ← 新增 SensorDataBin 解析结构
ESP/main/components/my_serial/src/mian_task.c         ← 解析 SensorDataBin 并分发
ESP/main/components/my_screen/src/detail_dB_logic.c   ← 🔥 核心改造：滑动窗口+统计
ESP/main/components/my_screen/inc/detail_dB_logic.h   ← 新增 getter 函数
ESP/main/components/my_screen/src/detail_temp_logic.c ← 🔥 滑动窗口+统计
ESP/main/components/my_screen/inc/detail_temp_logic.h ← 新增 getter 函数
ESP/main/components/my_mqtt/inc/mqtt_report.h         ← 新增字段声明
ESP/main/components/my_mqtt/src/mqtt_report.c         ← 注册新 key
```

---

## 三、逐步实施

### 第 1 步：my_serial.h — 同步 STM32 端数据结构

**文件：** `ESP/main/components/my_serial/inc/my_serial.h`

在 `CMDType` 枚举后面追加（**与 STM32 端完全一致**）：

```c
// ===== 新增：传感器统一数据格式 =====
typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;   // 0x01=温度, 0x02=分贝, 0x03=光照, 0x04=湿度
    uint8_t  status;        // bit0=数据有效, bit1=超量程, bit2=传感器故障
    int16_t  value_x10;     // 数值 × 10（有符号）
    uint16_t reserved;      // 预留
} SensorDataBin;

// ===== 新增：协议子命令码 =====
typedef enum {
    SUB_CMD_INIT    = 0x01,
    SUB_CMD_DEINIT  = 0x02,
    SUB_CMD_STATUS  = 0x03,
} ProtocolSubCmd;
```

---

### 第 2 步：mian_task.c — 解析 SensorDataBin + 分发到对应队列

**文件：** `ESP/main/components/my_serial/src/mian_task.c`

#### 2.1 新增 SensorDataBin 解析逻辑

在 `mian_task` 函数中，`item.msg_type` 分支内，新增对 `CMD_TEMPERATURE` / `CMD_DB` 的 `SensorDataBin` 解析：

```c
// ========== 新增：传感器二进制数据解析 ==========
// 放在 item.msg_type == MSG_TYPE_RESPONSE 分支中

// 判断是否为传感器响应（payload_len >= 6 且是已知 CMD）
if (item.payload_len >= sizeof(SensorDataBin)) {
    SensorDataBin sensor;
    memcpy(&sensor, item.payload, sizeof(SensorDataBin));

    // 有效性校验
    if (sensor.status & 0x04) {
        ESP_LOGW(TAG, "传感器故障: type=0x%02X", sensor.sensor_type);
        free(payload);
        continue;
    }

    float value = (float)sensor.value_x10 / 10.0f;

    switch (sensor.sensor_type) {
        case 0x01: // 温度
            if (temp_queue != NULL) {
                // 用负数标记为温度（与湿度区分），复用 float 队列
                // 方案：发送两个 float：温度值、湿度值
                // 或者：使用结构体队列
            }
            break;
        case 0x02: // 分贝
            if (dB_queue != NULL) {
                if (value >= 0.0f && value <= 180.0f) {
                    xQueueSend(dB_queue, &value, pdMS_TO_TICKS(10));
                }
            }
            break;
        case 0x04: // 湿度
            if (temp_queue != NULL) {
                // 发送湿度值（用负值标记，或改为结构体队列）
            }
            break;
    }
}
```

#### 2.2 改进 temp_queue 数据结构（推荐）

当前 `temp_queue` 存的是 `UartTxItem`（~140字节），建议改为专用结构体：

```c
// 新建或放在 detail_temp_logic.h 中
typedef struct {
    float temperature;   // 温度值（°C），无效时为 DHT22_ERROR_VALUE_TEMP
    float humidity;      // 湿度值（%），无效时为 DHT22_ERROR_VALUE_HUMI
    bool  valid;         // 数据是否有效
} TempHumData;
```

然后在 `detail_temp_logic.c` 中将 `temp_queue` 改为 `sizeof(TempHumData)`，mian_task 中按此结构体填充。

---

### 第 3 步：detail_dB_logic.c — 🔥 核心：滑动窗口统计

**文件：** `ESP/main/components/my_screen/src/detail_dB_logic.c`

这是本次升级**最重要的改动**，完整重写该文件的数据处理部分。

#### 3.1 滑动窗口数据结构和配置

```c
#include "detail_dB_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_serial.h"
#include "mqtt_report.h"
#include "my_mqtt.h"
#include "esp_timer.h"

static const char *TAG = "dB_LOGIC";

// ===== 滑动窗口配置 =====
#define DB_WINDOW_SIZE   200       // 200 个点（@150ms = 30 秒窗口）
#define DB_REPORT_MS     10000     // 每 10 秒向 OneNET 上报一条
#define DB_VALID_MIN     0.0f      // 物理量程下限
#define DB_VALID_MAX     180.0f    // 物理量程上限

// ===== 滑动窗口数据 =====
static float  db_window[DB_WINDOW_SIZE];
static int    db_window_idx  = 0;
static int    db_window_fill = 0;

// ===== 统计结果 =====
static float  db_instant  = 0.0f;
static float  db_avg      = 0.0f;
static float  db_min      = 999.0f;
static float  db_max      = 0.0f;
static bool   db_has_data = false;

// ===== 上报节流 =====
static uint32_t last_report_ms = 0;

QueueHandle_t dB_queue;

// ===== LVGL UI 对象（SquareLine Studio 已生成） =====
extern lv_obj_t * ui_dBNum;      // 主界面分贝数字
extern lv_obj_t * ui_dBValue;    // 详情页瞬时值
extern lv_obj_t * ui_dBMaxVal;   // 详情页最大值
extern lv_obj_t * ui_dBMinVal;   // 详情页最小值
extern lv_obj_t * ui_dBAvgVal;   // 详情页平均值
```

#### 3.2 滑动窗口核心函数

```c
/**
 * @brief 将新数据点推入滑动窗口，实时计算 avg/min/max
 * @param value 新的分贝值（已校验有效性）
 */
static void db_window_push(float value) {
    db_window[db_window_idx] = value;
    db_window_idx = (db_window_idx + 1) % DB_WINDOW_SIZE;
    if (db_window_fill < DB_WINDOW_SIZE) db_window_fill++;

    // 实时统计（O(n)，200 个 float 很快）
    float sum = 0.0f;
    db_min = 999.0f;
    db_max = 0.0f;
    for (int i = 0; i < db_window_fill; i++) {
        float v = db_window[i];
        sum += v;
        if (v < db_min) db_min = v;
        if (v > db_max) db_max = v;
    }
    db_avg      = sum / (float)db_window_fill;
    db_instant  = value;
    db_has_data = true;
}

/**
 * @brief 更新 LVGL UI（需在 LVGL 锁内调用）
 */
static void db_update_lvgl(void) {
    if (!db_has_data) return;

    char buf[16];

    // 主界面
    if (ui_dBNum != NULL) {
        snprintf(buf, sizeof(buf), "%.1f dB", db_instant);
        lv_label_set_text(ui_dBNum, buf);
    }

    // 详情页 — 瞬时值
    if (ui_dBValue != NULL) {
        snprintf(buf, sizeof(buf), "%.1f", db_instant);
        lv_label_set_text(ui_dBValue, buf);
    }

    // 详情页 — 平均值
    if (ui_dBAvgVal != NULL) {
        snprintf(buf, sizeof(buf), "%.1f", db_avg);
        lv_label_set_text(ui_dBAvgVal, buf);
    }

    // 详情页 — 最大值
    if (ui_dBMaxVal != NULL && db_window_fill > 0) {
        snprintf(buf, sizeof(buf), "%.1f", db_max);
        lv_label_set_text(ui_dBMaxVal, buf);
    }

    // 详情页 — 最小值
    if (ui_dBMinVal != NULL && db_window_fill > 0) {
        snprintf(buf, sizeof(buf), "%.1f", db_min);
        lv_label_set_text(ui_dBMinVal, buf);
    }
}

/**
 * @brief 写入 MQTT 上报缓存（由 data_report_dispatcher 定时触发）
 */
static void db_update_mqtt_report(void) {
    if (!db_has_data) return;

    mqtt_report_set_float("dB_instant", db_instant);
    mqtt_report_set_float("dB_avg",     db_avg);
    mqtt_report_set_float("dB_min",     db_min);
    mqtt_report_set_float("dB_max",     db_max);
    mqtt_report_set_bool("connect_status", true);
    mqtt_report_request_publish();
}
```

#### 3.3 主任务改造

```c
static void dB_task(void *arg) {
    while (1) {
        float dB_value;
        if (xQueueReceive(dB_queue, &dB_value, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // ✅ 有效性校验
        if (dB_value < DB_VALID_MIN || dB_value > DB_VALID_MAX) {
            ESP_LOGW(TAG, "无效分贝值: %.1f，已丢弃", dB_value);
            continue;
        }

        // 推入滑动窗口
        db_window_push(dB_value);

        // 更新 LVGL UI（需要锁，在 lvgl_port_task 中已加锁，这里用 lvgl 安全 API）
        // 通过 lv_async_call 或定时器方式更新更安全
        // 简化方案：直接更新（已在 LVGL 锁保护下）
        db_update_lvgl();

        // 节流上报：每 DB_REPORT_MS 毫秒上报一次
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((now - last_report_ms) >= DB_REPORT_MS) {
            db_update_mqtt_report();
            last_report_ms = now;
        }
    }
}
```

> **关于 LVGL 线程安全：** 如果你的 `lvgl_port_task` 已经用 `_lock_acquire(&lvgl_api_lock)` 保护，则 `db_update_lvgl()` 需要在该锁内调用。推荐方案是使用 `lv_async_call()` 或 LVGL 定时器来更新 UI，避免跨线程直接操作 LVGL 对象。
>
> **更安全的 LVGL 更新方式：**
> ```c
> // 在 db_window_push 后，通过 LVGL 定时器异步更新
> // 不直接调用 db_update_lvgl()
> ```

#### 3.4 Getter 函数（供外部访问）

```c
// ===== 新增 getter（detail_dB_logic.h 中声明） =====
bool  dB_get_has_data(void)  { return db_has_data; }
float dB_get_instant(void)   { return db_instant; }
float dB_get_avg(void)       { return db_avg; }
float dB_get_min(void)       { return db_min; }
float dB_get_max(void)       { return db_max; }
```

#### 3.5 初始化时发送 Init 命令（子命令码方式）

```c
void dB_init(void) {
    uint8_t sub_cmd = SUB_CMD_INIT;
    msg_Request(CMD_DB, &sub_cmd, 1);
}

void dB_deinit(void) {
    uint8_t sub_cmd = SUB_CMD_DEINIT;
    msg_Request(CMD_DB, &sub_cmd, 1);
}
```

---

### 第 4 步：detail_temp_logic.c — 温湿度滑动窗口

**文件：** `ESP/main/components/my_screen/src/detail_temp_logic.c`

逻辑与分贝类似，但窗口参数不同：

```c
// ===== 滑动窗口配置 =====
#define TEMP_WINDOW_SIZE  10       // 10 个点（@2s = 20 秒窗口）
#define TEMP_REPORT_MS    20000    // 每 20 秒上报一次

// ===== 温度滑动窗口 =====
static float temp_window[TEMP_WINDOW_SIZE];
static int   temp_window_idx  = 0;
static int   temp_window_fill = 0;

static float temp_instant  = 0.0f;
static float temp_avg      = 0.0f;
static float temp_min      = 999.0f;
static float temp_max      = -999.0f;
static bool  temp_has_data = false;

// ===== 湿度滑动窗口（同理） =====
static float hum_window[TEMP_WINDOW_SIZE];
static int   hum_window_idx  = 0;
static int   hum_window_fill = 0;

static float hum_instant  = 0.0f;
static float hum_avg      = 0.0f;
static float hum_min      = 999.0f;
static float hum_max      = 0.0f;
static bool  hum_has_data = false;

static uint32_t last_temp_report_ms = 0;
```

#### 4.1 窗口推送函数

```c
static void temp_window_push(float value) {
    temp_window[temp_window_idx] = value;
    temp_window_idx = (temp_window_idx + 1) % TEMP_WINDOW_SIZE;
    if (temp_window_fill < TEMP_WINDOW_SIZE) temp_window_fill++;

    float sum = 0.0f;
    temp_min = 999.0f;
    temp_max = -999.0f;
    for (int i = 0; i < temp_window_fill; i++) {
        float v = temp_window[i];
        sum += v;
        if (v < temp_min) temp_min = v;
        if (v > temp_max) temp_max = v;
    }
    temp_avg     = sum / (float)temp_window_fill;
    temp_instant = value;
    temp_has_data = true;
}

// hum_window_push() 同理，只是变量名不同
```

#### 4.2 LVGL UI 更新

```c
extern lv_obj_t * ui_tempValue;     // 详情页温度
extern lv_obj_t * ui_humidityValue; // 详情页湿度
extern lv_obj_t * ui_tempMaxVal;
extern lv_obj_t * ui_tempMinVal;
extern lv_obj_t * ui_tempAvgVal;
extern lv_obj_t * ui_temperatureNum; // 主界面温度数字

static void temp_update_lvgl(void) {
    char buf[16];

    if (ui_temperatureNum != NULL && temp_has_data) {
        snprintf(buf, sizeof(buf), "%.1fC", temp_instant);
        lv_label_set_text(ui_temperatureNum, buf);
    }
    if (ui_tempValue != NULL && temp_has_data) {
        snprintf(buf, sizeof(buf), "%.1f", temp_instant);
        lv_label_set_text(ui_tempValue, buf);
    }
    if (ui_tempAvgVal != NULL && temp_has_data) {
        snprintf(buf, sizeof(buf), "%.1f", temp_avg);
        lv_label_set_text(ui_tempAvgVal, buf);
    }
    if (ui_tempMaxVal != NULL && temp_window_fill > 0) {
        snprintf(buf, sizeof(buf), "%.1f", temp_max);
        lv_label_set_text(ui_tempMaxVal, buf);
    }
    if (ui_tempMinVal != NULL && temp_window_fill > 0) {
        snprintf(buf, sizeof(buf), "%.1f", temp_min);
        lv_label_set_text(ui_tempMinVal, buf);
    }
    if (ui_humidityValue != NULL && hum_has_data) {
        snprintf(buf, sizeof(buf), "%.1f%%", hum_instant);
        lv_label_set_text(ui_humidityValue, buf);
    }
}
```

#### 4.3 temp_task 改造

```c
static void temp_task(void *arg) {
    TempHumData data;
    while (1) {
        if (xQueueReceive(temp_queue, &data, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!data.valid) continue;

        // 温度校验
        if (data.temperature > -40.0f && data.temperature < 80.0f) {
            temp_window_push(data.temperature);
        }
        // 湿度校验
        if (data.humidity >= 0.0f && data.humidity <= 100.0f) {
            hum_window_push(data.humidity);
        }

        // 更新 LVGL
        temp_update_lvgl();

        // 节流上报
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((now - last_temp_report_ms) >= TEMP_REPORT_MS) {
            if (temp_has_data) {
                mqtt_report_set_float("temp_instant", temp_instant);
                mqtt_report_set_float("temp_avg",     temp_avg);
            }
            if (hum_has_data) {
                mqtt_report_set_float("humi_instant", hum_instant);
                mqtt_report_set_float("humi_avg",     hum_avg);
            }
            mqtt_report_set_bool("connect_status", true);
            mqtt_report_request_publish();
            last_temp_report_ms = now;
        }
    }
}
```

---

### 第 5 步：mqtt_report.c — 注册新上报字段

**文件：** `ESP/main/components/my_mqtt/src/mqtt_report.c`

在 `s_items[]` 数组中，替换旧字段，新增统计字段：

```c
static mqtt_report_item_t s_items[MQTT_REPORT_MAX_ITEMS] = {
    // 设备状态
    {.key = "connect_status", .type = MQTT_REPORT_BOOL, .valid = false},

    // ===== 分贝相关 =====
    {.key = "dB_instant",    .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "dB_avg",        .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "dB_min",        .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "dB_max",        .type = MQTT_REPORT_FLOAT, .valid = false},

    // ===== 温湿度相关 =====
    {.key = "temp_instant",  .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "temp_avg",      .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "humi_instant",  .type = MQTT_REPORT_FLOAT, .valid = false},
    {.key = "humi_avg",      .type = MQTT_REPORT_FLOAT, .valid = false},

    // 旧字段保留（兼容过渡期，可在后续删除）
    // {.key = "dB_value",    .type = MQTT_REPORT_FLOAT, .valid = false},
    // {.key = "temp_value",  .type = MQTT_REPORT_FLOAT, .valid = false},
    // {.key = "humi_value",  .type = MQTT_REPORT_FLOAT, .valid = false},
};
```

> **注意：** 数组元素数量需 ≤ `MQTT_REPORT_MAX_ITEMS`（当前为 16），新字段共 9 个，在限制内。

---

## 四、OneNET 平台配置

### 4.1 进入物模型管理

```
OneNET 控制台 → 产品管理 → NZzG1mvALQ → 物模型管理
```

### 4.2 添加新属性

逐一添加以下属性。**删除旧的 `dB_value`、`temp_value`、`humi_value`**（或先保留过渡）。

| 功能名称 | 标识符 | 数据类型 | 取值范围 | 单位 | 步长 | 读写 |
|---------|--------|---------|---------|------|------|------|
| 瞬时分贝 | `dB_instant` | float | 0 ~ 180 | dB | 0.1 | 只读 |
| 平均分贝 | `dB_avg` | float | 0 ~ 180 | dB | 0.1 | 只读 |
| 最小分贝 | `dB_min` | float | 0 ~ 180 | dB | 0.1 | 只读 |
| 最大分贝 | `dB_max` | float | 0 ~ 180 | dB | 0.1 | 只读 |
| 瞬时温度 | `temp_instant` | float | -40 ~ 80 | ℃ | 0.1 | 只读 |
| 平均温度 | `temp_avg` | float | -40 ~ 80 | ℃ | 0.1 | 只读 |
| 瞬时湿度 | `humi_instant` | float | 0 ~ 100 | % | 0.1 | 只读 |
| 平均湿度 | `humi_avg` | float | 0 ~ 100 | % | 0.1 | 只读 |
| 连接状态 | `connect_status` | bool | — | — | — | 只读 |

### 4.3 数据可视化仪表盘

```
OneNET 控制台 → 数据可视化 → 新建仪表盘 → 取名 "环境监测看板"
```

**布局建议：**

```
┌──────────────────────────────────────────┐
│  环境监测看板                 2026-06-04  │
├────────────┬────────────┬────────────────┤
│  瞬时 dB   │  平均 dB   │  dB 趋势曲线   │
│  [ 45.3 ]  │  [ 43.8 ]  │  ╱╲  ╱╲      │
│  大号数字  │  大号数字  │  ╲╱  ╲╱      │
│            │            │  (最近1小时)   │
├────────────┼────────────┼────────────────┤
│  瞬时温度  │  瞬时湿度  │  温湿度曲线    │
│  [ 25.5°C] │  [ 60.2%]  │  ═══════      │
│            │            │  (最近24小时)  │
├────────────┴────────────┴────────────────┤
│  数据记录表格（时间/分贝/温度/湿度）       │
└──────────────────────────────────────────┘
```

### 4.4 公开分享

```
仪表盘 → 右上角「分享」→ 开启公开分享 → 复制链接
```

手机浏览器打开链接即可查看实时数据。

---

## 五、LVGL 安全更新注意事项

由于 `dB_task` 和 `lvgl_port_task` 是**不同 FreeRTOS 任务**，直接操作 LVGL 对象不安全。

**推荐方案：使用 LVGL 定时器异步更新 UI**

在 `dB_task` 中只更新 `db_instant/db_avg/db_min/db_max` 变量，不直接调用 `lv_label_set_text()`。改为：

```c
// 在 dB_start() 或 screen_init 中创建一个 LVGL 定时器
static lv_timer_t * db_ui_timer = NULL;

static void db_ui_update_cb(lv_timer_t * timer) {
    // 此回调在 lvgl_port_task 的上下文中执行，已持有锁
    db_update_lvgl();
}

void dB_start(void) {
    // ... 原有初始化 ...

    // 创建 LVGL 定时器，每 500ms 刷新一次 UI
    db_ui_timer = lv_timer_create(db_ui_update_cb, 500, NULL);
}
```

这样 `lv_timer_create` 的回调会在 `lv_timer_handler()` 中被调用，天然在 LVGL 锁的保护下。

温湿度同理。

---

## 六、MQTT 上报数据格式验证

设备上报后，在 OneNET 控制台 → 设备管理 → ESP02 → 数据流 中应看到：

```json
{
  "id": "1734567890",
  "version": "1.0",
  "params": {
    "dB_instant":    { "value": 46.10 },
    "dB_avg":        { "value": 45.30 },
    "dB_min":        { "value": 42.00 },
    "dB_max":        { "value": 48.50 },
    "temp_instant":  { "value": 25.50 },
    "temp_avg":      { "value": 25.30 },
    "humi_instant":  { "value": 60.20 },
    "humi_avg":      { "value": 59.80 },
    "connect_status": { "value": true }
  }
}
```

---

## 七、编译与烧录

```powershell
cd ESP
idf.py build
idf.py -p COMx flash monitor
```

---

## 八、功能验证清单

| 验证项 | 预期结果 | 验证方法 |
|--------|---------|---------|
| 滑动窗口有数据 | `db_window_fill` 从 0 递增到 200 | 加 `ESP_LOGI` 打印 |
| UI 平均值非空 | `ui_dBAvgVal` 显示数值 | 切换屏幕到分贝详情页 |
| UI 最大/最小非空 | `ui_dBMaxVal`、`ui_dBMinVal` 显示数值 | 同上 |
| MQTT 上报成功 | OneNET 数据流中有 `dB_avg` 等字段 | OneNET 控制台查看 |
| 温湿度滑动窗口 | 详情页 avg/min/max 有值 | 切换到温湿度详情页 |
| 数据有效性过滤 | 异常值不更新 UI 且不触发上报 | 日志中有 "无效分贝值" 警告 |
| 上报节流 | 分贝每 10s 一条、温湿度每 20s 一条 | OneNET 数据流时间戳间隔 |

---

## 九、ESP32 端改动总结

| 文件 | 操作 | 改动量 |
|------|------|--------|
| `my_serial.h` | 新增 `SensorDataBin`、`ProtocolSubCmd` | +18 行 |
| `mian_task.c` | SensorDataBin 解析 + TempHumData 分发 | +40 行 / -15 行 |
| `detail_dB_logic.c` | 🔥 滑动窗口 + 统计 + LVGL + MQTT | +120 行 / -20 行 |
| `detail_dB_logic.h` | 新增 getter 函数声明 | +5 行 |
| `detail_temp_logic.c` | 🔥 滑动窗口 + TempHumData | +120 行 / -18 行 |
| `detail_temp_logic.h` | 新增 TempHumData 结构体 + getter | +12 行 |
| `mqtt_report.c` | 注册新 key（avg/min/max/instant） | +8 行 / -5 行 |
