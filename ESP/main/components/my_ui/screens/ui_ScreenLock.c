#include "../ui.h"

lv_obj_t * ui_ScreenLock = NULL;
lv_obj_t * ui_Panel2 = NULL;
lv_obj_t * ui_lockText = NULL;

void ui_event_ScreenLock(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED || (event_code == LV_EVENT_GESTURE &&
       lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT)) {
        if(event_code == LV_EVENT_GESTURE) {
            lv_indev_wait_release(lv_indev_active());
        }
        _ui_screen_change(&ui_main01, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, &ui_main01_screen_init);
        ui_ScreenLock = NULL;
        ui_Panel2 = NULL;
        ui_lockText = NULL;
    }
    if(event_code == LV_EVENT_SCREEN_LOADED) {
        screenLockBall_Animation(ui_Panel2, 0);
    }
}

void ui_ScreenLock_screen_init(void)
{
    ui_ScreenLock = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_ScreenLock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_ScreenLock, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ScreenLock, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 弹跳球
    ui_Panel2 = lv_obj_create(ui_ScreenLock);
    lv_obj_set_size(ui_Panel2, 40, 40);
    lv_obj_set_pos(ui_Panel2, 0, -100);
    lv_obj_set_align(ui_Panel2, LV_ALIGN_CENTER);
    lv_obj_set_ext_click_area(ui_Panel2, 1);
    lv_obj_remove_flag(ui_Panel2, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                       LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_radius(ui_Panel2, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Panel2, lv_color_hex(0x5B96F5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Panel2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 提示文字
    ui_lockText = lv_label_create(ui_ScreenLock);
    lv_obj_set_pos(ui_lockText, 0, 120);
    lv_obj_set_align(ui_lockText, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lockText, "Swipe left or tap to unlock");
    lv_obj_set_style_text_font(ui_lockText, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_lockText, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_ScreenLock, ui_event_ScreenLock, LV_EVENT_ALL, NULL);
}

void ui_ScreenLock_screen_destroy(void)
{
    if(ui_ScreenLock) lv_obj_del(ui_ScreenLock);

    ui_ScreenLock = NULL;
    ui_Panel2 = NULL;
    ui_lockText = NULL;
}
