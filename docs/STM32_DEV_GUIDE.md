# STM32 端数据处理升级 — 开发者指南

> 适用固件：STM32F103C8T6 | 框架：STM32Cube HAL + FreeRTOS + PlatformIO
> 前置条件：已完成 IMPROVEMENT_PLAN.md 中第一阶段 Bug 修复

---

## 一、升级目标概览

```
升级前：STM32 采集原始值 → 透传给 ESP32（ASCII/二进制混用）
升级后：STM32 采集原始值 → 本地滤波 → 统一二进制格式 → 上报 ESP32
```

| 改进项 | 说明 |
|--------|------|
| 统一数据格式 | 所有传感器数据用 `SensorDataBin`（6字节固定结构）代替 ASCII 字符串 |
| 传感器按需启动 | 不在 `freertos.c` 中自动 Init，由 ESP32 下发命令控制 |
| DHT22 任务通知 | 轮询 `g_dht22_enabled` 改为 `ulTaskNotifyTake` 阻塞等待 |
| 魔术数字宏化 | `-1000.0f` / `-1.0f` 改为 `DHT22_ERROR_VALUE_TEMP` / `DHT22_ERROR_VALUE_HUMI` |
| 统一序列号 | `msg_Report/Response/Request` 共享一个 `g_seq_num` |

**不做什么：** STM32 不做滑动窗口统计（内存有限），统计计算放在 ESP32 端。

---

## 二、涉及文件清单

```
STM/Core/Inc/MyInc/mySerial.h          ← 新增 SensorDataBin、ProtocolSubCmd
STM/Core/Inc/MyInc/temperature.h        ← 新增 DHT22 错误值宏
STM/Core/Src/MySrc/mySerial.c           ← 统一序列号、新增 sensor_report()
STM/Core/Src/MySrc/temperature.c        ← DHT22_Send_Report 改二进制、任务通知
STM/Core/Src/MySrc/db.c                 ← msg_Response 改 sensor_report()
STM/Core/Src/MySrc/main_task.c          ← strcmp 改子命令码
STM/Core/Src/freertos.c                 ← 移除无条件 DHT22_Init() / DB_Init()
```

---

## 三、逐步实施

### 第 1 步：mySerial.h — 新增数据结构和子命令码

**文件：** `STM/Core/Inc/MyInc/mySerial.h`

在 `CMDType` 枚举后面追加：

```c
// ===== 新增：传感器统一数据格式（与 ESP32 端同步） =====
typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;   // 0x01=温度, 0x02=分贝, 0x03=光照, 0x04=湿度
    uint8_t  status;        // bit0=数据有效, bit1=超量程, bit2=传感器故障
    int16_t  value_x10;     // 数值 × 10（有符号，支持负温度）
    uint16_t reserved;      // 预留扩展
} SensorDataBin;             // 固定 6 字节

// ===== 新增：协议子命令码 =====
typedef enum {
    SUB_CMD_INIT    = 0x01,  // 初始化/使能传感器
    SUB_CMD_DEINIT  = 0x02,  // 反初始化/停用传感器
    SUB_CMD_STATUS  = 0x03,  // 查询传感器状态
} ProtocolSubCmd;

// ===== 新增：传感器数据上报封装函数 =====
void sensor_report(uint8_t cmd, uint8_t sensor_type, int16_t value_x10, uint8_t status);
```

---

### 第 2 步：mySerial.c — 实现 sensor_report() + 统一序列号

**文件：** `STM/Core/Src/MySrc/mySerial.c`

#### 2.1 新增统一序列号 + sensor_report 函数

在 `msg_Request()` 函数后面追加：

```c
// ===== 统一序列号（替换各函数内 static seq_num） =====
static uint16_t g_seq_num = 0;

// ===== 新增：传感器数据上报（统一二进制格式） =====
void sensor_report(uint8_t cmd, uint8_t sensor_type, int16_t value_x10, uint8_t status) {
    SensorDataBin data;
    data.sensor_type = sensor_type;
    data.status      = status;
    data.value_x10   = value_x10;
    data.reserved    = 0;

    UartTxItem tx_item;
    tx_item.cmd         = cmd;
    tx_item.msg_type    = MSG_TYPE_RESPONSE;
    tx_item.seq         = g_seq_num++;
    tx_item.payload_len = sizeof(SensorDataBin);
    memcpy(tx_item.payload, &data, sizeof(SensorDataBin));
    osMessageQueuePut(uart_tx_queue, &tx_item, 0U, 10U);
}
```

#### 2.2 将 msg_Report / msg_Response / msg_Request 的 static seq_num 改为 g_seq_num

