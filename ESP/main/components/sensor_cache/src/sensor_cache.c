#include "sensor_cache.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CACHE_MAX_ENTRIES 1000  // 环形缓冲最大条数（1000 条 ≈ 8KB）

static SensorCacheEntry s_cache[CACHE_MAX_ENTRIES];
static int  s_head = 0;   // 写入位置
static int  s_tail = 0;   // 读取位置（未补传的最老数据）
static int  s_count = 0;  // 当前缓存条数
static SemaphoreHandle_t s_lock = NULL;

void sensor_cache_init(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void sensor_cache_push(uint8_t sensor_type, int16_t value_x10)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    // 时间戳：使用 FreeRTOS 自启动以来的秒数（离线场景不需要绝对时间）
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    s_cache[s_head].sensor_type = sensor_type;
    s_cache[s_head].value_x10   = value_x10;
    s_cache[s_head].timestamp   = now;
    s_head = (s_head + 1) % CACHE_MAX_ENTRIES;

    if (s_count < CACHE_MAX_ENTRIES) {
        s_count++;
    } else {
        // 环形缓冲已满，覆盖最老数据
        s_tail = (s_tail + 1) % CACHE_MAX_ENTRIES;
    }

    xSemaphoreGive(s_lock);
}

int sensor_cache_pending_count(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int count = s_count;
    xSemaphoreGive(s_lock);
    return count;
}

int sensor_cache_flush(sensor_cache_publish_fn_t publish_fn)
{
    if (!s_lock || !publish_fn) return 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    int success = 0;
    while (s_count > 0) {
        SensorCacheEntry *entry = &s_cache[s_tail];
        if (publish_fn(entry->sensor_type, entry->value_x10, entry->timestamp)) {
            s_tail = (s_tail + 1) % CACHE_MAX_ENTRIES;
            s_count--;
            success++;
        } else {
            break;  // 上传失败则停止，保留剩余数据下次重试
        }
    }

    xSemaphoreGive(s_lock);
    return success;
}

void sensor_cache_clear(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    xSemaphoreGive(s_lock);
}
