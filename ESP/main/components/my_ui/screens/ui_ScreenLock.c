#include "../ui.h"
#include <sys/time.h>

lv_obj_t *ui_ScreenLock = NULL;
lv_obj_t *ui_lockTime = NULL;
lv_obj_t *ui_lockDate = NULL;
lv_obj_t *ui_lockIconBg = NULL;
lv_obj_t *ui_lockIcon = NULL;
lv_obj_t *ui_lockHint = NULL;

static lv_timer_t *lock_time_timer = NULL;

static void anim_set_opa(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

static void lock_time_update(lv_timer_t *timer)
{
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char buf[16];

    strftime(buf, sizeof(buf), "%H:%M", timeinfo);
    if (ui_lockTime) lv_label_set_text(ui_lockTime, buf);

    strftime(buf, sizeof(buf), "%Y-%m-%d", timeinfo);
    if (ui_lockDate) lv_label_set_text(ui_lockDate, buf);
}

static void lock_start_animations(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_lockIconBg);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_set_values(&a, 40, 120);
    lv_anim_set_time(&a, 1800);
    lv_anim_set_playback_time(&a, 1800);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, ui_lockHint);
    lv_anim_set_exec_cb(&b, anim_set_opa);
    lv_anim_set_values(&b, 60, 255);
    lv_anim_set_time(&b, 2200);
    lv_anim_set_playback_time(&b, 2200);
    lv_anim_set_repeat_count(&b, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in_out);
    lv_anim_set_delay(&b, 400);
    lv_anim_start(&b);
}

void ui_event_ScreenLock(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || (code == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT)) {
        if (code == LV_EVENT_GESTURE) {
            lv_indev_wait_release(lv_indev_active());
        }
        if (lock_time_timer) {
            lv_timer_del(lock_time_timer);
            lock_time_timer = NULL;
        }
        _ui_screen_change(&ui_main01, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, &ui_main01_screen_init);
    }

    if (code == LV_EVENT_SCREEN_LOADED) {
        if (!lock_time_timer) {
            lock_time_timer = lv_timer_create(lock_time_update, 1000, NULL);
            lock_time_update(NULL);
        }
        lock_start_animations();
    }
}

void ui_ScreenLock_screen_init(void)
{
    ui_ScreenLock = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_ScreenLock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ScreenLock, lv_color_hex(0x0A0A14), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenLock, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lockTime = lv_label_create(ui_ScreenLock);
    lv_obj_set_pos(ui_lockTime, 0, -78);
    lv_obj_set_align(ui_lockTime, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lockTime, "00:00");
    lv_obj_set_style_text_font(ui_lockTime, &lv_font_montserrat_38, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lockTime, lv_color_hex(0xEEEEEE), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lockDate = lv_label_create(ui_ScreenLock);
    lv_obj_set_pos(ui_lockDate, 0, -30);
    lv_obj_set_align(ui_lockDate, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lockDate, "0000-00-00");
    lv_obj_set_style_text_font(ui_lockDate, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lockDate, lv_color_hex(0x777799), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lockIconBg = lv_obj_create(ui_ScreenLock);
    lv_obj_set_size(ui_lockIconBg, 74, 74);
    lv_obj_set_pos(ui_lockIconBg, 0, 32);
    lv_obj_set_align(ui_lockIconBg, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_lockIconBg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_lockIconBg, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_lockIconBg, lv_color_hex(0x5B96F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_lockIconBg, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_lockIconBg, lv_color_hex(0x5B96F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_lockIconBg, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_lockIconBg, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lockIcon = lv_label_create(ui_lockIconBg);
    lv_obj_center(ui_lockIcon);
    lv_label_set_text(ui_lockIcon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(ui_lockIcon, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lockIcon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lockHint = lv_label_create(ui_ScreenLock);
    lv_obj_set_pos(ui_lockHint, 0, 112);
    lv_obj_set_align(ui_lockHint, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lockHint, "Swipe left or tap to unlock");
    lv_obj_set_style_text_font(ui_lockHint, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lockHint, lv_color_hex(0x5B96F5), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_ScreenLock, ui_event_ScreenLock, LV_EVENT_ALL, NULL);
}

void ui_ScreenLock_screen_destroy(void)
{
    if (lock_time_timer) {
        lv_timer_del(lock_time_timer);
        lock_time_timer = NULL;
    }
    if (ui_ScreenLock) lv_obj_del(ui_ScreenLock);

    ui_ScreenLock = NULL;
    ui_lockTime = NULL;
    ui_lockDate = NULL;
    ui_lockIconBg = NULL;
    ui_lockIcon = NULL;
    ui_lockHint = NULL;
}
