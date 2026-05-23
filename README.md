# 基于 STM32 + ESP32 的双板环境监测系统

## 项目定位

一个从零搭建的嵌入式物联网综合项目，覆盖**传感器采集 → 数据解析 → 本地显示 → 云端上报 → 远程控制**完整链路。系统使用两块 MCU 分工协作：**STM32F103C8T6** 作为下位机负责传感器控制与数据采集，**ESP32-S3** 作为上位机负责网络通信、MQTT 云端交互、LVGL 触摸屏人机界面。

## 项目背景与学习目标

这是我的第一个综合性嵌入式项目。在学习过程中，我分别接触过传感器驱动、FreeRTOS 任务调度、WiFi 联网、MQTT 协议、LVGL 图形界面等独立知识点，但从未将它们整合为一个完整系统。这个项目的目的就是把这些分散的技能点串成一条端到端的数据链路，理解"数据从哪里来、经过哪些环节、最终到哪里去"。

通过这个项目，我掌握了：

- STM32 与 ESP32 双 MCU 之间自定义二进制通信协议的设计与实现
- FreeRTOS 多任务编程：队列、事件组、任务通知、互斥锁、状态机
- LVGL 图形库的使用和 SquareLine Studio 可视化 UI 设计
- MQTT 协议接入 OneNET 物联网平台（认证、主题设计、属性上报）
- ADC + DMA 循环双缓冲数据采集 + 数字滤波算法
- DHT22 单总线时序的精确控制（DWT 微秒延时 + 关中断原子操作）
- OneNET OTA 固件升级的完整流程（含 HMAC-SHA256 API 鉴权）
- NVS 持久化存储与 Wi-Fi 凭据管理
- ESP-IDF 组件化工程结构设计与 CMake 构建系统
- 嵌入式系统中常见架构问题的识别与改进思路

## 实物展示

> 请在此处添加你的项目实物照片。建议拍摄：

| 照片内容 | 建议文件名 | 说明 |
|---------|-----------|------|
| 整体接线俯视图 | `docs/images/overview.jpg` | 展示两块板子和所有外设的完整连线 |
| ESP32-S3 + 屏幕 | `docs/images/esp32_screen.jpg` | ESP32 连接 ILI9341 屏幕的近景 |
| STM32 + 传感器 | `docs/images/stm32_sensors.jpg` | STM32 连接 DHT22 和分贝模块 |
| 屏幕界面截图 | `docs/images/ui_main.jpg` | LVGL 主界面运行效果 |
| OLED 调试显示 | `docs/images/oled_debug.jpg` | STM32 OLED 显示的实时数据 |

![系统整体接线](docs/images/overview.jpg)

### 参考硬件图片链接

在实际拍照之前，以下链接可查看各模块的外观（均为官方或权威来源，链接相对稳定）：

