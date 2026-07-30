#include "../ui.h"
#include "detail_dB_logic.h"
#include "lvgl.h"

lv_obj_t * ui_detialDB = NULL;
lv_obj_t * ui_dBValue = NULL;
lv_obj_t * ui_dBMaxVal = NULL;
lv_obj_t * ui_dBMinVal = NULL;
lv_obj_t * ui_dBAvgVal = NULL;
lv_obj_t * ui_Switch2 = NULL;
lv_obj_t * ui_dBSwitchLabel = NULL;
lv_obj_t * ui_dBBackBtn = NULL;

static lv_timer_t * dB_ui_timer = NULL;

static void dB_ui_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (!dB_get_latest_valid()) return;

    char buf[16];
    float v = dB_get_latest_value();
    snprintf(buf, sizeof(buf), "%.1f dB", v);
    if (ui_dBValue) lv_label_set_text(ui_dBValue, buf);

    v = dB_get_max();
    snprintf(buf, sizeof(buf), "%.1f", v);
    if (ui_dBMaxVal) lv_label_set_text(ui_dBMaxVal, buf);

    v = dB_get_min();
    snprintf(buf, sizeof(buf), "%.1f", v);
    if (ui_dBMinVal) lv_label_set_text(ui_dBMinVal, buf);

    v = dB_get_avg();
    snprintf(buf, sizeof(buf), "%.1f", v);
    if (ui_dBAvgVal) lv_label_set_text(ui_dBAvgVal, buf);
}

static lv_obj_t * create_card(lv_obj_t * parent, int y, int h)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 230, h);
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_align(card, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    return card;
}

void ui_event_detialDB(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main03, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, &ui_main03_screen_init);
    }
}

void ui_event_dBBackBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_main03, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_main03_screen_init);
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

    // ── 标题栏 ──
    lv_obj_t * title_panel = lv_obj_create(ui_detialDB);
    lv_obj_set_size(title_panel, 230, 36);
    lv_obj_set_pos(title_panel, 0, 5);
    lv_obj_set_align(title_panel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * title = lv_label_create(title_panel);
    lv_obj_center(title);
    lv_label_set_text(title, "Sound Level");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 实时数据卡片 ──
    lv_obj_t * data_card = create_card(ui_detialDB, 48, 74);

    lv_obj_t * current_label = lv_label_create(data_card);
    lv_obj_set_pos(current_label, 0, -8);
    lv_obj_set_align(current_label, LV_ALIGN_CENTER);
    lv_label_set_text(current_label, "Current Level");
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(current_label, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBValue = lv_label_create(data_card);
    lv_obj_set_pos(ui_dBValue, 0, 14);
    lv_obj_set_align(ui_dBValue, LV_ALIGN_CENTER);
    lv_label_set_text(ui_dBValue, "--.- dB");
    lv_obj_set_style_text_font(ui_dBValue, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_dBValue, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 统计卡片（未来扩展） ──
    lv_obj_t * stats_card = create_card(ui_detialDB, 128, 58);
    lv_obj_set_flex_flow(stats_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stats_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * row1 = lv_obj_create(stats_card);
    lv_obj_set_size(row1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row1, 0, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);

    lv_obj_t * max_lbl = lv_label_create(row1);
    lv_label_set_text(max_lbl, "Max:");
    lv_obj_set_style_text_font(max_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(max_lbl, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBMaxVal = lv_label_create(row1);
    lv_label_set_text(ui_dBMaxVal, "--.-");
    lv_obj_set_style_text_font(ui_dBMaxVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_dBMaxVal, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * min_lbl = lv_label_create(row1);
    lv_obj_set_style_pad_left(min_lbl, 30, 0);
    lv_label_set_text(min_lbl, "Min:");
    lv_obj_set_style_text_font(min_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(min_lbl, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBMinVal = lv_label_create(row1);
    lv_label_set_text(ui_dBMinVal, "--.-");
    lv_obj_set_style_text_font(ui_dBMinVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_dBMinVal, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * row2 = lv_obj_create(stats_card);
    lv_obj_set_size(row2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row2, 0, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);

    lv_obj_t * avg_lbl = lv_label_create(row2);
    lv_label_set_text(avg_lbl, "Avg:");
    lv_obj_set_style_text_font(avg_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(avg_lbl, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dBAvgVal = lv_label_create(row2);
    lv_label_set_text(ui_dBAvgVal, "--.-");
    lv_obj_set_style_text_font(ui_dBAvgVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_dBAvgVal, lv_color_hex(0x5F6368), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 开关卡片 ──
    lv_obj_t * switch_card = create_card(ui_detialDB, 192, 46);

    ui_dBSwitchLabel = lv_label_create(switch_card);
    lv_obj_set_pos(ui_dBSwitchLabel, 8, 0);
    lv_obj_set_align(ui_dBSwitchLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_dBSwitchLabel, "Sensor: ON");
    lv_obj_set_style_text_font(ui_dBSwitchLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Switch2 = lv_switch_create(switch_card);
    lv_obj_set_size(ui_Switch2, 50, 25);
    lv_obj_set_pos(ui_Switch2, -8, 0);
    lv_obj_set_align(ui_Switch2, LV_ALIGN_RIGHT_MID);
    lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);

    // ── 返回按钮 ──
    ui_dBBackBtn = lv_button_create(ui_detialDB);
    lv_obj_set_size(ui_dBBackBtn, 180, 40);
    lv_obj_set_pos(ui_dBBackBtn, 0, 248);
    lv_obj_set_align(ui_dBBackBtn, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(ui_dBBackBtn, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_dBBackBtn, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dBBackBtn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_dBBackBtn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_dBBackBtn, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_label = lv_label_create(ui_dBBackBtn);
    lv_obj_center(btn_label);
    lv_label_set_text(btn_label, "Back");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 事件 ──
    lv_obj_add_event_cb(ui_detialDB, ui_event_detialDB, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_Switch2, ui_event_Switch2, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_dBBackBtn, ui_event_dBBackBtn, LV_EVENT_CLICKED, NULL);

    // ── 数据刷新定时器 ──
    dB_ui_timer = lv_timer_create(dB_ui_timer_cb, 500, NULL);

    dB_start();
}

void ui_detialDB_screen_destroy(void)
{
    if (dB_ui_timer) {
        lv_timer_del(dB_ui_timer);
        dB_ui_timer = NULL;
    }

    if(ui_detialDB) lv_obj_del(ui_detialDB);

    ui_detialDB = NULL;
    ui_dBValue = NULL;
    ui_dBMaxVal = NULL;
    ui_dBMinVal = NULL;
    ui_dBAvgVal = NULL;
    ui_Switch2 = NULL;
    ui_dBSwitchLabel = NULL;
    ui_dBBackBtn = NULL;
}
