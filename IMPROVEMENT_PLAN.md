# 项目改进计划 — 已完成

> 全部五个阶段已在 2026-07-30 完成实施并推送到 GitHub `main` 分支。

---

## 已完成阶段 ✅

| 阶段 | 内容 |
|------|------|
| 第一阶段 | Bug 修复（通信环路、数据过滤、HAL_Delay、注释/命名修正） |
| 第二阶段 | 架构解耦（WiFi 事件化、wifi_scan 回调、CMake 依赖清理、传感器-MQTT 解耦、extern 消除） |
| 第三阶段 | 数据规范化（SensorDataBin 统一编码、子命令码、malloc 消除） |
| 第四阶段 | STM32 改进（传感器按需启停、任务通知、魔术数字宏、统一序列号） |
| 第五阶段 | 高级特性（sensor_cache 离线缓存+补传、MQTT 下行命令、调试宏、光照骨架） |
| 🔧 | LVGL 滑动窗口统计（dB 30秒/温湿度 5分钟、avg/min/max UI 显示） |

---

## 新增文件

```
ESP/main/components/sensor_cache/inc/sensor_cache.h
ESP/main/components/sensor_cache/src/sensor_cache.c
ESP/main/components/sensor_cache/CMakeLists.txt
ESP/main/components/my_mqtt/inc/mqtt_report_dispatcher.h
ESP/main/components/my_mqtt/src/mqtt_report_dispatcher.c
```

---

## 待办

- [ ] 烧录 STM32 最新固件，双端联调
- [ ] OneNET 物模型添加统计字段（dB_avg/dB_max/dB_min/temp_avg/humi_avg）
- [ ] OneNET 可视化仪表盘配置
- [ ] 可选：光照传感器 BH1750 硬件接入
- [ ] 校验和命名统一（RX_READ_CRC_L → RX_READ_CHECKSUM_L，纯命名优化）
