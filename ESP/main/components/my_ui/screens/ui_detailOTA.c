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
lv_obj_t * ui_checkUpdateButton = NULL;
lv_obj_t * ui_checkUpdateButtonLabel = NULL;
lv_obj_t * ui_otaStatusLabel = NULL;
static lv_timer_t * s_ota_status_clear_timer = NULL;

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
                char * version;
                char * type;
                //@TODO:根据平台接口返回的结果，输出选择更新的提示，或者直接开始更新
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

    ui_otaTitlePanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_width(ui_otaTitlePanel, 230);
    lv_obj_set_height(ui_otaTitlePanel, 42);
    lv_obj_set_x(ui_otaTitlePanel, 5);
    lv_obj_set_y(ui_otaTitlePanel, 10);
    lv_obj_remove_flag(ui_otaTitlePanel, LV_OBJ_FLAG_SCROLLABLE);

    ui_otaTitleLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_otaTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_otaTitleLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_otaTitleLabel, 18);
    lv_obj_set_y(ui_otaTitleLabel, 20);
    lv_label_set_text(ui_otaTitleLabel, "     OTA Update");
    lv_obj_set_style_text_font(ui_otaTitleLabel, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_versionPanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_width(ui_versionPanel, 230);
    lv_obj_set_height(ui_versionPanel, 52);
    lv_obj_set_x(ui_versionPanel, 5);
    lv_obj_set_y(ui_versionPanel, 62);
    lv_obj_remove_flag(ui_versionPanel, LV_OBJ_FLAG_SCROLLABLE);

    ui_versionLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_versionLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_versionLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_versionLabel, 18);
    lv_obj_set_y(ui_versionLabel, 74);
    lv_label_set_text(ui_versionLabel, "Current:");
    lv_obj_set_style_text_font(ui_versionLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_versionValue = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_versionValue, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_versionValue, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_versionValue, 110);
    lv_obj_set_y(ui_versionValue, 74);
    lv_label_set_text(ui_versionValue, "XX.XX");
    lv_obj_set_style_text_font(ui_versionValue, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_checkUpdateButton = lv_button_create(ui_detailOTA);
    lv_obj_set_width(ui_checkUpdateButton, 150);
    lv_obj_set_height(ui_checkUpdateButton, 44);
    lv_obj_set_x(ui_checkUpdateButton, 0);
    lv_obj_set_y(ui_checkUpdateButton, 125);
    lv_obj_set_align(ui_checkUpdateButton, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_checkUpdateButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_checkUpdateButton, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_checkUpdateButton, lv_color_hex(0xB7F7F8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_checkUpdateButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_checkUpdateButtonLabel = lv_label_create(ui_checkUpdateButton);
    lv_obj_set_width(ui_checkUpdateButtonLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_checkUpdateButtonLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_checkUpdateButtonLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_checkUpdateButtonLabel, "updates");
    lv_obj_set_style_text_font(ui_checkUpdateButtonLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_otaStatusLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_otaStatusLabel, 0);
    lv_obj_set_y(ui_otaStatusLabel, 15);
    lv_obj_set_align(ui_otaStatusLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_otaStatusLabel, "");
    lv_obj_set_style_text_font(ui_otaStatusLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_checkUpdateButton, ui_event_checkUpdate, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_detailOTA, ui_event_detailOTA, LV_EVENT_GESTURE, NULL);
}

void ui_detailOTA_screen_destroy(void)
{
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }

    if(ui_detailOTA) lv_obj_del(ui_detailOTA);

    ui_detailOTA = NULL;
    ui_otaTitlePanel = NULL;
    ui_otaTitleLabel = NULL;
    ui_versionPanel = NULL;
    ui_versionLabel = NULL;
    ui_versionValue = NULL;
    ui_checkUpdateButton = NULL;
    ui_checkUpdateButtonLabel = NULL;
    ui_otaStatusLabel = NULL;
}
