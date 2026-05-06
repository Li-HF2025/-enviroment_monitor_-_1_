#include "screen_idle_lock.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_helpers.h"

static uint32_t s_timeout_ms = (5*60*1000); // 默认5分钟无操作后进入锁屏界面
static uint32_t s_last_activity_time = 0; // 上次活动时间戳
static bool s_screen_locked = false; // 当前屏幕是否处于锁屏状态

void screen_idle_lock_init(uint32_t timeout_ms){
    s_timeout_ms = timeout_ms;
    s_last_activity_time = lv_tick_get(); // 获取当前LVGL tick作为初始活动时间
    s_screen_locked = false; // 初始状态为未锁屏
}
void screen_idle_lock_mark_activity(void)
{
    s_last_activity_time = lv_tick_get();
}
bool screen_idle_lock_is_locked(void)
{
    return s_screen_locked;
}
void screen_idle_lock_poll(void)
{
    lv_obj_t *active = lv_screen_active();

    if (s_screen_locked) {
        if (active != ui_ScreenLock) {
            s_screen_locked = false;
            s_last_activity_time = lv_tick_get();
        }
        return;
    }

    if (active == ui_ScreenLock) {
        s_screen_locked = true;
        return;
    }

    if (lv_tick_elaps(s_last_activity_time) >= s_timeout_ms) {
        _ui_screen_change(&ui_ScreenLock, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_ScreenLock_screen_init);
        s_screen_locked = true;
    }
}