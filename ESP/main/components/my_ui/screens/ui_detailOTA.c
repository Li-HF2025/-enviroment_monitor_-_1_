#include <stddef.h>
#include "lvgl/lvgl.h"
#include "../ui.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "my_wifi.h"
#include "my_ota.h"

lv_obj_t * ui_detailOTA = NULL;
lv_obj_t * ui_otaTitlePanel = NULL;
lv_obj_t * ui_otaTitleLabel = NULL;
lv_obj_t * ui_versionPanel = NULL;
lv_obj_t * ui_versionLabel = NULL;
lv_obj_t * ui_versionValue = NULL;
lv_obj_t * ui_targetLabel = NULL;
lv_obj_t * ui_targetValue = NULL;
lv_obj_t * ui_sizeLabel = NULL;
lv_obj_t * ui_sizeValue = NULL;
lv_obj_t * ui_checkUpdateButton = NULL;
lv_obj_t * ui_checkUpdateButtonLabel = NULL;
lv_obj_t * ui_otaStatusLabel = NULL;
lv_obj_t * ui_progressBar = NULL;
lv_obj_t * ui_progressLabel = NULL;
static lv_timer_t * s_ota_status_clear_timer = NULL;
static lv_timer_t * s_ota_progress_timer = NULL;

static void ui_ota_status_clear_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (ui_otaStatusLabel != NULL) {
        lv_label_set_text(ui_otaStatusLabel, "");
    }
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }
}

static void ui_ota_progress_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (!ota_is_running()) {
        return;
    }

    int progress = ota_get_progress();

    if (lv_obj_has_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);
    }

    lv_bar_set_value(ui_progressBar, progress, LV_ANIM_ON);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", progress);
    lv_label_set_text(ui_progressLabel, buf);

    /* 下载进行中时不自动清除状态文字 */
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }
}

static void ui_ota_show_status_with_timeout(const char * status_text)
{
    if (ui_otaStatusLabel == NULL) {
        return;
    }

    lv_label_set_text(ui_otaStatusLabel, status_text);

    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }

    s_ota_status_clear_timer = lv_timer_create(ui_ota_status_clear_cb, 5000, NULL);
}

void ui_event_checkUpdate(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        wifi_ap_record_t ap_info;
        esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
        if (err == ESP_OK) {
            ui_ota_show_status_with_timeout("Checking updates...");
            if (onenet_ota_upload_version() == ESP_OK) {
                if(onenet_ota_check_task("2", ota_get_current_version()) == ESP_OK) {
                    lv_label_set_text(ui_targetValue, ota_get_target_version());

                    int size = ota_get_firmware_size();
                    if (size > 0) {
                        char size_buf[24];
                        if (size >= 1024 * 1024) {
                            snprintf(size_buf, sizeof(size_buf), "%.2f MB", size / (1024.0f * 1024.0f));
                        } else if (size >= 1024) {
                            snprintf(size_buf, sizeof(size_buf), "%d KB", size / 1024);
                        } else {
                            snprintf(size_buf, sizeof(size_buf), "%d B", size);
                        }
                        lv_label_set_text(ui_sizeValue, size_buf);
                    }

                    ui_ota_show_status_with_timeout("Update found, downloading...");
                    /* 显示进度条并启动进度轮询 */
                    if (lv_obj_has_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN)) {
                        lv_obj_remove_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_remove_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);
                    }
                    lv_bar_set_value(ui_progressBar, 0, LV_ANIM_OFF);
                    lv_label_set_text(ui_progressLabel, "0%");
                    if (s_ota_progress_timer == NULL) {
                        s_ota_progress_timer = lv_timer_create(ui_ota_progress_timer_cb, 500, NULL);
                    }
                    ato_start();
                } else {
                    ui_ota_show_status_with_timeout("No updates available");
                }
            } else {
                ui_ota_show_status_with_timeout("Failed to check updates");
            }
        } else {
            ui_ota_show_status_with_timeout("Wi-Fi not connected");
        }
    }
}

void ui_event_detailOTA(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_setting, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_setting_screen_init);
    }
}

