#include "../ui.h"
#include "detail_time_logic.h"
#include "string.h"
#include "esp_log.h"
#include <sys/time.h>

lv_obj_t * ui_detailTime = NULL;
lv_obj_t * ui_Panel1 = NULL;
lv_obj_t * ui_yearMonDay = NULL;
lv_obj_t * ui_Panel3 = NULL;
lv_obj_t * ui_hour = NULL;
lv_obj_t * ui_Label3 = NULL;
lv_obj_t * ui_DefaultTime = NULL;
lv_obj_t * ui_localTime = NULL;
lv_obj_t * ui_backMain = NULL;
lv_obj_t * ui_Label4 = NULL;
lv_obj_t * ui_Keyboard2 = NULL;
lv_obj_t * ui_Error01 = NULL;

void ui_event_localTime(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_Keyboard2,  ui_localTime);
        lv_obj_set_y(ui_Keyboard2, 160);
    }
}

void ui_event_backMain(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_main01, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_main01_screen_init);
    }
}

void ui_event_Keyboard2(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_READY) {
        const char *input_text = lv_textarea_get_text(ui_localTime);
        if(input_text!=NULL && strlen(input_text)>0){
            ESP_LOGI("KeyInput","%s",input_text);
            struct tm timeinfo = {0};
            time_t new_time;
            if(sscanf(input_text, "%d-%d-%d,%d:%d:%d",
                     &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday,
                     &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec) == 6) {
                timeinfo.tm_year -= 1900;
                timeinfo.tm_mon -= 1;
                timeinfo.tm_isdst = -1;

                new_time = mktime(&timeinfo);
                if(new_time !=(time_t)-1){
                    struct timeval tv = {new_time, 0};
                    if(settimeofday(&tv, NULL) == 0){
                        ESP_LOGI("TimeSet","time set success");
                        lv_obj_set_y(ui_Keyboard2, 225);
                        update_time_start();
                    } else {
                        ESP_LOGE("TimeSet","time set failed");
                    }
                }
            }
        }
    }
}

void ui_event_DefaultTime(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if(lv_obj_has_state(ui_DefaultTime, LV_STATE_CHECKED)){
            update_time_stop();
        }else{
            default_time_init();
            default_time_start();
            update_time_start();
        }
    }
}

void ui_detailTime_screen_init(void)
{
    ui_detailTime = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_detailTime, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_detailTime, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_detailTime, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 日期卡片 ──
    ui_Panel1 = lv_obj_create(ui_detailTime);
    lv_obj_set_size(ui_Panel1, 225, 50);
    lv_obj_set_pos(ui_Panel1, 0, -120);
    lv_obj_set_align(ui_Panel1, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_Panel1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel1, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Panel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_yearMonDay = lv_label_create(ui_Panel1);
    lv_obj_center(ui_yearMonDay);
    lv_label_set_text(ui_yearMonDay, "YYYY:MM:DD");
    lv_obj_set_style_text_font(ui_yearMonDay, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_yearMonDay, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 时间卡片 ──
    ui_Panel3 = lv_obj_create(ui_detailTime);
    lv_obj_set_size(ui_Panel3, 225, 50);
    lv_obj_set_pos(ui_Panel3, 0, -55);
    lv_obj_set_align(ui_Panel3, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_Panel3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel3, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel3, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Panel3, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_hour = lv_label_create(ui_Panel3);
    lv_obj_set_pos(ui_hour, -15, 0);
    lv_obj_set_align(ui_hour, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_hour, "HH:MM:SS");
    lv_obj_set_style_text_font(ui_hour, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label3 = lv_label_create(ui_Panel3);
    lv_obj_set_pos(ui_Label3, 15, 0);
    lv_obj_set_align(ui_Label3, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_Label3, "Week");
    lv_obj_set_style_text_font(ui_Label3, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 输入行 ──
    lv_obj_t * row_input = lv_obj_create(ui_detailTime);
    lv_obj_set_size(row_input, 225, 44);
    lv_obj_set_pos(row_input, 0, 5);
    lv_obj_set_align(row_input, LV_ALIGN_CENTER);
    lv_obj_remove_flag(row_input, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_input, 0, 0);
    lv_obj_set_style_border_width(row_input, 0, 0);
    lv_obj_set_style_pad_all(row_input, 0, 0);

    ui_localTime = lv_textarea_create(row_input);
    lv_obj_set_size(ui_localTime, 140, 35);
    lv_obj_set_pos(ui_localTime, -38, 0);
    lv_obj_set_align(ui_localTime, LV_ALIGN_LEFT_MID);
    lv_textarea_set_placeholder_text(ui_localTime, "input time");
    lv_obj_set_style_text_color(ui_localTime, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_localTime, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_DefaultTime = lv_switch_create(row_input);
    lv_obj_set_size(ui_DefaultTime, 50, 25);
    lv_obj_set_pos(ui_DefaultTime, 38, 0);
    lv_obj_set_align(ui_DefaultTime, LV_ALIGN_RIGHT_MID);

    // ── 返回按钮 ──
    ui_backMain = lv_button_create(ui_detailTime);
    lv_obj_set_size(ui_backMain, 160, 44);
    lv_obj_set_pos(ui_backMain, 0, 80);
    lv_obj_set_align(ui_backMain, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_backMain, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_backMain, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_backMain, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_backMain, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_backMain, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_backMain, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label4 = lv_label_create(ui_backMain);
    lv_obj_center(ui_Label4);
    lv_label_set_text(ui_Label4, "Back");
    lv_obj_set_style_text_font(ui_Label4, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Label4, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 键盘（初始隐藏在屏幕外） ──
    ui_Keyboard2 = lv_keyboard_create(ui_detailTime);
    lv_keyboard_set_mode(ui_Keyboard2, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(ui_Keyboard2, 237, 105);
    lv_obj_set_pos(ui_Keyboard2, 0, 225);
    lv_obj_set_align(ui_Keyboard2, LV_ALIGN_CENTER);

    lv_obj_add_event_cb(ui_localTime, ui_event_localTime, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_backMain, ui_event_backMain, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Keyboard2, ui_event_Keyboard2, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui_DefaultTime, ui_event_DefaultTime, LV_EVENT_CLICKED, NULL);
    default_time_init();
    default_time_start();
    update_time_start();
}

void ui_detailTime_screen_destroy(void)
{
    if(ui_detailTime) lv_obj_del(ui_detailTime);

    ui_detailTime = NULL;
    ui_Panel1 = NULL;
    ui_yearMonDay = NULL;
    ui_Panel3 = NULL;
    ui_hour = NULL;
    ui_Label3 = NULL;
    ui_DefaultTime = NULL;
    ui_localTime = NULL;
    ui_backMain = NULL;
    ui_Label4 = NULL;
    ui_Keyboard2 = NULL;

    update_time_stop();
}
