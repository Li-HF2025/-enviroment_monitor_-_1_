#include "../ui.h"
#include "wifi_scan.h"

lv_obj_t * ui_setting = NULL;
lv_obj_t * ui_WIFI = NULL;
lv_obj_t * ui_Label5 = NULL;
lv_obj_t * ui_WIFISwitch = NULL;
lv_obj_t * ui_OTA = NULL;
lv_obj_t * ui_Label06 = NULL;
static TaskHandle_t s_wifi_scan_task = NULL;

void ui_wifi_scan_task_clear_handle(void)
{
    s_wifi_scan_task = NULL;
}

void ui_event_setting(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main01, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_main01_screen_init);
    }
}

void ui_event_Label5(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_WIFIsetting, LV_SCR_LOAD_ANIM_NONE, 500, 0, &ui_WIFIsetting_screen_init);
        if (s_wifi_scan_task == NULL) {
            xTaskCreate(wifi_scan_worker_task, "wifi_scan_task", 4096, NULL, 5, &s_wifi_scan_task);
        }
    }
}

void ui_event_WIFISwitch(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * target = lv_event_get_target(e);
        bool is_on = lv_obj_has_state(target, LV_STATE_CHECKED);
        if(is_on){
            lv_obj_add_flag(ui_Label5, LV_OBJ_FLAG_CLICKABLE);
        }else{
            lv_obj_remove_flag(ui_Label5, LV_OBJ_FLAG_CLICKABLE);
            wifi_disconnect();
        }
    }
}

void ui_event_OTA(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_detailOTA, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_detailOTA_screen_init);
    }
}

void ui_setting_screen_init(void)
{
    ui_setting = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_setting, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_setting, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_setting, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── WiFi 行 ──
    ui_WIFI = lv_obj_create(ui_setting);
    lv_obj_set_size(ui_WIFI, 230, 44);
    lv_obj_set_pos(ui_WIFI, 5, 10);
    lv_obj_remove_flag(ui_WIFI, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_WIFI, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_WIFI, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_WIFI, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label5 = lv_label_create(ui_WIFI);
    lv_obj_set_pos(ui_Label5, 15, 0);
    lv_obj_set_align(ui_Label5, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_Label5, "WiFi Settings");
    lv_obj_add_flag(ui_Label5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(ui_Label5, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_WIFISwitch = lv_switch_create(ui_WIFI);
    lv_obj_set_size(ui_WIFISwitch, 50, 25);
    lv_obj_set_pos(ui_WIFISwitch, -15, 0);
    lv_obj_set_align(ui_WIFISwitch, LV_ALIGN_RIGHT_MID);
    lv_obj_set_state(ui_WIFISwitch, LV_STATE_CHECKED, true);

    // ── OTA 行 ──
    ui_OTA = lv_obj_create(ui_setting);
    lv_obj_set_size(ui_OTA, 230, 44);
    lv_obj_set_pos(ui_OTA, 5, 62);
    lv_obj_remove_flag(ui_OTA, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_OTA, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_OTA, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_OTA, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label06 = lv_label_create(ui_OTA);
    lv_obj_center(ui_Label06);
    lv_label_set_text(ui_Label06, "Check Version");
    lv_obj_set_style_text_font(ui_Label06, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Label5, ui_event_Label5, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_WIFISwitch, ui_event_WIFISwitch, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_OTA, ui_event_OTA, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_setting, ui_event_setting, LV_EVENT_GESTURE, NULL);
}

void ui_setting_screen_destroy(void)
{
    if(ui_setting) lv_obj_del(ui_setting);

    ui_setting = NULL;
    ui_WIFI = NULL;
    ui_OTA = NULL;
    ui_Label5 = NULL;
    ui_Label06 = NULL;
    ui_WIFISwitch = NULL;
}
