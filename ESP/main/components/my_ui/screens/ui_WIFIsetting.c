#include "../ui.h"
#include "my_wifi.h"

lv_obj_t * ui_WIFIsetting = NULL;
lv_obj_t * ui_Panel5 = NULL;
lv_obj_t * ui_WIFIChoice = NULL;
lv_obj_t * ui_Label6 = NULL;
lv_obj_t * ui_passwordContainer = NULL;
lv_obj_t * ui_Panel6 = NULL;
lv_obj_t * ui_Label7 = NULL;
lv_obj_t * ui_TextArea1 = NULL;
lv_obj_t * ui_Keyboard1 = NULL;

void ui_event_WIFIsetting(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_setting, LV_SCR_LOAD_ANIM_NONE, 500, 0, &ui_setting_screen_init);
    }
}

void ui_event_value_changed(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_remove_flag(ui_passwordContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_event_WIFIConnect(lv_event_t * e){
    if(lv_event_get_code(e) == LV_EVENT_READY) {
        const char *password = lv_textarea_get_text(ui_TextArea1);
        char selected_wifi[32];
        lv_dropdown_get_selected_str(ui_WIFIChoice, selected_wifi, sizeof(selected_wifi));
        bool connect_result = wifi_connect(selected_wifi, password);
        if(connect_result) {
            lv_obj_add_flag(ui_passwordContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ui_Label7, "Connected!");
        } else {
            lv_label_set_text(ui_Label7, "Connection failed");
        }
    }
}

void ui_WIFIsetting_screen_init(void)
{
    ui_WIFIsetting = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_WIFIsetting, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_WIFIsetting, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_WIFIsetting, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 标题
    ui_Panel5 = lv_obj_create(ui_WIFIsetting);
    lv_obj_set_size(ui_Panel5, 230, 40);
    lv_obj_set_pos(ui_Panel5, 5, 5);
    lv_obj_remove_flag(ui_Panel5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel5, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label6 = lv_label_create(ui_Panel5);
    lv_obj_center(ui_Label6);
    lv_label_set_text(ui_Label6, "Select WiFi");
    lv_obj_set_style_text_font(ui_Label6, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    // WiFi 下拉框
    ui_WIFIChoice = lv_dropdown_create(ui_WIFIsetting);
    lv_dropdown_set_options(ui_WIFIChoice, "Scanning...");
    lv_obj_set_size(ui_WIFIChoice, 230, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_WIFIChoice, 0, -95);
    lv_obj_set_align(ui_WIFIChoice, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_WIFIChoice, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_radius(ui_WIFIChoice, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 密码区域
    ui_passwordContainer = lv_obj_create(ui_WIFIsetting);
    lv_obj_remove_style_all(ui_passwordContainer);
    lv_obj_set_size(ui_passwordContainer, 230, 36);
    lv_obj_set_pos(ui_passwordContainer, 0, -45);
    lv_obj_set_align(ui_passwordContainer, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_passwordContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_passwordContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(ui_passwordContainer, 0, 0);
    lv_obj_set_style_border_width(ui_passwordContainer, 0, 0);

    ui_Panel6 = lv_obj_create(ui_passwordContainer);
    lv_obj_set_size(ui_Panel6, 230, 36);
    lv_obj_set_align(ui_Panel6, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_Panel6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel6, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label7 = lv_label_create(ui_Panel6);
    lv_obj_set_pos(ui_Label7, 8, 0);
    lv_obj_set_align(ui_Label7, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_Label7, "Password:");

    ui_TextArea1 = lv_textarea_create(ui_Panel6);
    lv_obj_set_size(ui_TextArea1, 120, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_TextArea1, -8, 0);
    lv_obj_set_align(ui_TextArea1, LV_ALIGN_RIGHT_MID);
    lv_textarea_set_max_length(ui_TextArea1, 25);
    lv_textarea_set_one_line(ui_TextArea1, true);

    // 键盘
    ui_Keyboard1 = lv_keyboard_create(ui_WIFIsetting);
    lv_obj_set_size(ui_Keyboard1, 238, 115);
    lv_obj_set_pos(ui_Keyboard1, 0, 90);
    lv_obj_set_align(ui_Keyboard1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Keyboard1, LV_OBJ_FLAG_HIDDEN);

    lv_keyboard_set_textarea(ui_Keyboard1, ui_TextArea1);

    lv_obj_add_event_cb(ui_WIFIsetting, ui_event_WIFIsetting, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_WIFIChoice, ui_event_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_Keyboard1, ui_event_WIFIConnect, LV_EVENT_READY, NULL);
}

void ui_WIFIsetting_screen_destroy(void)
{
    if(ui_WIFIsetting) lv_obj_del(ui_WIFIsetting);

    ui_WIFIsetting = NULL;
    ui_Panel5 = NULL;
    ui_WIFIChoice = NULL;
    ui_Label6 = NULL;
    ui_passwordContainer = NULL;
    ui_Panel6 = NULL;
    ui_Label7 = NULL;
    ui_TextArea1 = NULL;
    ui_Keyboard1 = NULL;
}
