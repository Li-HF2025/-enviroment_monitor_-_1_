#include "detail_time_logic.h"
#include "ui.h"
#include "ui_helpers.h"
#include "lvgl.h"
#include "esp_log.h"
#include "my_wifi.h"
// SNTP协议头文件（用于获取网络时间，确保系统时间正确）
#include "esp_sntp.h"

#define SNTP_SYNC_INTERVAL_MS  (60 * 60 * 1000) // SNTP同步时间间隔，单位为毫秒，这里设置为1小时
#define TIME_ZONE "CST-8" // 时区设置，这里设置为中国标准时间（UTC+8）


extern lv_obj_t * ui_Label1;//main01的时间显示标签
extern lv_obj_t * ui_yearMonDay;//detailTime的年月日显示标签
extern lv_obj_t * ui_hour;//detailTime的时分显示标签
extern lv_obj_t * ui_Label3;//detailTime的星期显示标签

extern lv_obj_t * ui_DefaultTime;//默认时间开关
extern lv_obj_t * ui_localTime;//detailTime的本地时间输入框

static lv_timer_t * time_update_timer = NULL;//时间更新定时器

static void time_update_callback(lv_timer_t* timer){
    time_t now = time(NULL);
    struct tm * timeinfo = localtime(&now);
    char date_str[16];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", timeinfo);
    if (ui_yearMonDay != NULL) {
        lv_label_set_text(ui_yearMonDay, date_str);
    }
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    if (ui_hour != NULL) {
        lv_label_set_text(ui_hour, time_str);
    }
    if (ui_Label1 != NULL) {
        lv_label_set_text(ui_Label1, time_str);
    }
    char week_num_str[4];
    snprintf(week_num_str, sizeof(week_num_str), "%d", timeinfo->tm_wday);
    if (ui_Label3 != NULL) {
        lv_label_set_text(ui_Label3, week_num_str);
    }
    char datetime_str[32];
    snprintf(datetime_str, sizeof(datetime_str), "%s,%s", date_str, time_str);
    if (ui_localTime != NULL) {
        lv_textarea_set_text(ui_localTime, datetime_str);
    }
}
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI("SNTP", "时间校准完成！");
    // 打印校准后的本地时间
    time_t now = time(NULL);
    char strftime_buf[64];
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI("SNTP", "当前本地时间: %s", strftime_buf);
}


static void sntp_init_custom(void){
    ESP_LOGI("SNTP", "正在初始化SNTP时间同步...");
    
    setenv("TZ", TIME_ZONE, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.ntsc.ac.cn");

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);// 设置SNTP同步时间间隔

    esp_sntp_init();
}
void default_time_init(void){
    /* 只在界面对象已创建时执行一次更新，避免在未创建界面时访问 NULL 指针 */
    if (ui_yearMonDay != NULL || ui_hour != NULL || ui_Label1 != NULL || ui_Label3 != NULL || ui_localTime != NULL) {
        time_update_callback(NULL);
    }
}

void default_time_start(void){
    sntp_init_custom();// 初始化SNTP时间同步
    
    // 打印当前时间验证
    time_t now = time(NULL);
    char strftime_buf[64];
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI("SNTP", "系统当前时间: %s", strftime_buf);
}
void update_time_start(void) {

    if (time_update_timer == NULL) {
        time_update_timer = lv_timer_create(time_update_callback, 1000, NULL);
        ESP_LOGI("TIME", "时间更新定时器已启动(每秒更新)");
    }
}

void update_time_stop(void) {
    if (time_update_timer != NULL) {
        lv_timer_del(time_update_timer);
        time_update_timer = NULL;
        ESP_LOGI("TIME", "时间更新定时器已停止");
    }
    esp_sntp_stop();
    ESP_LOGI("SNTP", "SNTP时间同步已停止");
}