void ui_detailOTA_screen_init(void)
{
    ui_detailOTA = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_detailOTA, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_detailOTA, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_detailOTA, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 顶部标题栏 ──
    ui_otaTitlePanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_size(ui_otaTitlePanel, 230, 36);
    lv_obj_set_pos(ui_otaTitlePanel, 0, 5);
    lv_obj_set_align(ui_otaTitlePanel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_otaTitlePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_otaTitlePanel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_otaTitleLabel = lv_label_create(ui_otaTitlePanel);
    lv_obj_center(ui_otaTitleLabel);
    lv_label_set_text(ui_otaTitleLabel, "OTA Update");
    lv_obj_set_style_text_font(ui_otaTitleLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 版本信息卡片 ──
    ui_versionPanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_size(ui_versionPanel, 230, 90);
    lv_obj_set_pos(ui_versionPanel, 0, 48);
    lv_obj_set_align(ui_versionPanel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_versionPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_versionPanel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_versionPanel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_versionPanel, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_versionPanel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ui_versionPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_versionPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Current 行
    lv_obj_t * row_current = lv_obj_create(ui_versionPanel);
    lv_obj_set_size(row_current, LV_SIZE_CONTENT, 20);
    lv_obj_remove_flag(row_current, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_current, 0, 0);
    lv_obj_set_style_border_width(row_current, 0, 0);
    lv_obj_set_style_pad_all(row_current, 0, 0);
    lv_obj_set_flex_flow(row_current, LV_FLEX_FLOW_ROW);

    ui_versionLabel = lv_label_create(row_current);
    lv_label_set_text(ui_versionLabel, "Current:");
    lv_obj_set_style_text_font(ui_versionLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_versionValue = lv_label_create(row_current);
    lv_obj_set_style_text_font(ui_versionValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_versionValue, ota_get_current_version());
    lv_obj_set_style_text_color(ui_versionValue, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Target 行
    lv_obj_t * row_target = lv_obj_create(ui_versionPanel);
    lv_obj_set_size(row_target, LV_SIZE_CONTENT, 20);
    lv_obj_remove_flag(row_target, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_target, 0, 0);
    lv_obj_set_style_border_width(row_target, 0, 0);
    lv_obj_set_style_pad_all(row_target, 0, 0);
    lv_obj_set_flex_flow(row_target, LV_FLEX_FLOW_ROW);

    ui_targetLabel = lv_label_create(row_target);
    lv_label_set_text(ui_targetLabel, "Target:");
    lv_obj_set_style_text_font(ui_targetLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_targetValue = lv_label_create(row_target);
    lv_label_set_text(ui_targetValue, "--");
    lv_obj_set_style_text_font(ui_targetValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_targetValue, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Size 行
    lv_obj_t * row_size = lv_obj_create(ui_versionPanel);
    lv_obj_set_size(row_size, LV_SIZE_CONTENT, 20);
    lv_obj_remove_flag(row_size, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_size, 0, 0);
    lv_obj_set_style_border_width(row_size, 0, 0);
    lv_obj_set_style_pad_all(row_size, 0, 0);
    lv_obj_set_flex_flow(row_size, LV_FLEX_FLOW_ROW);

    ui_sizeLabel = lv_label_create(row_size);
    lv_label_set_text(ui_sizeLabel, "Size:");
    lv_obj_set_style_text_font(ui_sizeLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sizeValue = lv_label_create(row_size);
    lv_label_set_text(ui_sizeValue, "--");
    lv_obj_set_style_text_font(ui_sizeValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_sizeValue, lv_color_hex(0x5F6368), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 进度条（初始隐藏） ──
    ui_progressBar = lv_bar_create(ui_detailOTA);
    lv_obj_set_size(ui_progressBar, 210, 22);
    lv_obj_set_pos(ui_progressBar, 0, 145);
    lv_obj_set_align(ui_progressBar, LV_ALIGN_TOP_MID);
    lv_bar_set_range(ui_progressBar, 0, 100);
    lv_bar_set_value(ui_progressBar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ui_progressBar, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_progressBar, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_progressBar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_progressBar, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_progressBar, lv_color_hex(0x1A73E8), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_progressLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_pos(ui_progressLabel, 0, 172);
    lv_obj_set_align(ui_progressLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_progressLabel, "0%");
    lv_obj_set_style_text_font(ui_progressLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_progressLabel, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);

    // ── 检查更新按钮 ──
    ui_checkUpdateButton = lv_button_create(ui_detailOTA);
    lv_obj_set_size(ui_checkUpdateButton, 200, 44);
    lv_obj_set_pos(ui_checkUpdateButton, 0, 190);
    lv_obj_set_align(ui_checkUpdateButton, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(ui_checkUpdateButton, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_checkUpdateButton, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_checkUpdateButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_checkUpdateButton, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_checkUpdateButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_checkUpdateButtonLabel = lv_label_create(ui_checkUpdateButton);
    lv_obj_center(ui_checkUpdateButtonLabel);
    lv_label_set_text(ui_checkUpdateButtonLabel, "Check Update");
    lv_obj_set_style_text_font(ui_checkUpdateButtonLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_checkUpdateButtonLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 状态信息 ──
    ui_otaStatusLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_otaStatusLabel, 0, 245);
    lv_obj_set_align(ui_otaStatusLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_otaStatusLabel, "");
    lv_obj_set_style_text_font(ui_otaStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── 进度轮询定时器 ──
    s_ota_progress_timer = lv_timer_create(ui_ota_progress_timer_cb, 500, NULL);

    lv_obj_add_event_cb(ui_checkUpdateButton, ui_event_checkUpdate, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_detailOTA, ui_event_detailOTA, LV_EVENT_GESTURE, NULL);
}

void ui_detailOTA_screen_destroy(void)
{
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }
    if (s_ota_progress_timer != NULL) {
        lv_timer_del(s_ota_progress_timer);
        s_ota_progress_timer = NULL;
    }

    if(ui_detailOTA) lv_obj_del(ui_detailOTA);

    ui_detailOTA = NULL;
    ui_otaTitlePanel = NULL;
    ui_otaTitleLabel = NULL;
    ui_versionPanel = NULL;
    ui_versionLabel = NULL;
    ui_versionValue = NULL;
    ui_targetLabel = NULL;
    ui_targetValue = NULL;
    ui_sizeLabel = NULL;
    ui_sizeValue = NULL;
    ui_checkUpdateButton = NULL;
    ui_checkUpdateButtonLabel = NULL;
    ui_otaStatusLabel = NULL;
    ui_progressBar = NULL;
    ui_progressLabel = NULL;
}