```c
// 改前（三个函数各有一行）：
static uint16_t seq_num = 0;
tx_item.seq = seq_num++;

// 改后：
tx_item.seq = g_seq_num++;
// 删除各函数内的 static uint16_t seq_num = 0;
```

---

### 第 3 步：temperature.h — 新增错误值宏

**文件：** `STM/Core/Inc/MyInc/temperature.h`

在文件末尾 `#endif` 前追加：

```c
// ===== DHT22 错误值 / 有效范围宏 =====
#define DHT22_ERROR_VALUE_TEMP   (-1000.0f)
#define DHT22_ERROR_VALUE_HUMI   (-1.0f)
#define DHT22_VALID_TEMP_MIN     (-40.0f)
#define DHT22_VALID_TEMP_MAX     (80.0f)
#define DHT22_VALID_HUMI_MIN     (0.0f)
#define DHT22_VALID_HUMI_MAX     (100.0f)
```

---

### 第 4 步：temperature.c — 核心改造（任务通知 + 二进制上报）

**文件：** `STM/Core/Src/MySrc/temperature.c`

#### 4.1 删除 g_dht22_enabled 轮询，改为任务通知

```c
// ===== DHT22_Task 改造前 =====
void DHT22_Task(void *argument) {
    msg_Report(CMD_TEMPERATURE, (uint8_t*)"DHT22 Task running", 20);
    while (1) {
        if (!g_dht22_enabled) {   // ❌ 轮询
            osDelay(200);
            continue;
        }
        // ... 读取、上报 ...
        osDelay(1000);
    }
}

// ===== DHT22_Task 改造后 =====
void DHT22_Task(void *argument) {
    while (1) {
        // ✅ 阻塞等待使能通知（无 CPU 空转）
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 收到通知后，持续读取直到被 DeInit 打断
        while (g_dht22_enabled) {
            float temperature, humidity;
            DHT22_Read(&temperature, &humidity);

            if (temperature > DHT22_VALID_TEMP_MIN && humidity >= DHT22_VALID_HUMI_MIN) {
                // 上报温度
                int16_t temp_x10 = (int16_t)(temperature * 10.0f
                    + (temperature >= 0.0f ? 0.5f : -0.5f));
                sensor_report(CMD_TEMPERATURE, 0x01, temp_x10, 0x00);

                // 上报湿度
                uint16_t hum_x10 = (uint16_t)(humidity * 10.0f + 0.5f);
                sensor_report(CMD_TEMPERATURE, 0x04, (int16_t)hum_x10, 0x00);

                // OLED 调试显示
                char temp_str[24], hum_str[24];
                snprintf(temp_str, sizeof(temp_str), "Temp: %.1fC", temperature);
                snprintf(hum_str,  sizeof(hum_str),  "Hum:  %.1f%%", humidity);
                OLED_WriteString(0, 0, temp_str);
                OLED_WriteString(1, 0, hum_str);
            } else {
                // 上报错误状态
                sensor_report(CMD_TEMPERATURE, 0x01, 0, 0x04);  // status bit2=传感器故障
                sensor_report(CMD_TEMPERATURE, 0x04, 0, 0x04);
            }

            osDelay(2000);  // DHT22 最大采样率 0.5Hz
        }
    }
}
```

#### 4.2 删除旧的 DHT22_Send_Report() 函数

旧函数用 4 字节自定义格式，现在统一用 `SensorDataBin`，直接删除整个函数体。

#### 4.3 DHT22_Init / DHT22_DeInit 加入任务通知

```c
void DHT22_Init(void) {
    DHT22_Pin_Output();
    HAL_GPIO_WritePin(DHT22_PORT, DHT22_PIN, GPIO_PIN_SET);
    g_dht22_enabled = true;
    xTaskNotifyGive(dht22TaskHandle);  // ✅ 唤醒任务
}

void DHT22_DeInit(void) {
    g_dht22_enabled = false;
    DHT22_Pin_Input();
}
```

> **注意：** `dht22TaskHandle` 需要从 `DHT22_RTOS_Init()` 中保存为模块级静态变量。
>
> ```c
> static TaskHandle_t dht22TaskHandle = NULL;
>
> void DHT22_RTOS_Init(void) {
>     // ... DWT 初始化 ...
>     dht22TaskHandle = osThreadNew(DHT22_Task, NULL, &dht22_task_attr);
> }
> ```

---

### 第 5 步：db.c — 用 sensor_report() 替代 msg_Response()

**文件：** `STM/Core/Src/MySrc/db.c`

在 `StartDecibelTask()` 中找到上报代码，替换：

