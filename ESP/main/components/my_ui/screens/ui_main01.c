#include "../ui.h"
#include <stdio.h>
#include "detail_dB_logic.h"
#include "detail_temp_logic.h"

lv_obj_t * ui_main01 = NULL;
lv_obj_t * ui_timeBase = NULL;
lv_obj_t * ui_Label1 = NULL;
lv_obj_t * ui_dBTable = NULL;
lv_obj_t * ui_dBNum = NULL;
lv_obj_t * ui_temperatureTable = NULL;
lv_obj_t * ui_temperatureNum = NULL;
lv_obj_t * ui_lightTable = NULL;
lv_obj_t * ui_Label2 = NULL;

static lv_timer_t * dB_ui_timer = NULL;

void ui_event_main01(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main02, LV_SCR_LOAD_ANIM_OVER_LEFT, 500, 0, &ui_main02_screen_init);
    }
    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_setting, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_setting_screen_init);
    }
}

void ui_event_Label1(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_detailTime, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_detailTime_screen_init);
    }
}

static void ui_timer_cb(lv_timer_t * timer)
{
    if(dB_get_latest_valid() && ui_dBNum){
        char dB_str[16];
        snprintf(dB_str, sizeof(dB_str), "dB:%.1f", dB_get_latest_value());
        lv_label_set_text(ui_dBNum, dB_str);
    }
    if(temp_get_latest_valid() && ui_temperatureNum){
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "Temp:%.1f", temp_get_latest_value());
        lv_label_set_text(ui_temperatureNum, temp_str);
    }
}

static lv_obj_t * create_sensor_card(lv_obj_t * parent, int y)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 220, 48);
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_align(card, LV_ALIGN_CENTER);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    return card;
}

void ui_main01_screen_init(void)
{
    ui_main01 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_main01, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_main01, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_main01, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 时间卡片 ──
    ui_timeBase = create_sensor_card(ui_main01, -100);
    lv_obj_set_style_bg_color(ui_timeBase, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label1 = lv_label_create(ui_timeBase);
    lv_obj_center(ui_Label1);
    lv_label_set_text(ui_Label1, "HH:MM:SS");
    lv_obj_set_style_text_font(ui_Label1, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_Label1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ui_Label1, 5);

    // ── 分贝卡片 ──
    ui_dBTable = create_sensor_card(ui_main01, -42);

    ui_dBNum = lv_label_create(ui_dBTable);
    lv_obj_center(ui_dBNum);
    lv_label_set_text(ui_dBNum, "dB:--.-");
    lv_obj_set_style_text_font(ui_dBNum, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 温度卡片 ──
    ui_temperatureTable = create_sensor_card(ui_main01, 16);

    ui_temperatureNum = lv_label_create(ui_temperatureTable);
    lv_obj_center(ui_temperatureNum);
    lv_label_set_text(ui_temperatureNum, "Temp:--.-");
    lv_obj_set_style_text_font(ui_temperatureNum, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 光照卡片 ──
    ui_lightTable = create_sensor_card(ui_main01, 74);

    ui_Label2 = lv_label_create(ui_lightTable);
    lv_obj_center(ui_Label2);
    lv_label_set_text(ui_Label2, "Light:--.-");
    lv_obj_set_style_text_font(ui_Label2, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Label1, ui_event_Label1, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_main01, ui_event_main01, LV_EVENT_ALL, NULL);

    if(dB_ui_timer == NULL){
        dB_ui_timer = lv_timer_create(ui_timer_cb, 100, NULL);
    }
}

void ui_main01_screen_destroy(void)
{
    if(dB_ui_timer){
        lv_timer_del(dB_ui_timer);
        dB_ui_timer = NULL;
    }

    if(ui_main01) lv_obj_del(ui_main01);

    ui_main01 = NULL;
    ui_timeBase = NULL;
    ui_Label1 = NULL;
    ui_dBTable = NULL;
    ui_dBNum = NULL;
    ui_temperatureTable = NULL;
    ui_temperatureNum = NULL;
    ui_lightTable = NULL;
    ui_Label2 = NULL;
}
