#include "../ui.h"

lv_obj_t * ui_main03 = NULL;
lv_obj_t * ui_dBImage = NULL;
lv_obj_t * ui_dBTitle = NULL;

void ui_event_main03(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main04, LV_SCR_LOAD_ANIM_OVER_LEFT, 500, 0, &ui_main04_screen_init);
    }
}

void ui_event_dBImage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_detialDB, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_detialDB_screen_init);
    }
}

void ui_main03_screen_init(void)
{
    ui_main03 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_main03, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_main03, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_main03, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBTitle = lv_label_create(ui_main03);
    lv_obj_set_pos(ui_dBTitle, 0, 10);
    lv_obj_set_align(ui_dBTitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_dBTitle, "Sound Level");
    lv_obj_set_style_text_font(ui_dBTitle, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBImage = lv_image_create(ui_main03);
    lv_image_set_src(ui_dBImage, &ui_img_db_png);
    lv_obj_set_size(ui_dBImage, lv_pct(60), lv_pct(60));
    lv_obj_set_align(ui_dBImage, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_dBImage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ui_dBImage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ui_dBImage, ui_event_dBImage, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_main03, ui_event_main03, LV_EVENT_GESTURE, NULL);
}

void ui_main03_screen_destroy(void)
{
    if(ui_main03) lv_obj_del(ui_main03);

    ui_main03 = NULL;
    ui_dBImage = NULL;
    ui_dBTitle = NULL;
}