| 模块 | 参考链接 |
|------|---------|
| ESP32-S3-DevKitC-1 | [Espressif 官方文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html) |
| STM32F103C8T6 最小系统板 | [STM32 Blue Pill Wiki](https://stm32-base.org/boards/STM32F103C8T6-Blue-Pill) |
| 2.8" ILI9341 SPI TFT + XPT2046 | [LCD Wiki](http://www.lcdwiki.com/2.8inch_SPI_Module_ILI9341_SKU:MSP2807) |
| DHT22 温湿度传感器 | [Adafruit 产品页](https://www.adafruit.com/product/385) |
| 0.96" SSD1306 OLED (I2C) | [Adafruit 产品页](https://www.adafruit.com/product/326) |

## 核心指标

| 维度 | 说明 |
|------|------|
| 主控芯片 | STM32F103C8T6（ARM Cortex-M3, 72MHz）+ ESP32-S3（Xtensa LX7, 240MHz） |
| 传感器 | DHT22 温湿度、模拟分贝计（ADC 采集） |
| 板间通信 | UART 自定义二进制协议，115200bps |
| 本地显示 | 2.8" ILI9341 SPI 触摸屏 (240×320)，LVGL v9.2 渲染，XPT2046 触摸 |
| 调试显示 | 0.96" SSD1306 OLED (I2C, 128×64) |
| 云端平台 | OneNET 物联网平台，MQTT 协议 |
| 网络能力 | Wi-Fi STA 模式、SNTP 网络时间同步 |
| 固件升级 | OneNET OTA 远程固件升级（HTTPS） |
| 操作系统 | FreeRTOS（双端均使用） |
| ESP32 框架 | ESP-IDF v5.x + CMake |
| STM32 框架 | STM32Cube HAL + PlatformIO |
| UI 设计工具 | SquareLine Studio 1.6.1 |

---

## 系统总体架构

### 为什么用双板？

如果只用 STM32，它没有 WiFi 能力，无法联网，也带不动 LVGL 触摸屏。如果只用 ESP32-S3，它的 ADC 精度不如 STM32，GPIO 抗干扰能力也偏弱，且没有 5V 容忍引脚。两块板各司其职，通过串口协作，是嵌入式系统中常见的架构模式。这种模式的核心理念是**将实时性要求高、硬件绑定的底层任务与计算密集、交互复杂的上层任务分离到不同 MCU 上执行**。

### 系统拓扑

```text
                        ┌─────────────┐
                        │  OneNET 云平台 │
                        │  (MQTT Broker) │
                        └──────┬──────┘
                               │ MQTT (TCP/WiFi)
   ┌───────────────────────────┼───────────────────────────┐
   │                     ESP32-S3                          │
   │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
   │  │  WiFi STA │  │ SNTP 时间 │  │ LVGL 触摸屏      │   │
   │  │  + MQTT   │  │   同步    │  │ (ILI9341+XPT2046)│   │
   │  └──────────┘  └──────────┘  └──────────────────┘   │
   │                         │                             │
   │                    UART1 (115200)                     │
   │              自定义二进制协议                          │
   └─────────────────────────┼───────────────────────────┘
                             │
   ┌─────────────────────────┼───────────────────────────┐
   │                     STM32F103C8                      │
   │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
   │  │ DHT22    │  │ 分贝模块  │  │ OLED 调试屏       │   │
   │  │ (PB0)    │  │ (PB1 ADC) │  │ (I2C, 本地调试)  │   │
   │  └──────────┘  └──────────┘  └──────────────────┘   │
   └────────────────────────────────────────────────────┘
```

### 数据流（两条链路）

**上行链路（采集 → 显示 → 云端）：**

```
传感器 → STM32 ADC/GPIO → DMA缓冲 → 滤波算法 → 协议帧
  → UART → ESP32 状态机解析 → 主任务队列分发
    → LVGL 界面更新 + MQTT JSON 上报 → OneNET 平台
```

**下行链路（界面/云端 → 控制传感器）：**

```
触摸屏点击 / MQTT 云端命令 → ESP32 发送请求帧
  → UART → STM32 解析子命令 → 使能/停用传感器
    → 返回上报帧确认执行结果
```

### 技术栈详情

#### ESP32-S3 侧

| 层级 | 技术/组件 | 用途 |
|------|-----------|------|
| 构建系统 | ESP-IDF v5.x + CMake | 项目构建、menuconfig 配置管理 |
| 实时操作系统 | FreeRTOS (原生 API) | 任务调度、队列通信、事件组同步 |
| 网络层 | ESP-NETIF + LwIP | TCP/IP 协议栈 |
| Wi-Fi | ESP-WIFI | STA 模式连接、AP 扫描、自动重连 |
| 应用协议 | MQTT (esp-mqtt client) | OneNET 平台数据上报与命令订阅 |
| 时间同步 | SNTP (lwip/apps) | 从 NTP 服务器获取 UTC+8 时间 |
| 文件系统 | NVS (Non-Volatile Storage) | Wi-Fi 凭据、固件版本号持久化存储 |
| 图形界面 | LVGL v9.2.2 | 10 个页面的渲染、动画、事件处理 |
| UI 设计工具 | SquareLine Studio 1.6.1 | 可视化拖拽生成 LVGL 界面代码 |
| 显示屏驱动 | esp_lcd_ili9341 | ILI9341 SPI 屏幕初始化与数据传输 |
| 触摸驱动 | esp_lcd_touch_xpt2046 | XPT2046 电阻触摸坐标读取与映射 |
| 串口通信 | UART1 (esp_driver_uart) | 与 STM32 双向二进制协议通信 |
| OTA 升级 | esp_https_ota | OneNET OTA 固件下载、校验、刷写 |
| 加密/认证 | mbedtls (HMAC-SHA256, Base64) | OneNET API 鉴权 token 生成 |
| JSON 解析 | cJSON | OTA 接口响应解析、MQTT 数据组织 |

#### STM32F103C8T6 侧

| 层级 | 技术/组件 | 用途 |
|------|-----------|------|
| 构建系统 | PlatformIO (GCC ARM) | 编译、烧录、串口监控 |
| 硬件抽象层 | STM32Cube HAL | GPIO、ADC、DMA、UART、I2C 硬件驱动 |
| 实时操作系统 | FreeRTOS (CMSIS-OS v2 封装) | 任务调度、消息队列、线程标志 |
| ADC 采集 | ADC1 + DMA 循环模式 | 分贝传感器连续采样（50 点双缓冲） |
| 精确定时 | DWT (Data Watchpoint) CYCCNT | 微秒级延时（DHT22 时序要求） |
| 单总线协议 | GPIO 位操作 + 关中断 | DHT22 温湿度传感器通信 |
| 串口通信 | USART1 (中断接收) | 与 ESP32-S3 双向二进制协议通信 |
| 调试显示 | OLED (I2C, SSD1306) | 本地实时显示传感器原始值和状态 |

#### FreeRTOS 任务列表（ESP32-S3 侧）

| 任务名称 | 优先级 | 栈大小 | 职责 |
|---------|--------|--------|------|
| `uart_event_task` | configMAX-1 | 4096B | 串口接收 + 协议帧状态机逐字节解析 |
| `uart_tx_task` | configMAX-2 | 3072B | 协议帧构建 + 串口发送 |
| `main_task` | 10 | 4096B | 协议消息分发（命令 → 业务队列） |
| `lvgl_port_task` | 5 | 8192B | LVGL 渲染循环 + 空闲锁屏轮询 |
| `dB_task` | 10 | 2048B | 分贝数据接收与缓存更新 |
| `temp_task` | 10 | 2048B | 温湿度数据解析与缓存更新 |
| `mqtt_report_task` | 5 | 4096B | 定时 + 按需 MQTT 属性上报 |
| `wifi_scan_task` | 5 | 4096B | Wi-Fi 扫描（按需创建，完成后自删） |
| `ota_task` | 5 | 8192B | OTA 固件下载与刷写（按需创建） |

#### FreeRTOS 任务列表（STM32F103C8T6 侧）

| 任务名称 | 优先级 | 栈大小 | 职责 |
|---------|--------|--------|------|
| `UART_RX_TASK` | osPriorityHigh | 512B | 串口中断接收 + 协议帧状态机解析 |
| `UART_TX_TASK` | osPriorityAboveNormal | 512B | 协议帧构建 + 串口发送 |
| `MainTask` | osPriorityAboveNormal | 512B | 命令解析（请求帧 → 传感器控制） |
| `Decibel` | osPriorityAboveNormal | 512B | ADC DMA 数据处理 + 滤波 + 上报 |
| `DHT22Task` | osPriorityAboveNormal | 512B | DHT22 读取 + 温湿度解析 + 上报 |

> **关于 API 不一致的说明：** ESP32-S3 侧使用 FreeRTOS 原生 API（`xQueueCreate`、`xTaskCreate`），因为 ESP-IDF 本身就是基于 FreeRTOS 构建的。STM32 侧使用 CMSIS-OS v2 封装（`osMessageQueueNew`、`osThreadNew`），因为 STM32CubeMX 默认生成这个风格。两者底层都是 FreeRTOS，行为一致，只是 API 名称不同。

---

## 硬件连接与引脚映射

### ESP32-S3 引脚分配

#### LCD 屏 + 触摸（共用 SPI2 总线）

| 引脚功能 | GPIO | 说明 |
|---------|------|------|
| SCLK (SPI 时钟) | 6 | SPI2 总线时钟，LCD 与触摸共用 |
| MOSI (主机输出) | 7 | SPI2 数据输出 |
| MISO (主机输入) | 8 | SPI2 数据输入 |
| DC (数据/命令) | 5 | LCD 专用：高电平=数据，低电平=命令 |
| CS (LCD 片选) | 4 | LCD 专用片选 |
| RST (LCD 复位) | 9 | LCD 专用复位，低电平有效 |
| BK_LIGHT (背光) | 10 | 高电平点亮 |
| CS (触摸片选) | 11 | 触摸专用片选，与 LCD 共享 SPI 总线 |

#### UART1（与 STM32 通信）

| 引脚功能 | GPIO | 方向 |
|---------|------|------|
| TX | 17 | ESP32 → STM32 |
| RX | 16 | STM32 → ESP32 |

### STM32F103C8T6 引脚分配

| 外设 | 引脚 | 功能 |
|------|------|------|
| USART1 TX | PA9 | STM32 → ESP32 |
| USART1 RX | PA10 | ESP32 → STM32 |
| DHT22 数据 | PB0 | 单总线双向通信，需外部上拉电阻 |
| 分贝模拟输入 | PB1 | ADC1_IN9，模拟电压采集 |
| OLED SCL | PB6 | I2C1 时钟 |
| OLED SDA | PB7 | I2C1 数据 |

### 板间连接

```text
ESP32-S3              STM32F103C8T6
  GPIO17 (TX) ─────────► PA10 (RX)
  GPIO16 (RX) ◄───────── PA9  (TX)
  GND        ─────────── GND   (必须共地！)
```

> **为什么要共地？** UART 通信依赖电压差来判断逻辑电平（0/1）。如果两块板子没有共同的参考地，接收方读到的电压是发送方电压 + 两地之间的浮动电压差，导致逻辑判断错误，通信失败。

---

## 通信协议

### 为什么用自定义二进制协议？

| 方案 | 问题 |
|------|------|
| AT 指令 | 文本协议解析成本高，每个命令都需要字符串匹配，不适合高频数据传输 |
| Modbus | 标准协议但帧开销大，对于只有几个传感器的小系统来说过于复杂 |
| 自定义二进制 | 结构紧凑（最小帧仅 12 字节）、解析高效（状态机一次遍历）、扩展灵活（新增命令只需增加枚举值） |

### 协议帧格式

```
Byte Offset:  0    1    2    3    4    5    6    7    8    9    10+N  12+N 13+N
            ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬─────┬─────┬─────┬─────┐
            │0x55│0xAA│ Ver│Type│ Cmd│SeqL│SeqH│LenL│LenH│ Pay │CkSum│0x0D │0x0A │
            │ SOF    │ 1B │ 1B │ 1B │ 2B(Little)│ 2B(Little)│0~128│ 2B  │ EOF    │
            └────┴────┴────┴────┴────┴────┴────┴────┴────┴─────┴─────┴─────┴─────┘
最小帧长: 12 字节 (PayloadLen=0)
最大帧长: 140 字节 (PayloadLen=128)
```

多字节字段（Seq、PayloadLen、Checksum）使用**小端序**（Little-Endian），与 Cortex-M 和 Xtensa 处理器原生字节序一致。

### 字段说明

| 字段 | 偏移 | 长度 | 说明 |
|------|------|------|------|
| SOF | 0 | 2B | 帧头 `0x55 0xAA`，用于帧同步 |
| Version | 2 | 1B | 协议版本号，当前为 `0x01`（预留扩展） |
| MsgType | 3 | 1B | 0=请求(Request), 1=响应(Response), 2=上报(Report) |
| Cmd | 4 | 1B | 命令码（见下方命令定义） |
| Seq | 5 | 2B | 序列号，每发一条消息自增（用于请求-响应匹配，预留） |
| PayloadLen | 7 | 2B | 载荷字节数，0~128 |
| Payload | 9 | N | 实际数据 |
| Checksum | 9+N | 2B | 累加校验和（覆盖 Ver + Type + Cmd + Seq + Len + Payload） |
| EOF | 11+N | 2B | 帧尾 `0x0D 0x0A`，用于帧结束确认 |

### 校验和算法

```c
checksum = Ver + MsgType + Cmd + Seq_L + Seq_H + Len_L + Len_H + sum(Payload[0..N-1])
// 累加溢出自动截断为 uint16_t
```

这是**累加和校验**，不是 CRC。优点：计算极快，不需要查表；缺点：检测能力不如 CRC（无法检测字节顺序交换）。对于板间短距离 UART 通信（硬件已做奇偶校验和帧错误检测），累加和足够使用。

### 命令定义

| 命令码 | 枚举名 | 用途 | 数据方向 |
|--------|--------|------|---------|
| `0xAA` | CMD_TEST | 通信测试，Payload 通常为空 | 双向 |
| `0x01` | CMD_TEMPERATURE | 温湿度传感器控制与数据 | 双向（请求=控制, 响应=温湿度数据） |
| `0x02` | CMD_DB | 分贝模块控制与数据 | 双向（请求=控制, 响应/上报=分贝数据） |
| `0x03` | CMD_LIGHT | 光照传感器（已预留，待实现） | 预留 |

### 状态机解析流程

协议解析的核心是一个 14 状态的状态机，逐字节驱动。以 ESP32 端为例：

```
RX_WAIT_SOF1 ──[0x55]──► RX_WAIT_SOF2 ──[0xAA]──► RX_READ_VER
                                                      │
   [任意状态收到意外字节时回退到 RX_WAIT_SOF1]            │
                                                      ▼
RX_READ_EOF2 ◄── RX_READ_EOF1 ◄── RX_READ_CRC_H ◄── RX_READ_CRC_L
    │                                                      ▲
    │ [0x0A]                                               │
    ▼                                                      │
 校验通过 ──► main_queue ──► main_task 分发 ──► temp_queue / dB_queue
 校验失败 ──► 丢弃，打印 WARNING ──► RX_WAIT_SOF1
```

**关键设计决策：**
- ESP32 使用事件驱动批量读取（`uart_read_bytes` 一次最多 128 字节后逐字节喂给状态机），比逐字节中断效率高
- STM32 使用中断逐字节接收后推入队列，延迟更低但吞吐稍低。对于本项目的数据量级两种方式都绰绰有余
- 不完整帧自动丢弃，不会累积错误（无"试图修复"的逻辑，简单可靠）
- `RX_READ_LEN_H` 后立即检查 `payload_len > 128`，防止恶意帧导致缓冲区溢出

### ESP32 端主任务分发逻辑

`main_task` 从 `main_queue` 收到解析完成的帧后的路由决策：

```
if msg_type == REQUEST  → 忽略（ESP32 不处理请求，那是 STM32 的职责）
if cmd == TEMPERATURE   → 转到 temp_queue（温湿度任务消费 4 字节二进制数据）
if msg_type == RESPONSE && cmd == DB → atof(payload) → 转到 dB_queue
if msg_type == REPORT   && cmd == DB → atof(payload) → 转到 dB_queue
```

这里隐含了一个约定：**温湿度数据通过 cmd 路由，分贝数据通过 msg_type + cmd 双重过滤后以 float 形式路由**。这是因为 STM32 的温湿度 payload 是 4 字节二进制数据（int16_t × 2），不能直接 `atof()`。

---

## 功能模块详解

### 1. Wi-Fi 联网（my_wifi）

ESP32-S3 以 STA 模式连接路由器，支持三套配置来源：

1. **menuconfig 默认配置** — 编译时预设 SSID 和密码（Kconfig 配置项 `CONFIG_MY_WIFI_SSID` / `CONFIG_MY_WIFI_PASSWORD`）
2. **NVS 持久化存储** — 手动连接成功后自动保存凭据，下次启动优先使用
3. **触摸屏手动选择** — 在 Wi-Fi 设置界面扫描附近 AP，选择后输入密码连接

#### 事件驱动架构

ESP32 的 Wi-Fi 和 IP 是两个独立的事件源，必须分开注册：

```c
esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, ...);
esp_event_handler_instance_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &ip_event_handler, ...);
```

关键事件流：

```
esp_wifi_start() → WIFI_EVENT_STA_START → [有凭据] → esp_wifi_connect()
    ↓
WIFI_EVENT_STA_CONNECTED → DHCP → IP_EVENT_STA_GOT_IP → 触发 MQTT/SNTP
    ↓ (断线)
WIFI_EVENT_STA_DISCONNECTED → [自动模式] → esp_wifi_connect() 重试（最多5次）
                            → [手动断开] → 不做任何事，等待用户操作
```

#### 状态管理

```c
static uint8_t s_retry_num = 0;          // 当前重试次数（连接成功后清零）
static bool s_user_disconnect = false;    // 用户主动断开标志
static bool s_manual_connecting = false;  // 手动连接正在执行标志
```

三个变量的协作用例：
- **正常自动连接**：`s_manual_connecting=false` → WIFI_EVENT_STA_START 时自动连接
- **手动连接**：`s_manual_connecting=true` → 阻止自动连接逻辑（避免重复），完成后重置
- **手动断开**：`s_user_disconnect=true` → 断线事件中识别这是用户主动行为，跳过重连

#### Wi-Fi 凭据持久化

连接成功后自动写入 NVS：`wifi_ssid` / `wifi_pass`。下次启动时优先从 NVS 读取，menuconfig 中的默认凭据作为兜底。

#### Wi-Fi 扫描功能（wifi_scan）

- 在独立 FreeRTOS 任务中执行同步扫描（可阻塞），避免阻塞 UI 线程
- 扫描结果通过 `lv_async_call` 异步推送到 LVGL 下拉框更新
- 任务完成后自删除（`vTaskDelete(NULL)`），避免资源浪费

#### 已解决问题：WPA3 兼容性

代码中保留了 `EXAMPLE_H2E_IDENTIFIER` 的定义但注释掉了。实际测试中发现设置 H2E 标识符后连接失败。最终方案是兼容 WPA/WPA2/WPA3 混合模式（`WIFI_AUTH_WPA3_PSK` 作为阈值，实际协商时降级到路由器支持的最高安全等级）。

---

### 2. MQTT 云端通信（my_mqtt）

#### OneNET 平台 MQTT 接入规范

OneNET 的 MQTT 接入与标准 MQTT 的区别主要在**认证方式**和**主题命名规则**：

| 参数 | OneNET 要求 | 本项目配置 |
|------|-----------|-----------|
| clientId | 必须等于设备名称 | `ONENET_DEVICE_NAME` |
| username | 必须等于产品 ID | `ONENET_PRODUCT_ID` |
| password | 设备 Key 生成的 Token | `ONENET_TOKEN` (menuconfig 配置) |
| 属性上报 topic | `$sys/{pid}/{dev}/thing/property/post` | 固定公式拼接 |
| 命令下发 topic | `$sys/{pid}/{dev}/cmd/#` | 通配订阅所有命令 |

#### 自动订阅的主题

| 主题 | 用途 | 当前状态 |
|------|------|---------|
| `$sys/{pid}/{dev}/cmd/#` | 云端命令下发 | 已订阅，处理逻辑待补完 |
| `$sys/{pid}/{dev}/thing/property/post/reply` | 属性上报回执 | 已订阅，观察平台交互结果 |
| `$sys/{pid}/{dev}/thing/property/set` | 属性设置 | 已订阅，处理逻辑待补完 |

#### 属性上报数据管理（mqtt_report）

采用注册表模式管理上报项：

```c
static mqtt_report_item_t s_items[16] = {
    {.key = "connect_status", .type = MQTT_REPORT_BOOL},  // 连接状态
    {.key = "dB_value",      .type = MQTT_REPORT_FLOAT},  // 分贝值
    {.key = "temp_value",    .type = MQTT_REPORT_FLOAT},  // 温度值
    {.key = "humi_value",    .type = MQTT_REPORT_FLOAT},  // 湿度值
};
```

- 每个 item 有独立的 `valid` 标志位，未获取到数据时不会上报
- 写操作通过 `mqtt_report_set_float/bool/int` 统一接口，内部用 FreeRTOS 互斥锁保护
- 后续新增传感器只需在数组中追加条目，无需修改上报逻辑

#### 上报的 JSON 格式

```json
{
  "id": "1234567890",
  "version": "1.0",
  "params": {
    "connect_status": {"value": true},
    "dB_value": {"value": 45.20},
    "temp_value": {"value": 26.30},
    "humi_value": {"value": 65.80}
  }
}
```

JSON 使用 `snprintf` 逐字段拼接（而非 cJSON）。这是有意识的选择：MQTT 上报是高频操作（数秒一次），JSON 结构简单且固定，`snprintf` 拼接比 cJSON 创建对象/序列化快很多。OTA 模块因为 API 响应 JSON 结构复杂且会变化，则使用了 cJSON 解析。

#### 上报调度机制

双重触发策略：

1. **按需触发**：传感器数据更新后调用 `mqtt_report_request_publish()` → 任务通知 → 立即上报
2. **周期兜底**：`mqtt_report_task` 的 `ulTaskNotifyTake` 有 30 秒超时，即使没有按需触发也会定期上报

既保证了数据变化的实时性，又有心跳保活的作用。

---

### 3. LVGL 触摸屏界面（my_screen + my_ui）

#### 硬件配置

- **显示屏**：2.8 英寸 ILI9341，240×320 分辨率，SPI 接口（SPI2_HOST），16 位色深 RGB565
- **触摸**：XPT2046 电阻触摸控制器，共用 SPI 总线，独立片选（GPIO11）
- **刷新策略**：局部刷新（40 行缓冲 × 2 = 双缓冲），DMA 传输
- **像素时钟**：20MHz

#### 初始化流程

```
1.  GPIO 背光引脚配置
2.  SPI 总线初始化（SPI2_HOST, 20MHz, DMA_CH_AUTO）
3.  LCD 面板 IO 创建（绑定 DC/CS 到 SPI）
4.  ILI9341 驱动初始化（复位 → 配置 → 开显示 + 水平镜像）
5.  背光开启
6.  LVGL 初始化（创建 display → 分配 DMA 双缓冲 → RGB565 颜色格式 → 注册刷新回调）
7.  LVGL tick 定时器启动（esp_timer, 2ms 周期）
8.  IO 事件回调注册（帧传输完成 → notify_lvgl_flush_ready）
9.  触摸初始化（独立 SPI IO + XPT2046 驱动 + 坐标映射）
10. LVGL 任务创建（8192B 栈, 优先级 5）
11. UI 初始化（ui_init） + 空闲锁屏初始化（5 分钟超时）
```

#### 双缓冲 + 局部刷新

```c
void *buf1 = spi_bus_dma_memory_alloc(HSPI_HOST, fb_size, 0);
void *buf2 = spi_bus_dma_memory_alloc(HSPI_HOST, fb_size, 0);
lv_display_set_buffers(disp, buf1, buf2, fb_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
```

- 每个缓冲区 240×40 像素 × 2 字节 = 19.2KB，两块共 38.4KB
- 全屏需要 240×320×2 = 150KB，局部刷新将内存需求降低了 75%
- 刷新流程：GPU 渲染到 buf1 → DMA 传输 buf1 到屏幕 → 同时 GPU 渲染下一块到 buf2

#### 屏幕旋转

`lcd_port_update_callback` 在每次 LVGL 刷新前检查旋转设置并同步 LCD 硬件参数。LVGL 设置旋转后只更新自己的内部坐标映射，**不会自动修改** LCD 硬件的 mirror 和 swap_xy 参数，必须在这个回调中手动同步。

#### 触摸坐标校准

XPT2046 返回的原始坐标范围经实测约为 X: 10~225, Y: 10~310，通过线性映射转换到屏幕坐标 0~239 和 0~319，并带有边界保护。

#### 界面结构

```
主界面 (main01) ──左右滑──► main02 ──► main03 ──► main04
    │                    （更多传感器数据页，待扩展）
    │ 点击时钟区域
    ├──► 时间详情页 ── 年月日/时分秒/星期 + SNTP 同步控制
    │ 点击分贝区域
    ├──► 分贝详情页 ── 实时分贝大字体数值
    │ 点击温度区域
    ├──► 温湿度详情页 ── 温度 + 湿度大字体显示
    │ 点击光照区域
    ├──► 光照详情页 ── 预留页，等待 CMD_LIGHT 实现
    │ 下滑手势
    └──► 设置页面 ── Wi-Fi 开关 + OTA 检查入口
               │
               ├──► Wi-Fi 设置页 ── AP 扫描列表 + 密码输入键盘 + 连接按钮
               └──► OTA 详情页 ── 当前版本 + 检查更新按钮 + 状态提示

空闲 5 分钟 ──► 锁屏页 ── 点击任意位置解锁
```

#### 完整界面清单

| 界面 | 生成文件 | 业务逻辑文件 | 触发方式 |
|------|---------|------------|---------|
| 主界面 1 | `ui_main01.c` | `detail_time_logic.c` / `detail_dB_logic.c` / `detail_temp_logic.c` | 启动默认 |
| 主界面 2-4 | `ui_main02~04.c` | — | 左右滑动 |
| 时间详情 | `ui_detailTime.c` | `detail_time_logic.c` | 点击时钟区域 |
| 温湿度详情 | `ui_detailTemperature.c` | `detail_temp_logic.c` | 点击温度区域 |
| 分贝详情 | `ui_detialDB.c` | `detail_dB_logic.c` | 点击分贝区域 |
| 光照详情 | `ui_detialLight.c` | —（预留） | 点击光照区域 |
| 设置 | `ui_setting.c` | — | 下滑手势 |
| Wi-Fi 设置 | `ui_WIFIsetting.c` | `wifi_scan.c` | 点击 WiFi 标签 |
| OTA 详情 | `ui_detailOTA.c` | `my_ota.c` | 点击 OTA 标签 |
| 锁屏 | `ui_ScreenLock.c` | `screen_idle_lock.c` | 5 分钟无操作 |

#### 线程安全

- LVGL 不是线程安全的，所有 LVGL API 调用必须持有 `lvgl_api_lock` 互斥锁
- 跨任务更新 UI 使用 `lv_async_call`（Wi-Fi 扫描结果更新下拉框）或 LVGL 定时器（传感器数据每秒刷新）
- 触摸坐标采集在 LCD 刷新回调中，天然在 LVGL 线程内

#### SquareLine Studio 工作流

所有 `ui_*.c` 文件由 SquareLine Studio 1.6.1 生成（LVGL 9.2.2），遵循以下约定：

- 每个界面一对函数：`ui_XXX_screen_init()` 创建所有对象，`ui_XXX_screen_destroy()` 删除并置 NULL
- 事件回调命名：`ui_event_XXX()`
- 跨界面切换使用 `_ui_screen_change()` 辅助函数
- **不要直接修改生成的 `ui_*.c` 文件**，业务逻辑写在 `detail_*_logic.c` 中

---

### 4. 时间同步（detail_time_logic）

#### SNTP 初始化参数

```c
setenv("TZ", "CST-8", 1);               // 中国标准时间 UTC+8
esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0, "pool.ntp.org");    // 国际 NTP 池
esp_sntp_setservername(1, "ntp.ntsc.ac.cn");  // 中科院国家授时中心
sntp_set_sync_interval(3600000);              // 每小时同步一次
```

#### 时间更新链路

```
SNTP 同步成功 → time_sync_notification_cb (打印日志确认)
       ↓
系统时间更新 (settimeofday)
       ↓
lv_timer(1000ms) → time_update_callback()
       ↓
localtime(&now) → strftime → lv_label_set_text (各 UI 标签)
```

- 两个独立定时器：SNTP 同步（1 小时周期，系统级），LVGL 显示更新（1 秒周期，界面级）
- 时间在两个位置显示：主界面顶部 + 时间详情页

---

### 5. 分贝检测（db.c + detail_dB_logic）

#### 采集链路

```
PB1(AIN9) → ADC1 → DMA(50点循环双缓冲) → 半满/全满回调
  → osThreadFlagsSet → 任务被唤醒
    → 半缓冲区求平均 → 移动平均滤波(5点) → 指数平滑(α=0.2)
      → ADC值线性映射到0~180dB → 响应帧回传 → ESP32 dB_task
        → 更新缓存 + 触发 MQTT 上报
          → LVGL 定时器(100ms) → 更新 ui_dBNum 标签
```

#### DMA 双缓冲机制

```c
#define ADC_BUF_SIZE 50
uint16_t adc_dma_buf[50];  // DMA 循环填充
```

- **半满中断**：前 25 个点就绪 → 任务处理 `buf[0..24]`
- **全满中断**：后 25 个点就绪 → 任务处理 `buf[25..49]`

CPU 处理前半部分时，DMA 继续填充后半部分，不会丢失任何采样点。

#### 滤波算法详解

**第一层 — 移动平均（窗口=5）：**

```c
filter_buf[idx] = adc_value;
idx = (idx + 1) % 5;
output = sum(filter_buf) / 5;
```

作用：消除 ADC 采样的随机高频毛刺。窗口 5 是经验值 — 太大会导致响应迟钝，太小滤波效果不明显。

**第二层 — 指数平滑（α=0.2）：**

```c
db_smooth = db_smooth * 0.8 + current_value * 0.2;
```

作用：让显示值"追赶"真实值而不是跳变。α=0.2 意味着每次更新只采纳 20% 的新值，保留 80% 的历史值。

#### ADC 值到分贝值的线性映射

```c
ratio = (adc - 200) / (3000 - 200);  // [200, 3000] → [0, 1]
dB = 0 + (180 - 0) * ratio;           // [0, 1] → [0, 180]
```

> **注意：** 这是线性映射产生的"伪分贝值"，绝对值不准确，但相对变化能反映声音大小变化趋势。如果需要真实 dB SPL 值，需要用标准声源做多点校准。

---

### 6. 温湿度检测（temperature.c + detail_temp_logic）

#### 采集链路

```
PB0 → DHT22 单总线协议
  → 发送起始信号(18ms 低电平 + 40μs 释放)
    → 等待 DHT22 应答(三段时序检查)
      → 逐位读取 40bit 原始数据(5 字节)
        → 校验和验证(Byte0+1+2+3 == Byte4)
          → 解析温度(int16_t) + 湿度(uint16_t) → 组装 4 字节 payload
            → 响应帧回传 → ESP32 temp_task → 更新缓存 + 触发 MQTT
```

#### DHT22 单总线协议时序

```
主机发送起始信号:
  ├── 拉低 18ms (唤醒传感器)
  └── 拉高 40μs (释放总线)

传感器应答:
  ├── 拉低 80μs
  └── 拉高 80μs

传感器发送 40 位数据 (5 字节, 高位先出):
  每个 bit:
    ├── 拉低 50μs (起始)
    └── 拉高 26~28μs → 逻辑 0
    └── 拉高 70μs    → 逻辑 1

数据格式:
  Byte0: 湿度整数   Byte1: 湿度小数
  Byte2: 温度整数 (Bit15=1 表示负温)   Byte3: 温度小数
  Byte4: 校验和 = (Byte0+1+2+3) & 0xFF
```

#### 微秒级精确定时

使用 DWT (Data Watchpoint and Trace) CYCCNT 寄存器：

```c
// 初始化
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
g_us_ticks_per_loop = SystemCoreClock / 1000000;  // 72MHz → 72 ticks/μs

// 微秒延时
void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * g_us_ticks_per_loop;
    while ((DWT->CYCCNT - start) < ticks);
}
```

DWT 是 Cortex-M3/M4 内置的调试单元，CYCCNT 以 CPU 主频递增。相比 `HAL_Delay`（依赖 SysTick, 最小精度 1ms），DWT 可以提供真正的微秒级精度。

#### 关中断保证时序原子性

```c
__disable_irq();
// ... DHT22 40 bit 读取过程 ...
__enable_irq();
```

DHT22 的 bit 判断依赖高电平持续时间的精确测量（26μs vs 70μs 的差别只有 44μs）。如果在读取过程中被 FreeRTOS 任务切换打断，可能导致时序误判。关中断保证了 40 bit 读取的原子性。整个读过程约 5ms，对系统实时性影响有限。

#### 数据传输格式（二进制定长）

STM32 采集到温湿度后，按以下格式打包为 4 字节：

```c
payload[0] = temp10 & 0xFF;        // 温度×10 的低字节 (int16_t, 支持负值)
payload[1] = (temp10 >> 8) & 0xFF; // 温度×10 的高字节
payload[2] = hum10 & 0xFF;         // 湿度×10 的低字节 (uint16_t)
payload[3] = (hum10 >> 8) & 0xFF;  // 湿度×10 的高字节
```

ESP32 端解析：

```c
int16_t temp_raw = payload[0] | (payload[1] << 8);
float temp = temp_raw / 10.0f;
```

与字符串传输相比（"26.3,65.8" 占 11 字节），二进制格式压缩到 4 字节，节省 64% 带宽，且解析无需 `sscanf`。

---

### 7. NVS 持久化存储（my_nvs）

#### 初始化流程

```c
nvs_flash_init();  // 首次调用初始化 NVS 子系统
// 如果 Flash 被擦除过或分区表变更
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();  // ⚠️ 清除所有 NVS 数据
    nvs_flash_init();
}
nvs_open("storage", NVS_READWRITE, &handle);
```

#### 当前存储项

| Key | 类型 | 写入方 | 写入时机 |
|-----|------|--------|---------|
| `wifi_ssid` | string | `wifi_connect()` | 手动连接 Wi-Fi 成功后 |
| `wifi_pass` | string | `wifi_connect()` | 手动连接 Wi-Fi 成功后 |
| `app_version` | string | `get_app_verion()` | OTA 模块读取，需要外部写入 |

#### 封装层的价值

直接使用 ESP-IDF 的 NVS API 需要管理 `nvs_handle_t` 句柄、每次读写传入句柄。封装后统一管理全局句柄和错误处理，未来如果切换存储方式（如 SPIFFS 或 LittleFS），只需改 `my_nvs` 内部实现。

---

### 8. OTA 固件升级（my_ota）

#### 升级流程

```
1. 上报版本 → POST /fuse-ota/{pid}/{dev}/version
       Body: {"s_version":"V1.0", "f_version":"V1.0"}
2. 查询任务 → GET  /fuse-ota/{pid}/{dev}/check?type=...&version=...
       响应包含 target(目标版本) 和 tid(任务ID)
3. 下载固件 → HTTPS GET, esp_https_ota_perform() 分块下载
4. 校验固件 → 比对版本号(相同则拒绝) + 校验固件签名
5. 刷入Flash → 自动写入 OTA 分区
6. 上报状态 → POST /fuse-ota/{pid}/{dev}/{tid}/status
       Body: {"step": 50} 或 {"step": 100}
7. 重启    → esp_restart()
```

#### Token 鉴权机制

API 调用需要 Authorization 头，签名算法：

```
签名字符串 = "{过期时间}\n{签名方法}\n{资源路径}\n{API版本}"
Token = HMAC-SHA256(AccessKey, 签名字符串)
```

Token 有效期默认 24 小时（86400 秒），格式为：

```
version=2022-05-01&res=products%2F{pid}&et={timestamp}&method=sha256&sign={base64_hmac}
```

#### 安全机制

- 版本相同拒绝升级（防止无限循环）
- 固件签名由 esp_https_ota 内部验证
- 下载中断（网络断开等）→ `esp_https_ota_abort()` 清理，不影响现有固件
- 写入过程中断电 → OTA 分区标记为无效，bootloader 回退到原固件

#### 当前状态

底层的 token 生成、版本上报、任务查询、状态上报、固件下载烧写功能均已实现。自动触发流程（查询到任务后提示用户并启动下载）待补完。

---

## 代码结构

```text
environmental_monitoring/
├── ESP/                                  # ESP32-S3 工程 (ESP-IDF)
│   ├── CMakeLists.txt                    # 顶层 CMake
│   ├── main/
│   │   ├── CMakeLists.txt                # 主组件注册（依赖所有子组件）
│   │   ├── main.c                        # app_main 入口
│   │   └── components/
│   │       ├── my_serial/                # 串口协议收发
│   │       │   ├── inc/
│   │       │   │   ├── my_serial.h       # 协议帧定义 + 公共接口
│   │       │   │   └── main_task.h       # 主任务队列接口
│   │       │   └── src/
│   │       │       ├── my_serial.c       # UART 驱动 + 协议收发 + 状态机
│   │       │       └── mian_task.c       # 协议消息分发（文件名待修正）
│   │       ├── my_wifi/                  # Wi-Fi 连接管理
│   │       │   ├── inc/
│   │       │   │   ├── my_wifi.h
│   │       │   │   └── wifi_scan.h
│   │       │   └── src/
│   │       │       ├── my_wifi.c         # STA 模式 + 连接/断开/重连
│   │       │       └── wifi_scan.c       # AP 扫描 + 下拉框更新
│   │       ├── my_mqtt/                  # MQTT 云端通信
│   │       │   ├── Kconfig               # menuconfig 配置项
│   │       │   ├── inc/
│   │       │   │   ├── my_mqtt.h
│   │       │   │   └── mqtt_report.h     # 属性上报注册表
│   │       │   └── src/
│   │       │       ├── my_mqtt.c         # MQTT 客户端 + 事件处理 + JSON 上报
│   │       │       └── mqtt_report.c     # 上报数据读写管理
│   │       ├── my_screen/                # 屏幕驱动 + 业务逻辑
│   │       │   ├── inc/
│   │       │   │   ├── my_screen.h
│   │       │   │   ├── detail_time_logic.h
│   │       │   │   ├── detail_dB_logic.h
│   │       │   │   ├── detail_temp_logic.h
│   │       │   │   └── screen_idle_lock.h
│   │       │   └── src/
│   │       │       ├── my_screen.c       # LCD + 触摸初始化 + LVGL 渲染循环
│   │       │       ├── detail_time_logic.c  # SNTP 时间同步 + 界面更新
│   │       │       ├── detail_dB_logic.c    # 分贝数据接收 + MQTT 触发
│   │       │       ├── detail_temp_logic.c  # 温湿度数据解析 + MQTT 触发
│   │       │       └── screen_idle_lock.c   # 空闲锁屏逻辑
│   │       ├── my_ui/                    # SquareLine 生成的 LVGL 界面
│   │       │   ├── ui.h                 # 界面总入口
│   │       │   ├── components/           # 可复用 UI 组件
│   │       │   ├── images/               # 图片资源
│   │       │   └── screens/              # 10 个界面文件
│   │       │       ├── ui_main01~04.c    # 主界面
│   │       │       ├── ui_detailTime.c   # 时间详情
│   │       │       ├── ui_detailTemperature.c  # 温湿度详情
│   │       │       ├── ui_detialDB.c     # 分贝详情
│   │       │       ├── ui_detialLight.c  # 光照详情（预留）
│   │       │       ├── ui_setting.c      # 设置界面
│   │       │       ├── ui_WIFIsetting.c  # Wi-Fi 设置
│   │       │       ├── ui_detailOTA.c    # OTA 详情
│   │       │       └── ui_ScreenLock.c   # 锁屏界面
│   │       ├── my_nvs/                   # NVS 持久化存储封装
│   │       │   ├── inc/my_nvs.h
│   │       │   └── src/my_nvs.c
│   │       └── my_ota/                   # OTA 固件升级
│   │           ├── inc/my_ota.h
│   │           └── src/my_ota.c          # Token + API + 固件下载
│   └── managed_components/               # ESP-IDF 组件管理器自动下载
│       ├── lvgl__lvgl/                   # LVGL v9.2.2
│       ├── espressif__esp_lcd_ili9341/   # ILI9341 驱动
│       └── atanisoft__esp_lcd_touch_xpt2046/  # XPT2046 触摸驱动
│
├── STM/                                  # STM32F103C8T6 工程 (PlatformIO)
│   ├── platformio.ini
│   ├── Core/
│   │   ├── Inc/
│   │   │   ├── MyInc/
│   │   │   │   ├── mySerial.h           # 协议帧定义 + 公共接口 (与 ESP32 端一致)
│   │   │   │   ├── db.h
│   │   │   │   ├── temperature.h
│   │   │   │   ├── main_task.h
│   │   │   │   └── OLED.h
│   │   │   └── ...                       # STM32CubeMX 生成的外设头文件
│   │   ├── Src/
│   │   │   ├── MySrc/
│   │   │   │   ├── mySerial.c           # USART1 中断接收 + 协议状态机
│   │   │   │   ├── db.c                 # ADC+DMA 分贝采集 + 滤波
│   │   │   │   ├── temperature.c        # DHT22 单总线驱动 + 温湿度上报
│   │   │   │   ├── main_task.c          # 命令解析分发
│   │   │   │   └── OLED.c              # SSD1306 OLED 驱动
│   │   │   ├── main.c                   # STM32CubeMX 生成的主入口
│   │   │   ├── freertos.c               # FreeRTOS 初始化 + 任务创建
│   │   │   └── ...                       # STM32CubeMX 生成的外设驱动
│   │   └── Startup/                     # 启动文件
│   └── Drivers/                          # HAL 库 + CMSIS
│
├── README.md                             # 本文件
└── IMPROVEMENT_PLAN.md                   # 项目改进计划
```

---

## 构建与运行

### ESP32-S3

```bash
cd ESP
idf.py set-target esp32s3
idf.py menuconfig          # 配置 Wi-Fi SSID/密码、MQTT 参数等
idf.py build flash monitor
```

在 `menuconfig` 中需要配置的内容：

- `My WiFi Configuration` → Wi-Fi SSID 和密码
- `My MQTT Configuration` → Broker URL、产品 ID、设备名称、Token
- `My MQTT Configuration` → Access Key（OTA 鉴权需要）

### STM32F103C8T6

```bash
cd STM
pio run                    # 编译
pio run -t upload          # 烧录
pio device monitor         # 串口监控
```

如果使用 STM32CubeIDE，也可以直接导入 STM 工程进行编译和下载。

---

## 已知问题与改进方向

详见 [IMPROVEMENT_PLAN.md](IMPROVEMENT_PLAN.md)，主要包括：

1. **架构解耦** — 组件间存在循环依赖，WiFi 模块直接依赖 MQTT/OTA/屏幕等上层业务
2. **协议优化** — 载荷使用字符串命令（`"DB Init"`），应改为子命令码
3. **状态管理** — 全局变量通过 extern 跨组件暴露，应封装为访问函数
4. **STM32 侧** — 传感器无条件自启动，应改为由 ESP32 通过协议控制
5. **OTA 自动流程** — 底层 API 已就绪，查询→下载→重启的自动流程待补完

---

## 函数调用与运行流程

> 以下使用 `→` 表示同步函数调用，`⇒` 表示跨任务的消息传递（队列/任务通知），`-->>` 表示硬件中断触发。

### 一、系统启动流程

#### ESP32-S3 启动（`app_main` → 各组件就绪）

```
main.c: app_main()
│
├── uart_init()                                    [my_serial.c]
│   ├── uart_param_config()                        ESP-IDF: 配置波特率/数据位/停止位
│   ├── uart_set_pin()                             ESP-IDF: 绑定 GPIO16(RX)/17(TX)
│   ├── uart_driver_install()                      ESP-IDF: 安装驱动 + 创建 uart_rx_queue
│   ├── xQueueCreate() → uart_tx_queue             FreeRTOS: 发送队列(5项)
│   ├── xTaskCreate(uart_rx_task, configMAX-1)     uart_rx_task 开始运行，阻塞在 uart_rx_queue
│   ├── xTaskCreate(uart_tx_task, configMAX-2)     uart_tx_task 开始运行，阻塞在 uart_tx_queue
│   └── uart_send_test()                           发送 CMD_TEST 请求帧到 STM32
│
├── main_task_init()                               [mian_task.c]
│   ├── xQueueCreate() → main_queue                FreeRTOS: 主分发队列(20项)
│   └── xTaskCreate(mian_task, 10)                 main_task 开始运行，阻塞在 main_queue
│
├── my_nvs_init()                                  [my_nvs.c]
│   ├── nvs_flash_init()                           ESP-IDF: 初始化 NVS 子系统
│   └── nvs_open("storage", READWRITE)             打开命名空间 → g_nvs_handle
│
├── wifi_start()                                   [my_wifi.c]
│   └── wifi_init_sta()
│       ├── xEventGroupCreate() → wifi_event_group 同步事件组
│       ├── esp_netif_init()                       TCP/IP 协议栈初始化
│       ├── esp_event_loop_create_default()        默认事件循环
│       ├── esp_netif_create_default_wifi_sta()    创建 STA 网络接口
│       ├── esp_wifi_init()                        Wi-Fi 驱动初始化
│       ├── esp_event_handler_instance_register()  注册 wifi_event_handler + ip_event_handler
│       ├── esp_wifi_set_mode(STA)                 设为 STA 模式
│       ├── esp_wifi_start()                       启动 Wi-Fi 驱动
│       │     └── 触发 WIFI_EVENT_STA_START
│       │           └── wifi_event_handler()
│       │                 └── esp_wifi_connect()    开始连接(若有凭据)
│       └── wifi_connect(SSID, PASS)               或手动连接(若配置了凭据)
│             ├── esp_wifi_set_config()             写入 SSID/密码
│             ├── esp_wifi_start() + esp_wifi_connect()
│             └── xEventGroupWaitBits()             阻塞等待连接结果
│                   ├── 成功 → my_nvs_set_value()  保存凭据到 NVS
│                   └── 失败 → 返回 false
│
└── screen_init()                                  [my_screen.c]
    ├── gpio_config()                              背光引脚(GPIO10)输出
    ├── spi_bus_initialize(SPI2_HOST)              SPI2 总线初始化(20MHz)
    ├── esp_lcd_new_panel_io_spi() → io_handle     LCD IO 句柄(绑定 DC=5, CS=4)
    ├── esp_lcd_new_panel_ili9341() → panel_handle LCD 面板句柄
    ├── esp_lcd_panel_reset() + init()             ILI9341 复位 + 初始化
    ├── esp_lcd_panel_mirror(true, false)          水平镜像(适应安装方向)
    ├── gpio_set_level(背光, ON)                    点亮背光
    ├── lv_init()                                  LVGL 库初始化
    ├── lv_display_create(240, 320)                创建 display 对象
    ├── spi_bus_dma_memory_alloc() × 2             分配双缓冲(各19.2KB)
    ├── lv_display_set_buffers()                   绑定局部刷新双缓冲
    ├── lv_display_set_flush_cb(lcd_flush_cb)      注册刷新回调
    ├── esp_timer_create() → lvgl_tick_timer       创建 2ms 定时器 → lvgl_tick_task()
    │                                                └── lv_tick_inc(2) 驱动 LVGL 时间基准
    ├── esp_lcd_panel_io_register_event_callbacks() 注册帧传输完成回调
    │                                                └── notify_lvgl_flush_ready()
    │                                                      └── lv_display_flush_ready()
    ├── [触摸] esp_lcd_new_panel_io_spi()          触摸 SPI IO(独立 CS=11)
    ├── [触摸] esp_lcd_touch_new_spi_xpt2046()     创建触摸句柄
    ├── [触摸] lv_indev_create() + set_read_cb()   注册触摸读取回调 touch_read_cb()
    ├── xTaskCreate(lvgl_port_task, 5)             LVGL 渲染任务开始运行
    │     └── while(1):
    │           _lock_acquire(&lvgl_api_lock)
    │           lv_timer_handler()                 处理 LVGL 定时器 + 动画 + 重绘
    │           screen_idle_lock_poll()            检查空闲是否超时 → 自动锁屏
    │           _lock_release(&lvgl_api_lock)
    │           usleep(5~500ms)                    释放 CPU
    ├── _lock_acquire(&lvgl_api_lock)              获取 LVGL 锁
    ├── ui_init()                                  遍历所有界面调用 screen_init()
    │     ├── ui_main01_screen_init()              创建主界面所有 LVGL 对象
    │     ├── ui_main02_screen_init()              ...
    │     └── ...                                  但 lv_scr_load 只显示第一个
    └── screen_idle_lock_init(5×60×1000)           初始化空闲锁(5分钟超时)
```

#### STM32F103C8T6 启动（`main` → FreeRTOS 就绪）

```
main.c: main()
├── HAL_Init()
├── SystemClock_Config()                           配置 72MHz, APB1=36MHz, APB2=72MHz
├── MX_GPIO_Init()
├── MX_DMA_Init()                                  DMA1 初始化
├── MX_ADC1_Init()                                 ADC1 初始化(PB1, IN9)
├── MX_I2C1_Init()                                 I2C1 初始化(PB6/PB7, OLED用)
├── MX_USART1_UART_Init()                          USART1 初始化(PA9/PA10, 115200)
├── osKernelInitialize()
└── MX_FREERTOS_Init()                             [freertos.c]
    ├── osThreadNew(StartDefaultTask)              空循环任务(Normal 优先级)
    ├── OLED_Init()                                SSD1306 I2C 显示初始化
    ├── mySerial_RTOS_Init()                       [mySerial.c]
    │   ├── osMessageQueueNew() → uart_rx_queue    接收队列(128字节)
    │   ├── osMessageQueueNew() → uart_tx_queue    发送队列(4项)
    │   ├── osThreadNew(uart_rx_task, High)        uart_rx_task 开始运行
    │   └── osThreadNew(uart_tx_task, AboveNormal) uart_tx_task 开始运行
    ├── mySerial_init()                            发送 "Serial Init OK" 上报帧
    │   └── HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1)  使能 UART 中断接收
    ├── DHT22_RTOS_Init()                          [temperature.c]
    │   ├── DWT 初始化(使能 CYCCNT, 计算 μs/ticks)
    │   └── osThreadNew(DHT22_Task, AboveNormal)   DHT22Task 开始运行
    │         └── while(1):
    │               if(!g_dht22_enabled) osDelay(200); continue  等待使能信号
    │               DHT22_Read() → DHT22_Send_Report()            读取+上报
    │               osDelay(2000)
    ├── DHT22_Init()                                g_dht22_enabled = true (当前无条件启动)
    ├── DB_RTOS_Init()                             [db.c]
    │   └── osThreadNew(StartDecibelTask, AboveNormal)  Decibel 任务开始运行
    │         └── while(1):
    │               osThreadFlagsWait(HALF|FULL)   阻塞等待 DMA 半满/全满通知
    │               求平均 → DB_Filter() → calculate_db() → 指数平滑
    │               msg_Response(CMD_DB, report_str)
    ├── DB_Init()                                   HAL_ADC_Start_DMA() (当前无条件启动)
    └── main_task_RTOS_Init()                      [main_task.c]
        ├── osMessageQueueNew() → main_queue        主命令队列(4项)
        └── osThreadNew(main_task, AboveNormal)     MainTask 开始运行
              └── while(1):
                    osMessageQueueGet(main_queue)   阻塞等待命令
                    if(REQUEST) → 解析子命令 → 控制传感器
```

### 二、Wi-Fi 连接与 MQTT 启动流程

```
用户点击 Wi-Fi 设置界面的连接按钮
│
├── ui_event_WIFIConnect()                         [ui_WIFIsetting.c]
│   ├── lv_dropdown_get_selected_str()             获取选择的 SSID
│   ├── lv_textarea_get_text()                     获取输入的密码
│   └── wifi_connect(ssid, password)               [my_wifi.c]
│         ├── s_user_disconnect = true              标记状态
│         ├── s_manual_connecting = true
│         ├── esp_wifi_disconnect() + esp_wifi_stop() 断开当前连接
│         ├── esp_wifi_set_mode(STA)
│         ├── esp_wifi_set_config()                 写入新 SSID/密码
│         ├── esp_wifi_start()
│         ├── esp_wifi_connect()                    发起连接
│         ├── s_manual_connecting = false
│         └── xEventGroupWaitBits(CONNECTED|FAIL, 无限等待)
│               │
│               ├── [CONNECTED 路径]
│               │     └── my_nvs_set_value("wifi_ssid", ssid)   持久化凭据
│               │         my_nvs_set_value("wifi_pass", password)
│               │
│               └── [FAIL 路径]
│                     └── return false → UI 显示错误
│
Wi-Fi 底层异步事件(在 WiFi 驱动任务中):
│
├── IP_EVENT_STA_GOT_IP
│   └── ip_event_handler()                         [my_wifi.c]
│         ├── s_retry_num = 0                       重置重试计数
│         ├── xEventGroupSetBits(CONNECTED_BIT)     通知 wifi_connect() 解除阻塞
│         └── mqtt_app_start()                      [my_mqtt.c] ← 直接耦合,待改为事件
│               ├── mqtt_report_init()               初始化属性上报注册表 + 互斥锁
│               ├── mqtt_init()
│               │   ├── snprintf() → 拼接 clientId/主题字符串
│               │   ├── esp_mqtt_client_init()       创建 MQTT 客户端
│               │   ├── esp_mqtt_client_register_event()  注册 mqtt_event_handler
│               │   ├── esp_mqtt_client_start()      启动 MQTT 连接
│               │   └── xTaskCreate(mqtt_report_task)  创建周期上报任务
│               │         └── while(1):
│               │               ulTaskNotifyTake(30s超时)  等待通知或超时
│               │               mqtt_publish_all_report()  构建 JSON → mqtt_send_message()
│               │
│               └── [MQTT_EVENT_CONNECTED 回调]
│                     └── mqtt_event_handler()
│                           ├── s_mqtt_connected = true
│                           ├── esp_mqtt_client_subscribe(cmd/#)    订阅命令
│                           ├── esp_mqtt_client_subscribe(post/reply)  订阅回执
│                           ├── esp_mqtt_client_subscribe(property/set)  订阅属性设置
│                           └── mqtt_publish_all_report()  首次全量上报
│
└── [断线重连]
      └── WIFI_EVENT_STA_DISCONNECTED
            └── wifi_event_handler()
                  ├── [自动模式] s_retry_num < 5 → esp_wifi_connect() 重试
                  ├── [手动断开] s_user_disconnect=true → 不重连
                  └── [超过重试] xEventGroupSetBits(FAIL_BIT) + mqtt_app_stop()
```

### 三、分贝数据上行完整流程

```
硬件层 (STM32):
  PB1 → ADC1_IN9 → ADC 采样
    │
    └── DMA1_Channel1 循环搬运(50点)  --硬件自动-->
        │
        ├── [25点完成] --中断--> HAL_ADC_ConvHalfCpltCallback()  [db.c]
        │     └── osThreadFlagsSet(decibelTaskHandle, DB_FLAG_HALF)
        │
        └── [50点完成] --中断--> HAL_ADC_ConvCpltCallback()
              └── osThreadFlagsSet(decibelTaskHandle, DB_FLAG_FULL)
                    │
                    ▼
              StartDecibelTask()  被 osThreadFlagsWait 唤醒  [db.c]
                │
                ├── 确定半区(target_buf = buf[0..24] 或 buf[25..49])
                ├── 半区 25 点求平均 → avg_adc
                ├── DB_Filter(&avg_adc)                    移动平均滤波(窗口5)
                │     └── filter_buf[idx] = avg_adc → sum/5
                ├── calculate_db(filtered)                  线性映射
                │     └── ratio = (adc-200)/(3000-200) → dB = ratio×180
                ├── db_smooth = db_smooth*0.8 + dB*0.2     指数平滑
                ├── snprintf(report_str, "%u.%u", whole, frac)  值→字符串
                └── msg_Response(CMD_DB, report_str, strlen)  [mySerial.c]
                      │
                      ▼
                protocol_send_frame(RESPONSE, CMD_DB, seq, payload, len)
                  ├── 构建帧: SOF + Ver + Type + Cmd + Seq + Len + Payload + Checksum + EOF
                  └── HAL_UART_Transmit(&huart1, frame, idx)  --UART TX→
                                                                      │
── 板间 UART 传输 ───────────────────────────────────────────────────────│──
                                                                      │
                                                                      ▼
ESP32 接收:
  UART1 RX(GPIO16) --硬件接收→                                [my_serial.c]
    │
    └── uart_event_task 收到 UART_DATA 事件
          ├── uart_read_bytes() 批量读取(最多128字节)
          └── 逐字节喂入 14 状态状态机
                │
                ├── RX_WAIT_SOF1 → [0x55] → SOF2 → [0xAA] → READ_VER → ...
                ├── ... → RX_READ_CRC_H → [校验计算]
                ├── [校验通过] → RX_READ_EOF1 → [0x0D] → EOF2 → [0x0A]
                │     │
                │     └── 组装 UartTxItem → xQueueSend(main_queue, &item)
                │                                                      │
                └── [校验失败] → ESP_LOGW → RX_WAIT_SOF1(丢弃重来)       │
                                                                       │
                ▼                                                      │
          main_task (mian_task.c) 被 xQueueReceive 唤醒                │
            │                                                           │
            ├── item.msg_type == RESPONSE && item.cmd == CMD_DB?        │
            │     └── atof(payload) → float dB_value                     │
            │           └── xQueueSend(dB_queue, &dB_value) ──────────┐ │
            │                                                          │ │
            └── item.msg_type == REPORT && item.cmd == CMD_DB?         │ │
                  └── atof(payload) → xQueueSend(dB_queue, ...) ──────┘ │
                                                                        │
                ▼                                                       │
          dB_task (detail_dB_logic.c) 被 xQueueReceive 唤醒            │
            ├── latest_dB_value = dB_value                              │
            ├── latest_dB_valid = true                                  │
            ├── mqtt_report_set_float("dB_value", dB_value)  写上报注册表│
            ├── mqtt_report_set_bool("connect_status", true)            │
            └── mqtt_report_request_publish() ──────────────────────┐   │
                                                                     │   │
                ▼                                                    │   │
          LVGL 定时器(ui_timer_cb, 100ms)  [ui_main01.c]            │   │
            ├── dB_get_latest_valid()?                               │   │
            ├── dB_get_latest_value() → snprintf → lv_label_set_text  │   │
            └── temp_get_latest_valid()? 同样更新温度显示             │   │
                                                                     │   │
                ▼                                                    │   │
          mqtt_report_task 被 xTaskNotifyGive 唤醒  [my_mqtt.c]     │   │
            └── mqtt_publish_all_report()                            │   │
                  ├── mqtt_report_get_all(&count)  读取所有有效注册项  │   │
                  ├── snprintf 逐字段拼接 OneNET JSON 格式             │   │
                  └── mqtt_send_message(post_topic, json)              │   │
                        └── esp_mqtt_client_publish() --TCP→ OneNET 平台
```

### 四、温湿度数据上行完整流程

```
硬件层 (STM32):
  PB0 → DHT22 单总线
    │
    └── DHT22Task (temperature.c) 循环执行:
          ├── if(!g_dht22_enabled) → osDelay(200); continue
          ├── DHT22_Read(&temp, &humi)
          │     ├── DHT22_Send_Start()              发送 18ms 低电平起始信号
          │     ├── __disable_irq()                  关中断! 保证时序原子性
          │     ├── DHT22_Wait_Ack()                 等待三段应答(拉低→拉高→拉低)
          │     ├── DHT22_Read_Byte() × 5            逐字节读取 40bit 数据
          │     │     └── DHT22_Read_Bit() × 8
          │     │           ├── 等低电平结束(起始)
          │     │           └── 测高电平持续时间: >40μs → 1, <40μs → 0
          │     ├── __enable_irq()                   恢复中断
          │     └── DHT22_Parse_Data()               校验和 + 解析温湿度
          │           ├── checksum = (B0+B1+B2+B3) & 0xFF vs B4
          │           ├── humidity = ((B0<<8)|B1) / 10.0f
          │           └── temperature = ((B2<<8)|B3) / 10.0f (支持负温)
          │
          └── DHT22_Send_Report(temp, humi)
                ├── temp10 = (int16_t)(temp * 10)    温湿度 ×10 取整
                ├── hum10 = (uint16_t)(humi * 10)
                ├── payload[0..1] = temp10(little-endian)
                ├── payload[2..3] = hum10(little-endian)
                └── msg_Response(CMD_TEMPERATURE, payload, 4)
                      └── protocol_send_frame() → HAL_UART_Transmit()
                                                              │
── 板间 UART 传输 ──────────────────────────────────────────│──
                                                              │
ESP32 接收:                                                   │
  uart_event_task → 状态机解析 → xQueueSend(main_queue)      │
                                                              │
                ▼                                             │
          main_task (mian_task.c)                             │
            └── item.cmd == CMD_TEMPERATURE?                  │
                  └── xQueueSend(temp_queue, &item) ─────────┐│
                                                              ││
                ▼                                             ││
          temp_task (detail_temp_logic.c)                     ││
            ├── 校验: cmd==TEMPERATURE && payload_len>=4       ││
            ├── temp_raw = payload[0] | (payload[1]<<8)        ││
            ├── humi_raw = payload[2] | (payload[3]<<8)        ││
            ├── temp = temp_raw / 10.0f                        ││
            ├── humi = humi_raw / 10.0f                        ││
            ├── latest_temp_value = temp; latest_temp_valid = true
            ├── latest_humidity_value = humi
            ├── mqtt_report_set_float("temp_value", temp)      ││
            ├── mqtt_report_set_float("humi_value", humi)      ││
            └── mqtt_report_request_publish() ─────────────────┘│
                                                                │
          → LVGL 定时器更新 ui_temperatureNum (同分贝流程)      │
          → mqtt_report_task 被通知 → JSON 上报 OneNET (同分贝流程)
```

### 五、协议帧发送流程（以 ESP32 端发起请求为例）

```
业务层调用:
  msg_Request(CMD_DB, "DB Init", 7)               [my_serial.c]
    │
    ├── 组装 UartTxItem:
    │     .msg_type = MSG_TYPE_REQUEST (0)
    │     .cmd = CMD_DB (0x02)
    │     .seq = seq_num++                         全局序列号自增
    │     .payload = "DB Init"
    │     .payload_len = 7
    │
    └── xQueueSend(uart_tx_queue, &tx_item) ────────┐
                                                     │
                ▼                                    │
          uart_tx_task 被唤醒                         │
            └── protocol_send_frame(...)             │
                  │                                   │
                  ├── 构建帧缓冲区 frame[140]:        │
                  │     [0]=0x55  [1]=0xAA            SOF
                  │     [2]=0x01                       Version
                  │     [3]=0x00                       MSG_TYPE_REQUEST
                  │     [4]=0x02                       CMD_DB
                  │     [5..6]=seq                     Seq (LE)
                  │     [7..8]=7                       PayloadLen (LE)
                  │     [9..15]="DB Init"              Payload
                  │
                  ├── calculate_checksum_fields()      累加校验
                  │     = 0x01 + 0x00 + 0x02 + seq_L + seq_H + 7 + 0 + 'D'+'B'+' '+'I'+'n'+'i'+'t'
                  │
                  ├── frame[N+9..N+10] = checksum     Checksum (LE)
                  ├── frame[N+11]=0x0D [N+12]=0x0A   EOF
                  │
                  └── uart_write_bytes(UART1, frame, total_len)
                        │
                        └── 硬件 TX(GPIO17) → STM32 PA10(RX)
                                             │
── 板间 UART 传输 ─────────────────────────│──
                                             │
STM32 接收:                                  ▼
  USART1 RX 中断 → HAL_UART_RxCpltCallback  [mySerial.c]
    └── osMessageQueuePut(uart_rx_queue, &s_rx_byte)
          │
          ▼
    uart_rx_task 逐字节取出 → 状态机解析(与 ESP32 相同的 14 状态)
          │
          ├── [完整帧校验通过]
          │     └── 组装 UartTxItem → osMessageQueuePut(main_queue, &item)
          │                                        │
          └── [校验失败] → RX_WAIT_SOF1(丢弃)       │
                                                    ▼
                                            main_task (main_task.c)
                                              ├── msg_type == REQUEST?
                                              │     ├── cmd == CMD_DB? → strcmp("DB Init")==0?
                                              │     │     ├── DB_Init()             启动 ADC+DMA
                                              │     │     └── msg_Report("DB Init OK")
                                              │     │           └── protocol_send_frame() → UART TX
                                              │     │
                                              │     └── cmd == CMD_TEMPERATURE?
                                              │           └── strcmp("DHT22 Init")==0?
                                              │                 ├── DHT22_Init()      g_dht22_enabled=true
                                              │                 └── msg_Report("DHT22 Init OK")
                                              │
                                              └── msg_type == RESPONSE/REPORT?
                                                    └── (当前未处理，ESP32 负责消费这些消息)
```

### 六、UI 交互流程（触摸 → 界面切换）

```
硬件触摸 → XPT2046 采集
  │
  └── LVGL 输入设备读取回调:
        touch_read_cb(indev, data)                    [my_screen.c]
          ├── esp_lcd_touch_read_data()               读取触摸原始数据
          ├── esp_lcd_touch_get_data()                获取坐标 + 状态
          ├── [有触摸]:
          │     ├── raw_x,y → 线性映射 → mapped_x,y   坐标校准
          │     ├── data->point = (mapped_x, mapped_y)
          │     ├── data->state = PRESSED
          │     └── screen_idle_lock_mark_activity()  重置空闲计时
          └── [无触摸]:
                └── data->state = RELEASED
                      │
                      ▼
                LVGL 内部处理:
                  ├── lv_indev_read()                 读取输入设备
                  ├── 命中测试(哪个对象被点中)
                  ├── 触发对应的事件回调
                  │
                  ├── [点击 ui_Label1(时间区域)]
                  │     └── ui_event_Label1()         [ui_main01.c]
                  │           └── _ui_screen_change(&ui_detailTime, ...)
                  │                 ├── 如果需要: ui_main01_screen_destroy()
                  │                 ├── ui_detailTime_screen_init()  创建新界面对象
                  │                 │     └── lv_obj_create + lv_label_create 等
                  │                 └── lv_scr_load(ui_detailTime)  切换显示
                  │
                  ├── [点击 ui_WIFI(设置界面)]
                  │     └── ui_event_Label5()         [ui_setting.c]
                  │           ├── _ui_screen_change(&ui_WIFIsetting, ...)
                  │           └── xTaskCreate(wifi_scan_worker_task)  后台扫描
                  │                 └── wifi_scan_worker_task()       [wifi_scan.c]
                  │                       ├── esp_wifi_scan_start()   同步扫描(阻塞)
                  │                       ├── esp_wifi_scan_get_ap_records()
                  │                       ├── 拼接 "SSID1\nSSID2\n..."
                  │                       ├── lv_async_call(wifi_update_dropdown_async, options)
                  │                       │     └── [在 LVGL 线程中回调]
                  │                       │           └── lv_dropdown_set_options(ui_WIFIChoice, options)
                  │                       └── vTaskDelete(NULL)        任务自删除
                  │
                  ├── [点击 OTA 更新按钮]
                  │     └── ui_event_checkUpdate()    [ui_detailOTA.c]
                  │           ├── esp_wifi_sta_get_ap_info()  检查 Wi-Fi 连接状态
                  │           ├── onenet_ota_upload_version() 上报当前版本
                  │           └── [待补完] 查询任务 → 提示用户 → 启动 ota_task()
                  │
                  ├── [左右滑动手势]
                  │     └── LV_EVENT_GESTURE + LV_DIR_LEFT/RIGHT
                  │           └── _ui_screen_change(&ui_main02, OVER_LEFT, ...)
                  │
                  └── [下滑手势 → 设置]
                        └── _ui_screen_change(&ui_setting, FADE_ON, ...)

空闲 5 分钟:
  lvgl_port_task() 每循环:
    └── screen_idle_lock_poll()                      [screen_idle_lock.c]
          ├── s_screen_locked? → return(已锁屏,不检查)
          ├── lv_screen_active() == ScreenLock? → s_screen_locked=true
          ├── lv_tick_elaps(s_last_activity) >= 5min?
          │     └── _ui_screen_change(&ui_ScreenLock, FADE_ON, ...)
          └── (未超时) → return

锁屏后点击任意位置:
  └── touch_read_cb() → screen_idle_lock_mark_activity()  重置计时
        └── screen_idle_lock_poll() 检测到 active screen 不是 ScreenLock
              └── s_screen_locked = false → 恢复正常交互
```

### 七、MQTT 停止与 Wi-Fi 断开流程

```
用户操作:
  ├── [关闭 Wi-Fi 开关]
  │     └── ui_event_WIFISwitch()                    [ui_setting.c]
  │           └── wifi_disconnect()                  [my_wifi.c]
  │                 ├── s_user_disconnect = true
  │                 ├── mqtt_app_stop()               [my_mqtt.c]
  │                 │     ├── s_mqtt_started = false
  │                 │     ├── esp_mqtt_client_stop()  停止客户端
  │                 │     ├── esp_mqtt_client_destroy() 销毁客户端
  │                 │     └── vTaskDelete(mqtt_report_task)  删除上报任务
  │                 ├── esp_wifi_disconnect()         断开 Wi-Fi
  │                 └── xEventGroupClearBits(CONNECTED|FAIL)
  │
  └── [意外断线]
        └── WIFI_EVENT_STA_DISCONNECTED
              └── wifi_event_handler()
                    ├── s_user_disconnect == false?
                    │     └── s_retry_num < 5? → esp_wifi_connect()
                    │           └── s_retry_num++
                    └── s_retry_num >= 5?
                          ├── xEventGroupSetBits(FAIL_BIT)
                          └── mqtt_app_stop()
```

### 八、OTA 固件升级流程（底层 API 已实现）

```
系统启动 Wi-Fi 获取 IP 后:
  └── ip_event_handler() → mqtt_app_start() (MQTT 连接)
        │
        └── [Wi-Fi 获取 IP] → esp_event → event_handler()  [my_ota.c]
              └── ato_start()
                    └── [待补完] 应调用 onenet_ota_check_task() 查询升级任务

用户手动触发 OTA 检查:
  └── ui_event_checkUpdate()                         [ui_detailOTA.c]
        ├── esp_wifi_sta_get_ap_info()                确认 Wi-Fi 已连接
        └── onenet_ota_upload_version()               [my_ota.c]
              ├── get_app_verion()                     获取当前版本(NVS或app_desc)
              │     └── my_nvs_get_value("app_version") → 否则 esp_app_get_description()
              ├── snprintf → version_info JSON
              ├── dev_token_generate()                 生成 HMAC-SHA256 鉴权 token
              │     ├── 解码 AccessKey (Base64 → Binary)
              │     ├── 构建签名字符串: "{et}\n{method}\n{res}\n{version}"
              │     ├── mbedtls_md_hmac(SHA256, key, string) → digest
              │     └── Base64 编码 digest → URL 编码 → token 字符串
              └── onenet_ota_http_connect(POST, url, version_info)
                    ├── esp_http_client_init()         创建 HTTP 客户端
                    ├── esp_http_client_set_header("Authorization", token)
                    ├── esp_http_client_perform()      同步 POST(阻塞)
                    └── cJSON_Parse(data_buff)         解析响应 → 确认 code==0

[待补完]:
  onenet_ota_check_task()                             查询 OTA 任务
    └── → 获取 target_version + task_id
  onenet_ota_upload_status(tid, step=1~100)           上报进度
  xTaskCreate(ota_task)                               创建下载任务
    └── ota_task()                                    [my_ota.c]
          ├── esp_https_ota_begin()                   初始化 OTA
          ├── esp_https_ota_get_img_desc()            读取新固件描述
          ├── validate_image_header()                 版本号比对(相同则拒绝)
          ├── while(1):
          │     └── esp_https_ota_perform()           分块下载(可读取进度)
          ├── esp_https_ota_finish()                  完成 + 校验
          └── esp_restart()                           重启进入新固件
```

---

## 学习总结

### 做得好的方面

- 自定义二进制协议设计合理，两端状态机实现稳定，CRC 校验、长度保护、异常恢复机制完善
- 分贝采集的 DMA 双缓冲 + 两层滤波（移动平均 + 指数平滑）是典型的嵌入式信号处理方案
- DHT22 的 DWT 微秒延时 + 关中断原子操作体现了对硬件时序的深入理解
- 属性上报的注册表模式使新增传感器变得简单
- LVGL 线程安全处理（互斥锁 + lv_async_call + LVGL 定时器）覆盖了全部跨线程访问场景

### 需要改进的方面

- 架构层面缺乏分层设计，底层驱动和上层业务耦合过重
- 日志管理不够精细化（大量 INFO 级别日志实为 DEBUG）
- 错误处理策略不统一（有的 return void，有的 return err，有的 assert）
- 两端协议代码完全重复但缺乏注释说明对应关系

### 最大的收获

这个项目让我真正理解了"嵌入式系统不是独立技术的堆砌，而是将它们有机组合成一条可靠的数据链路"。从传感器输出的微弱模拟信号，经过 ADC 采样、DMA 搬运、滤波处理、串口传输、状态机解析、任务分发、界面渲染，最终变成云端的一个 JSON 字段——这条链路中的每一个环节都有其存在的意义和设计考量。理解了这一点，才算真正入门了嵌入式系统设计。