```c
// 改前：
char report_str[8];
snprintf(report_str, sizeof(report_str), "%u.%u", report_whole, report_frac);
msg_Response(CMD_DB, (uint8_t *)report_str, (uint16_t)strlen(report_str));

// 改后：
uint16_t db_x10 = (uint16_t)(db_smooth * 10.0f + 0.5f);
sensor_report(CMD_DB, 0x02, (int16_t)db_x10, 0x00);
```

> **说明：** `sensor_type = 0x02` 表示分贝数据，ESP32 端根据此字段区分数据类型。

---

### 第 6 步：main_task.c — strcmp 改为子命令码

**文件：** `STM/Core/Src/MySrc/main_task.c`

```c
// 改前：
case CMD_DB:
    if(strcmp(buf, "DB Init") == 0){
        DB_Init();
        msg_Report(CMD_DB, (uint8_t*)"DB Init OK", 10);
    } else if(strcmp(buf, "DB DeInit") == 0){
        DB_DeInit();
        ...

// 改后：
case CMD_DB:
    if(item.payload_len >= 1 && item.payload[0] == SUB_CMD_INIT){
        DB_Init();
        msg_Response(CMD_DB, (uint8_t*)"OK", 2);
    } else if(item.payload_len >= 1 && item.payload[0] == SUB_CMD_DEINIT){
        DB_DeInit();
        msg_Response(CMD_DB, (uint8_t*)"OK", 2);
    } else {
        msg_Response(CMD_DB, (uint8_t*)"ERR", 3);
    }
    break;

case CMD_TEMPERATURE:
    if(item.payload_len >= 1 && item.payload[0] == SUB_CMD_INIT){
        DHT22_Init();
        msg_Response(CMD_TEMPERATURE, (uint8_t*)"OK", 2);
    } else if(item.payload_len >= 1 && item.payload[0] == SUB_CMD_DEINIT){
        DHT22_DeInit();
        msg_Response(CMD_TEMPERATURE, (uint8_t*)"OK", 2);
    } else {
        msg_Response(CMD_TEMPERATURE, (uint8_t*)"ERR", 3);
    }
    break;
```

同时删除 `display_value()` 中不再需要的 ASCII 字符串处理逻辑（`line_hex`、`line` 相关代码可以简化或删除）。

---

### 第 7 步：freertos.c — 移除无条件传感器启动

**文件：** `STM/Core/Src/freertos.c`

```c
// 改前：
mySerial_RTOS_Init();
mySerial_init();

DHT22_RTOS_Init();
DHT22_Init();        // ❌ 无条件启动
DB_RTOS_Init();
DB_Init();           // ❌ 无条件启动
main_task_RTOS_Init();

// 改后：
mySerial_RTOS_Init();
mySerial_init();

DHT22_RTOS_Init();   // ✅ 只创建任务，不使能
DB_RTOS_Init();      // ✅ 只创建任务，不使能
main_task_RTOS_Init();
// 传感器由 ESP32 通过协议帧下发 Init 命令后按需启动
```

---

## 四、编译验证

```powershell
# PlatformIO 项目
cd STM
pio run

# 或 STM32CubeIDE
# Project → Build All
```

预期：无编译错误。

---

## 五、数据流验证（联调 ESP32 后）

### 5.1 通过 ESP32 日志确认格式

启用 ESP32 端 main_task 的日志（`ESP_LOGI`），应看到：

```
收到数据: sensor_type=0x01(温度), value_x10=255, status=0x00 → 25.5°C
收到数据: sensor_type=0x04(湿度), value_x10=602, status=0x00 → 60.2%
收到数据: sensor_type=0x02(分贝), value_x10=453, status=0x00 → 45.3dB
```

### 5.2 验证传感器按需启停

1. ESP32 启动后，STM32 OLED 上不应自动显示温湿度（等待 Init 命令）
2. ESP32 通过 LVGL 开关下发 `Init` 命令后，OLED 上出现 Temp/Hum 显示
3. ESP32 下发 `DeInit` 后，OLED 显示停止更新

---

## 六、STM32 端改动总结

| 文件 | 操作 | 改动量 |
|------|------|--------|
| `mySerial.h` | 新增 `SensorDataBin`、`ProtocolSubCmd`、`sensor_report()` 声明 | +20 行 |
| `mySerial.c` | 新增 `sensor_report()` 实现 + 统一序列号 | +20 行 / -6 行 |
| `temperature.h` | 新增错误值宏 | +8 行 |
| `temperature.c` | 重写 `DHT22_Task`、删除旧 `DHT22_Send_Report` | +40 行 / -30 行 |
| `db.c` | `msg_Response` → `sensor_report` | -3 行 / +2 行 |
| `main_task.c` | `strcmp` → 子命令码 + 简化 display_value | +15 行 / -30 行 |
| `freertos.c` | 删除无条件 `Init()` 调用 | -2 行 |
