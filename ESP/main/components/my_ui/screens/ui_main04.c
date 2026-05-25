#include "../ui.h"

lv_obj_t * ui_main04 = NULL;
lv_obj_t * ui_lightImage = NULL;
lv_obj_t * ui_lightTitle = NULL;

void ui_event_main04(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main01, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_main01_screen_init);
    }
}

void ui_event_lightImage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_detialLight, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_detialLight_screen_init);
    }
}

void ui_main04_screen_init(void)
{
    ui_main04 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_main04, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_main04, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_main04, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lightTitle = lv_label_create(ui_main04);
    lv_obj_set_pos(ui_lightTitle, 0, 10);
    lv_obj_set_align(ui_lightTitle, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_lightTitle, "Light");
    lv_obj_set_style_text_font(ui_lightTitle, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lightImage = lv_image_create(ui_main04);
    lv_image_set_src(ui_lightImage, &ui_img_light_png);
    lv_obj_set_size(ui_lightImage, lv_pct(60), lv_pct(60));
    lv_obj_set_align(ui_lightImage, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_lightImage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ui_lightImage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ui_lightImage, ui_event_lightImage, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_main04, ui_event_main04, LV_EVENT_GESTURE, NULL);
}

void ui_main04_screen_destroy(void)
{
    if(ui_main04) lv_obj_del(ui_main04);

    ui_main04 = NULL;
    ui_lightImage = NULL;
    ui_lightTitle = NULL;
}
