#include "../ui.h"
#include "detail_temp_logic.h"
#include "lvgl.h"

lv_obj_t * ui_detailTemperature = NULL;
lv_obj_t * ui_tempValue = NULL;
lv_obj_t * ui_humidityValue = NULL;
lv_obj_t * ui_tempMaxVal = NULL;
lv_obj_t * ui_tempMinVal = NULL;
lv_obj_t * ui_tempAvgVal = NULL;
lv_obj_t * ui_Switch1 = NULL;
lv_obj_t * ui_tempSwitchLabel = NULL;
lv_obj_t * ui_tempBackBtn = NULL;

static lv_timer_t * temp_ui_timer = NULL;

static void temp_ui_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (!temp_get_latest_valid()) return;

    char buf[16];
    float t = temp_get_latest_value();
    float h = temp_get_latest_humidity();

    snprintf(buf, sizeof(buf), "%.1f C", t);
    if (ui_tempValue) lv_label_set_text(ui_tempValue, buf);

    snprintf(buf, sizeof(buf), "%.1f %%", h);
    if (ui_humidityValue) lv_label_set_text(ui_humidityValue, buf);

    t = temp_get_max();
    snprintf(buf, sizeof(buf), "%.1f", t);
    if (ui_tempMaxVal) lv_label_set_text(ui_tempMaxVal, buf);

    t = temp_get_min();
    snprintf(buf, sizeof(buf), "%.1f", t);
    if (ui_tempMinVal) lv_label_set_text(ui_tempMinVal, buf);

    t = temp_get_avg();
    snprintf(buf, sizeof(buf), "%.1f", t);
    if (ui_tempAvgVal) lv_label_set_text(ui_tempAvgVal, buf);
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

void ui_event_detailTemperature(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_main02, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_main02_screen_init);
    }
}

void ui_event_tempBackBtn(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _ui_screen_change(&ui_main02, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_main02_screen_init);
    }
}

void ui_event_Switch1(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * target = lv_event_get_target(e);
        bool is_on = lv_obj_has_state(target, LV_STATE_CHECKED);
        if(is_on){
            temp_init();
            lv_label_set_text(ui_tempSwitchLabel, "Sensor: ON");
        }else{
            temp_deinit();
            lv_label_set_text(ui_tempSwitchLabel, "Sensor: OFF");
        }
    }
}

