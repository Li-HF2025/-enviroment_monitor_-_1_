# 项目改进计划

> 基于 2026-05-23 代码审查报告生成，按优先级排列。

---

## 第一阶段：修复已知 Bug（预计 1-2 天）

### 1.1 STM32 端：删除接收帧后的多余回传

**文件：** `STM/Core/Src/MySrc/mySerial.c` 第 255 行

`uart_rx_task` 中解析完一帧后会通过 `msg_Report()` 把数据回传给 ESP32，造成通信环路。

```c
// 删除这行
msg_Report(item.cmd, item.payload, item.payload_len);
```

### 1.2 STM32 端：`HAL_Delay` 改为 `osDelay`

**文件：** `STM/Core/Src/MySrc/db.c` 第 122 行

FreeRTOS 任务中不应使用裸机阻塞延时。

```c
// 改前
HAL_Delay(100);
// 改后
osDelay(100);
```

### 1.3 STM32 端：删除重复 include

**文件：** `STM/Core/Src/freertos.c` 第 32 行

删除重复的 `#include "OLED.h"`。

### 1.4 STM32 端：修正 DHT22 读取间隔注释

**文件：** `STM/Core/Src/MySrc/temperature.c` 第 253 行

```c
// 改前
osDelay(1000); // 间隔1分钟读取一次
// 改后
osDelay(2000); // 每2秒读取一次（DHT22最大采样率0.5Hz）
```

### 1.5 ESP32 端：修正文件名拼写

**文件：** `ESP/main/components/my_serial/src/mian_task.c`

重命名为 `main_task.c`，同步更新 CMakeLists.txt 中的引用。

### 1.6 ESP32 端：触摸日志降级

**文件：** `ESP/main/components/my_screen/src/my_screen.c` 第 140 行

```c
// 改前
ESP_LOGI(TAG, "Touch at mapped(%d, %d) ...");
// 改后
ESP_LOGD(TAG, "Touch at mapped(%d, %d) ...");
```

---

## 第二阶段：架构解耦（预计 3-5 天）

### 2.1 WiFi 组件事件化改造

**目标：** 解除 `my_wifi` 对 `my_mqtt`、`my_ota`、`my_nvs`、`my_screen` 的直接依赖。

**方案：** 使用 ESP-IDF 的 Event Loop 机制。

**步骤：**

1. 定义自定义事件：

```c
// my_wifi.h 新增
typedef enum {
    MY_WIFI_EVENT_CONNECTED,      // WiFi 已连接且获取 IP
    MY_WIFI_EVENT_DISCONNECTED,   // WiFi 已断开
    MY_WIFI_EVENT_CONNECT_FAILED, // 连接失败
} my_wifi_event_id_t;

ESP_EVENT_DECLARE_BASE(MY_WIFI_EVENT_BASE);
```

2. `my_wifi.c` 中只发事件，不调用任何上层业务函数：

```c
// 替换 mqtt_app_start();
esp_event_post(MY_WIFI_EVENT_BASE, MY_WIFI_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);

// 替换 mqtt_app_stop();
esp_event_post(MY_WIFI_EVENT_BASE, MY_WIFI_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
```

3. 各业务组件各自订阅 WiFi 事件：

| 组件 | 订阅事件 | 行为 |
|------|---------|------|
| `my_mqtt` | CONNECTED | 启动 MQTT 客户端 |
| `my_mqtt` | DISCONNECTED | 停止 MQTT 客户端 |
| `my_ota` | CONNECTED | 上报版本号 |
| `detail_time_logic` | CONNECTED | 启动 SNTP 同步 |

4. 移除 `my_wifi/CMakeLists.txt` 中对 `my_mqtt`、`my_ota`、`my_screen` 的依赖。

### 2.2 传感器层与 MQTT 解耦

**目标：** `detail_dB_logic.c` 和 `detail_temp_logic.c` 不再直接调用 `mqtt_report_set_*`。

**方案：** 拆分 MQTT 上报为一个独立的"上报调度器"。

**步骤：**

1. 传感器任务只更新数据缓存 + 发出"数据已更新"事件：

```c
// detail_dB_logic.c — 删掉这些
mqtt_report_set_float("dB_value", dB_value);
mqtt_report_set_bool("connect_status", true);
mqtt_report_request_publish();

// 改为发事件
esp_event_post(SENSOR_EVENT_BASE, SENSOR_DB_UPDATED, &dB_value, sizeof(float), 0);
```

2. 新建一个轻量的 `data_report_dispatcher`，订阅所有传感器的数据更新事件，统一写入 `mqtt_report` 并触发上报。

### 2.3 统一全局状态管理

