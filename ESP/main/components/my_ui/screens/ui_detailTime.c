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

static void keyboard_show(void)
{
    lv_obj_set_y(ui_Keyboard2, 212);
    lv_obj_set_y(ui_backMain, 160);
}

static void keyboard_hide(void)
{
    lv_obj_set_y(ui_Keyboard2, 320);
    lv_obj_set_y(ui_backMain, 210);
}

void ui_event_localTime(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_Keyboard2, ui_localTime);
        keyboard_show();
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
        if(input_text != NULL && strlen(input_text) > 0) {
            ESP_LOGI("KeyInput", "%s", input_text);
            struct tm timeinfo = {0};
            time_t new_time;
            if(sscanf(input_text, "%d-%d-%d,%d:%d:%d",
                     &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday,
                     &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec) == 6) {
                timeinfo.tm_year -= 1900;
                timeinfo.tm_mon -= 1;
                timeinfo.tm_isdst = -1;

                new_time = mktime(&timeinfo);
                if(new_time != (time_t)-1) {
                    struct timeval tv = {new_time, 0};
                    if(settimeofday(&tv, NULL) == 0) {
                        ESP_LOGI("TimeSet", "time set success");
                        keyboard_hide();
                        update_time_start();
                    } else {
                        ESP_LOGE("TimeSet", "time set failed");
                    }
                }
            }
        }
    }
}

void ui_event_DefaultTime(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if(lv_obj_has_state(ui_DefaultTime, LV_STATE_CHECKED)) {
            update_time_stop();
        } else {
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

    // ── 标题栏 ──
    lv_obj_t * title_panel = lv_obj_create(ui_detailTime);
    lv_obj_set_size(title_panel, 230, 36);
    lv_obj_set_pos(title_panel, 0, 5);
    lv_obj_set_align(title_panel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * title_label = lv_label_create(title_panel);
    lv_obj_center(title_label);
    lv_label_set_text(title_label, "Time Settings");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 日期卡片 ──
    ui_Panel1 = lv_obj_create(ui_detailTime);
    lv_obj_set_size(ui_Panel1, 230, 46);
    lv_obj_set_pos(ui_Panel1, 0, 48);
    lv_obj_set_align(ui_Panel1, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_Panel1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel1, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Panel1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_yearMonDay = lv_label_create(ui_Panel1);
    lv_obj_center(ui_yearMonDay);
    lv_label_set_text(ui_yearMonDay, "YYYY-MM-DD");
    lv_obj_set_style_text_font(ui_yearMonDay, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_yearMonDay, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 时间 + 星期卡片 ──
    ui_Panel3 = lv_obj_create(ui_detailTime);
    lv_obj_set_size(ui_Panel3, 230, 46);
    lv_obj_set_pos(ui_Panel3, 0, 100);
    lv_obj_set_align(ui_Panel3, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_Panel3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_Panel3, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel3, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Panel3, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_hour = lv_label_create(ui_Panel3);
    lv_obj_set_pos(ui_hour, -20, 0);
    lv_obj_set_align(ui_hour, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_hour, "HH:MM:SS");
    lv_obj_set_style_text_font(ui_hour, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label3 = lv_label_create(ui_Panel3);
    lv_obj_set_pos(ui_Label3, 20, 0);
    lv_obj_set_align(ui_Label3, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_Label3, "Day");
    lv_obj_set_style_text_font(ui_Label3, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 输入卡片 ──
    lv_obj_t * input_card = lv_obj_create(ui_detailTime);
    lv_obj_set_size(input_card, 230, 46);
    lv_obj_set_pos(input_card, 0, 154);
    lv_obj_set_align(input_card, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(input_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(input_card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(input_card, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(input_card, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_localTime = lv_textarea_create(input_card);
    lv_obj_set_size(ui_localTime, 140, 35);
    lv_obj_set_pos(ui_localTime, -25, 0);
    lv_obj_set_align(ui_localTime, LV_ALIGN_LEFT_MID);
    lv_textarea_set_placeholder_text(ui_localTime, "input time");
    lv_obj_set_style_text_color(ui_localTime, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_localTime, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_DefaultTime = lv_switch_create(input_card);
    lv_obj_set_size(ui_DefaultTime, 50, 25);
    lv_obj_set_pos(ui_DefaultTime, 25, 0);
    lv_obj_set_align(ui_DefaultTime, LV_ALIGN_RIGHT_MID);

    // ── 返回按钮 ──
    ui_backMain = lv_button_create(ui_detailTime);
    lv_obj_set_size(ui_backMain, 180, 42);
    lv_obj_set_pos(ui_backMain, 0, 210);
    lv_obj_set_align(ui_backMain, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_backMain, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_backMain, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_backMain, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_backMain, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_backMain, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_backMain, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label4 = lv_label_create(ui_backMain);
    lv_obj_center(ui_Label4);
    lv_label_set_text(ui_Label4, "Back");
    lv_obj_set_style_text_font(ui_Label4, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Label4, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 键盘（初始隐藏在屏幕外） ──
    ui_Keyboard2 = lv_keyboard_create(ui_detailTime);
    lv_keyboard_set_mode(ui_Keyboard2, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(ui_Keyboard2, 237, 105);
    lv_obj_set_pos(ui_Keyboard2, 0, 320);
    lv_obj_set_align(ui_Keyboard2, LV_ALIGN_TOP_MID);

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