void ui_detailTemperature_screen_init(void)
{
    ui_detailTemperature = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_detailTemperature, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_detailTemperature, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_detailTemperature, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 标题栏 ──
    lv_obj_t * title_panel = lv_obj_create(ui_detailTemperature);
    lv_obj_set_size(title_panel, 230, 36);
    lv_obj_set_pos(title_panel, 0, 5);
    lv_obj_set_align(title_panel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(title_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * title = lv_label_create(title_panel);
    lv_obj_center(title);
    lv_label_set_text(title, "Temperature & Humidity");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 实时数据卡片（温度 + 湿度双列） ──
    lv_obj_t * data_card = create_card(ui_detailTemperature, 48, 68);

    lv_obj_t * sep = lv_obj_create(data_card);
    lv_obj_set_size(sep, 2, 40);
    lv_obj_set_pos(sep, 0, 0);
    lv_obj_set_align(sep, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* 温度 - 左侧 */
    lv_obj_t * temp_label = lv_label_create(data_card);
    lv_obj_set_pos(temp_label, -48, -10);
    lv_obj_set_align(temp_label, LV_ALIGN_CENTER);
    lv_label_set_text(temp_label, "Temperature");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tempValue = lv_label_create(data_card);
    lv_obj_set_pos(ui_tempValue, -48, 12);
    lv_obj_set_align(ui_tempValue, LV_ALIGN_CENTER);
    lv_label_set_text(ui_tempValue, "--.- C");
    lv_obj_set_style_text_font(ui_tempValue, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_tempValue, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 湿度 - 右侧 */
    lv_obj_t * hum_label = lv_label_create(data_card);
    lv_obj_set_pos(hum_label, 48, -10);
    lv_obj_set_align(hum_label, LV_ALIGN_CENTER);
    lv_label_set_text(hum_label, "Humidity");
    lv_obj_set_style_text_font(hum_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hum_label, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_humidityValue = lv_label_create(data_card);
    lv_obj_set_pos(ui_humidityValue, 48, 12);
    lv_obj_set_align(ui_humidityValue, LV_ALIGN_CENTER);
    lv_label_set_text(ui_humidityValue, "--.- %");
    lv_obj_set_style_text_font(ui_humidityValue, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_humidityValue, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 统计卡片（未来扩展：最大/最小/平均） ──
    lv_obj_t * stats_card = create_card(ui_detailTemperature, 122, 58);
    lv_obj_set_flex_flow(stats_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stats_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 第一行：Max / Min */
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

    ui_tempMaxVal = lv_label_create(row1);
    lv_label_set_text(ui_tempMaxVal, "--.-");
    lv_obj_set_style_text_font(ui_tempMaxVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_tempMaxVal, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * min_lbl = lv_label_create(row1);
    lv_obj_set_style_pad_left(min_lbl, 30, 0);
    lv_label_set_text(min_lbl, "Min:");
    lv_obj_set_style_text_font(min_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(min_lbl, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_tempMinVal = lv_label_create(row1);
    lv_label_set_text(ui_tempMinVal, "--.-");
    lv_obj_set_style_text_font(ui_tempMinVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_tempMinVal, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 第二行：Avg */
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

    ui_tempAvgVal = lv_label_create(row2);
    lv_label_set_text(ui_tempAvgVal, "--.-");
    lv_obj_set_style_text_font(ui_tempAvgVal, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_tempAvgVal, lv_color_hex(0x5F6368), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 开关卡片 ──
    lv_obj_t * switch_card = create_card(ui_detailTemperature, 186, 46);

    ui_tempSwitchLabel = lv_label_create(switch_card);
    lv_obj_set_pos(ui_tempSwitchLabel, 8, 0);
    lv_obj_set_align(ui_tempSwitchLabel, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_tempSwitchLabel, "Sensor: ON");
    lv_obj_set_style_text_font(ui_tempSwitchLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Switch1 = lv_switch_create(switch_card);
    lv_obj_set_size(ui_Switch1, 50, 25);
    lv_obj_set_pos(ui_Switch1, -8, 0);
    lv_obj_set_align(ui_Switch1, LV_ALIGN_RIGHT_MID);
    lv_obj_add_state(ui_Switch1, LV_STATE_CHECKED);

    // ── 返回按钮 ──
    ui_tempBackBtn = lv_button_create(ui_detailTemperature);
    lv_obj_set_size(ui_tempBackBtn, 180, 40);
    lv_obj_set_pos(ui_tempBackBtn, 0, 242);
    lv_obj_set_align(ui_tempBackBtn, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(ui_tempBackBtn, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_tempBackBtn, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_tempBackBtn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_tempBackBtn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_tempBackBtn, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_label = lv_label_create(ui_tempBackBtn);
    lv_obj_center(btn_label);
    lv_label_set_text(btn_label, "Back");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 事件 ──
    lv_obj_add_event_cb(ui_detailTemperature, ui_event_detailTemperature, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_Switch1, ui_event_Switch1, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_tempBackBtn, ui_event_tempBackBtn, LV_EVENT_CLICKED, NULL);

    // ── 数据刷新定时器 ──
    temp_ui_timer = lv_timer_create(temp_ui_timer_cb, 500, NULL);

    temp_start();
}

void ui_detailTemperature_screen_destroy(void)
{
    if (temp_ui_timer) {
        lv_timer_del(temp_ui_timer);
        temp_ui_timer = NULL;
    }

    if(ui_detailTemperature) lv_obj_del(ui_detailTemperature);

    ui_detailTemperature = NULL;
    ui_tempValue = NULL;
    ui_humidityValue = NULL;
    ui_tempMaxVal = NULL;
    ui_tempMinVal = NULL;
    ui_tempAvgVal = NULL;
    ui_Switch1 = NULL;
    ui_tempSwitchLabel = NULL;
    ui_tempBackBtn = NULL;
}
