#ifndef __SENSOR_CACHE_H__
#define __SENSOR_CACHE_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单条缓存条目（6 字节，与 SensorDataBin 对齐） */
typedef struct __attribute__((packed)) {
    uint8_t  sensor_type;  // 0x01=温度, 0x02=分贝, 0x04=湿度
    int16_t  value_x10;    // 数值 × 10
    uint32_t timestamp;    // Unix 时间戳
} SensorCacheEntry;        // 8 字节

/** @brief 初始化缓存模块（内存环形缓冲） */
void sensor_cache_init(void);

/** @brief 压入一条传感器数据到环形缓冲 */
void sensor_cache_push(uint8_t sensor_type, int16_t value_x10);

/** @brief 获取待补传条数 */
int  sensor_cache_pending_count(void);

/**
 * @brief 批量补传：对缓冲中每条数据调用 publish_fn
 * @param publish_fn  回调：sensor_type, value_x10, timestamp → 返回 true 表示上传成功
 * @return 成功上传的条数（失败的数据会保留）
 */
typedef bool (*sensor_cache_publish_fn_t)(uint8_t sensor_type, int16_t value_x10, uint32_t timestamp);
int  sensor_cache_flush(sensor_cache_publish_fn_t publish_fn);

/** @brief 情况：手动清空全部缓存 */
void sensor_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_CACHE_H__ */
