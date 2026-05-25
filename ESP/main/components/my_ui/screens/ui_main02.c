#include "../ui.h"

lv_obj_t * ui_main02 = NULL;
lv_obj_t * ui_tempuratureImage = NULL;
lv_obj_t * ui_tempTitle = NULL;

void ui_event_main02(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main03, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_main03_screen_init);
    }
}

void ui_event_tempuratureImage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_detailTemperature, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_detailTemperature_screen_init);
    }
}

void ui_main02_screen_init(void)
{
    ui_main02 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_main02, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_main02, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_main02, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tempTitle = lv_label_create(ui_main02);
    lv_obj_set_pos(ui_tempTitle, 0, 10);
    lv_obj_set_align(ui_tempTitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_tempTitle, "Temperature");
    lv_obj_set_style_text_font(ui_tempTitle, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tempuratureImage = lv_image_create(ui_main02);
    lv_image_set_src(ui_tempuratureImage, &ui_img_temperature_png);
    lv_obj_set_size(ui_tempuratureImage, lv_pct(50), lv_pct(50));
    lv_obj_set_align(ui_tempuratureImage, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_tempuratureImage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ui_tempuratureImage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ui_tempuratureImage, ui_event_tempuratureImage, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_main02, ui_event_main02, LV_EVENT_GESTURE, NULL);
}

void ui_main02_screen_destroy(void)
{
    if(ui_main02) lv_obj_del(ui_main02);

    ui_main02 = NULL;
    ui_tempuratureImage = NULL;
    ui_tempTitle = NULL;
}
