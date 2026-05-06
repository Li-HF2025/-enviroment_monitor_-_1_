#ifndef MY_SCREEN_H
#define MY_SCREEN_H
// 标准C库头文件
#include <stdio.h>          // 标准输入输出
#include <unistd.h>         // UNIX标准函数（如usleep）
#include <sys/lock.h>       // 系统锁（用于线程安全）
#include <sys/param.h>      // 系统参数宏（如MAX/MIN）

// ESP-IDF 核心头文件
#include "freertos/FreeRTOS.h"  // FreeRTOS内核
#include "freertos/task.h"      // FreeRTOS任务管理
#include "esp_timer.h"          // ESP定时器（用于LVGL tick）
#include "esp_lcd_panel_io.h"   // LCD面板IO抽象层
#include "esp_lcd_panel_vendor.h" // LCD厂商驱动接口
#include "esp_lcd_panel_ops.h"  // LCD面板操作接口
#include "driver/gpio.h"        // GPIO驱动
#include "driver/spi_master.h"  // SPI主机驱动
#include "esp_err.h"            // ESP错误码处理
#include "esp_log.h"            // 日志输出

// LVGL图形库头文件
#include "lvgl.h"

// ILI9341控制器驱动
#include "esp_lcd_ili9341.h"
   
// XPT2046触摸控制器驱动
#include "esp_lcd_touch_xpt2046.h" 

#include "ui.h"                // 包含LVGL UI定义
#include "ui_helpers.h"        // 包含LVGL UI辅助函数
#include "screen_idle_lock.h"  // 包含屏幕空闲锁相关函数
void screen_init(void);
#endif /* MY_SCREEN_H */