**目标：** 消除 `extern` 跨组件直接访问变量。

**方案：** 为需要跨组件访问的数据提供访问函数。

**步骤：**

1. `my_serial` — 不再暴露 `uart_tx_queue`，提供 `serial_send_frame()` 封装
2. UI 对象 — 每个 detail_logic 模块提供 setter/getter：

```c
// detail_dB_logic.h 新增
void dB_logic_set_ui_label(lv_obj_t *label);
void dB_logic_set_ui_value(float value);  // 线程安全更新 UI
```

---

## 第三阶段：协议优化（预计 1-2 天）

### 3.1 协议载荷从字符串改为子命令码

**目标：** 替换 `strcmp("DB Init")` 这种字符串比较方式。

**方案：**

1. 在协议定义中增加子命令码：

```c
// my_serial.h 两端同步更新
typedef enum {
    SUB_CMD_INIT    = 0x01,  // 初始化/使能
    SUB_CMD_DEINIT  = 0x02,  // 反初始化/停用
    SUB_CMD_STATUS  = 0x03,  // 查询状态
} ProtocolSubCmd;
```

2. 请求帧的 payload 第一个字节改为子命令码
3. STM32 端 `main_task.c` 的 switch 改为一字节比较：

```c
// 改前
if (strcmp(buf, "DB Init") == 0) { ... }
// 改后
if (buf[0] == SUB_CMD_INIT) { ... }
```

### 3.2 统一校验和命名

将代码中的 `CRC`、`checksum`、`crc` 统一为一个名称，因为实际算法是累加校验而非 CRC。

```c
// 建议统一为 checksum
#define PROTOCOL_CHECKSUM_SIZE 2
```

---

## 第四阶段：STM32 侧改进（预计 2-3 天）

### 4.1 传感器不自动启动

**文件：** `STM/Core/Src/freertos.c`

删除无条件启动，改为只创建任务（任务内等待命令）：

```c
// 改前
DHT22_Init();
DB_Init();

// 改后
// 只创建任务，不自动使能。由 ESP32 通过协议帧下发 Init 命令控制启停。
```

### 4.2 DHT22 任务改信号量等待

**文件：** `STM/Core/Src/MySrc/temperature.c`

将轮询 `g_dht22_enabled` 改为信号量/任务通知：

```c
void DHT22_Task(void *argument) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // 等待使能信号
        // 执行读取和上报
    }
}

void DHT22_Init(void) {
    g_dht22_enabled = true;
    xTaskNotifyGive(dht22TaskHandle);  // 唤醒任务
}
```

### 4.3 DHT22 错误返回值用宏代替魔术数字

```c
#define DHT22_ERROR_VALUE_TEMP  -1000.0f
#define DHT22_ERROR_VALUE_HUMI  -1.0f
```

---

## 第五阶段：功能补全（预计 2-3 天）

### 5.1 MQTT 下行命令处理

**文件：** `ESP/main/components/my_mqtt/src/my_mqtt.c` `MQTT_EVENT_DATA` 分支

实现接收到的云端命令解析和分发。

### 5.2 STM32 端 OLED 调试输出用宏包裹

```c
#ifdef DBG_OLED_ENABLE
#define DBG_OLED(line, col, str) OLED_WriteString(line, col, str)
#else
#define DBG_OLED(line, col, str) ((void)0)
#endif
```

---

## 修改顺序建议

```
第一阶段（Bug修复）
  ├─ 1.1 删除多余回传          ← 最重要，今天就可以改
  ├─ 1.2 HAL_Delay → osDelay
  ├─ 1.3 删除重复 include
  ├─ 1.4 修正注释
  ├─ 1.5 文件名拼写
  └─ 1.6 日志降级

第二阶段（架构解耦）            ← 改动最大，建议集中时间做
  ├─ 2.1 WiFi 事件化
  ├─ 2.2 传感器-MQTT 解耦
  └─ 2.3 全局状态封装

第三阶段（协议优化）
  ├─ 3.1 子命令码替代字符串
  └─ 3.2 命名统一

第四阶段（STM32改进）
  ├─ 4.1 传感器按需启动
  ├─ 4.2 信号量替代轮询
  └─ 4.3 魔术数字

第五阶段（功能补全）
  ├─ 5.1 MQTT 下行命令
  └─ 5.2 调试宏
```

---

## 注意事项

- 第二阶段改动较大，建议新建一个分支如 `refactor/decouple-wifi`
- 每次改完一个阶段就编译验证，避免积累问题
- 协议修改（第三阶段）需要**同时改 ESP32 和 STM32 两端**，改完要做联调测试
- 第一阶段 1.1 是唯一会导致运行时异常的 bug，其他都是优化项
