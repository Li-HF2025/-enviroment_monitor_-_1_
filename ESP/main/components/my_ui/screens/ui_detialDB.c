#include "../ui.h"
#include "detail_dB_logic.h"

lv_obj_t * ui_detialDB = NULL;
lv_obj_t * ui_Switch2 = NULL;
lv_obj_t * ui_dBSwitchLabel = NULL;

void ui_event_detialDB(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main03, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, &ui_main03_screen_init);
    }
}

void ui_event_Switch2(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * target = lv_event_get_target(e);
        bool is_on = lv_obj_has_state(target, LV_STATE_CHECKED);
        if(is_on){
            dB_init();
            lv_label_set_text(ui_dBSwitchLabel, "Sensor: ON");
        }else{
            dB_deinit();
            lv_label_set_text(ui_dBSwitchLabel, "Sensor: OFF");
        }
    }
}

void ui_detialDB_screen_init(void)
{
    ui_detialDB = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_detialDB, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_detialDB, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_detialDB, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 标题
    lv_obj_t * title = lv_label_create(ui_detialDB);
    lv_obj_set_pos(title, 0, 10);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_label_set_text(title, "Sound Sensor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 开关卡片
    lv_obj_t * card = lv_obj_create(ui_detialDB);
    lv_obj_set_size(card, 220, 70);
    lv_obj_set_pos(card, 0, -20);
    lv_obj_set_align(card, LV_ALIGN_CENTER);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBSwitchLabel = lv_label_create(card);
    lv_obj_set_pos(ui_dBSwitchLabel, -30, 0);
    lv_obj_set_align(ui_dBSwitchLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_dBSwitchLabel, "Sensor: ON");
    lv_obj_set_style_text_font(ui_dBSwitchLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Switch2 = lv_switch_create(card);
    lv_obj_set_size(ui_Switch2, 50, 25);
    lv_obj_set_pos(ui_Switch2, -30, 0);
    lv_obj_set_align(ui_Switch2, LV_ALIGN_RIGHT_MID);
    lv_obj_set_state(ui_Switch2, LV_STATE_CHECKED, true);

    lv_obj_add_event_cb(ui_detialDB, ui_event_detialDB, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_Switch2, ui_event_Switch2, LV_EVENT_VALUE_CHANGED, NULL);
    dB_start();
}

void ui_detialDB_screen_destroy(void)
{
    if(ui_detialDB) lv_obj_del(ui_detialDB);

    ui_detialDB = NULL;
    ui_Switch2 = NULL;
    ui_dBSwitchLabel = NULL;
}
