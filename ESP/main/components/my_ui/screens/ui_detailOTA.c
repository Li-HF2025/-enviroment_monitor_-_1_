#include <stddef.h>
#include "lvgl/lvgl.h"
#include "../ui.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "my_wifi.h"
#include "my_ota.h"
#include "my_stm_ota.h"

/* ==========================================================================
 * Screen objects
 * ========================================================================== */

lv_obj_t * ui_detailOTA = NULL;
lv_obj_t * ui_otaTitlePanel = NULL;
lv_obj_t * ui_otaTitleLabel = NULL;

// Chip switch bar
lv_obj_t * ui_chipSwitchBar = NULL;
lv_obj_t * ui_chipEspLabel = NULL;
lv_obj_t * ui_chipStmLabel = NULL;
lv_obj_t * ui_chipEspDot = NULL;
lv_obj_t * ui_chipStmDot = NULL;

// Version info card
lv_obj_t * ui_versionPanel = NULL;
lv_obj_t * ui_versionLabel = NULL;
lv_obj_t * ui_versionValue = NULL;
lv_obj_t * ui_targetLabel = NULL;
lv_obj_t * ui_targetValue = NULL;
lv_obj_t * ui_sizeLabel = NULL;
lv_obj_t * ui_sizeValue = NULL;
lv_obj_t * ui_previousLabel = NULL;
lv_obj_t * ui_previousValue = NULL;
lv_obj_t * ui_previousRow = NULL;

// Single action button: Check or Update (mutually exclusive, same position)
lv_obj_t * ui_actionButton = NULL;
lv_obj_t * ui_actionButtonLabel = NULL;
lv_obj_t * ui_rollbackButton = NULL;
lv_obj_t * ui_rollbackButtonLabel = NULL;

// Progress & status
lv_obj_t * ui_progressBar = NULL;
lv_obj_t * ui_progressLabel = NULL;
lv_obj_t * ui_otaStatusLabel = NULL;

/* ==========================================================================
 * Module state
 * ========================================================================== */

typedef enum { OTA_CHIP_ESP32 = 0, OTA_CHIP_STM32 = 1 } ota_chip_t;
static ota_chip_t s_current_chip = OTA_CHIP_ESP32;

static bool s_esp32_has_update = false;
static bool s_stm32_has_update = false;

static lv_timer_t * s_ota_status_clear_timer = NULL;
static lv_timer_t * s_ota_progress_timer = NULL;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void ui_ota_status_clear_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (ui_otaStatusLabel != NULL) lv_label_set_text(ui_otaStatusLabel, "");
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }
}

static void ui_ota_show_status(const char * status_text)
{
    if (ui_otaStatusLabel == NULL) return;
    lv_label_set_text(ui_otaStatusLabel, status_text);
    if (s_ota_status_clear_timer != NULL) {
        lv_timer_del(s_ota_status_clear_timer);
        s_ota_status_clear_timer = NULL;
    }
    s_ota_status_clear_timer = lv_timer_create(ui_ota_status_clear_cb, 5000, NULL);
}

static const char *format_size_str(int size, char *buf, size_t buf_len)
{
    if (size <= 0) return "--";
    if (size >= 1024 * 1024) snprintf(buf, buf_len, "%.2f MB", size / (1024.0f * 1024.0f));
    else if (size >= 1024)   snprintf(buf, buf_len, "%d KB", size / 1024);
    else                     snprintf(buf, buf_len, "%d B", size);
    return buf;
}

/* ==========================================================================
 * Chip switch bar — visual style
 * ========================================================================== */

static void ui_chip_switch_style_selected(lv_obj_t * label)
{
    lv_obj_set_style_bg_color(label, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ui_chip_switch_style_unselected(lv_obj_t * label)
{
    lv_obj_set_style_bg_color(label, lv_color_hex(0xD0D0D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0x5F6368), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ui_chip_switch_update_styles(void)
{
    if (s_current_chip == OTA_CHIP_ESP32) {
        ui_chip_switch_style_selected(ui_chipEspLabel);
        ui_chip_switch_style_unselected(ui_chipStmLabel);
    } else {
        ui_chip_switch_style_unselected(ui_chipEspLabel);
        ui_chip_switch_style_selected(ui_chipStmLabel);
    }
}

static void ui_chip_update_dots(void)
{
    if (ui_chipEspDot) {
        if (s_esp32_has_update) lv_obj_remove_flag(ui_chipEspDot, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_add_flag(ui_chipEspDot, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_chipStmDot) {
        if (s_stm32_has_update) lv_obj_remove_flag(ui_chipStmDot, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_add_flag(ui_chipStmDot, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ==========================================================================
 * Card refresh — updates labels, action button, rollback for current chip
 * ========================================================================== */

static void ui_ota_refresh_card(void)
{
    bool has_update;
    const char *chip_name, *target_str;
    int fw_size;

    ui_chip_switch_update_styles();

    if (s_current_chip == OTA_CHIP_ESP32) {
        has_update = s_esp32_has_update;
        chip_name  = "ESP32";
        lv_label_set_text(ui_versionValue, ota_get_current_version());
        target_str = ota_get_target_version();
        fw_size    = ota_get_firmware_size();

        if (ui_previousRow) lv_obj_remove_flag(ui_previousRow, LV_OBJ_FLAG_HIDDEN);
        const char *prev = ota_get_previous_version();
        lv_label_set_text(ui_previousValue, (prev && prev[0]) ? prev : "--");

        if (prev && prev[0]) {
            lv_obj_remove_flag(ui_rollbackButton, LV_OBJ_FLAG_HIDDEN);
            char rb[48];
            snprintf(rb, sizeof(rb), "Rollback to %s", prev);
            lv_label_set_text(ui_rollbackButtonLabel, rb);
        } else {
            lv_obj_add_flag(ui_rollbackButton, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        has_update = s_stm32_has_update;
        chip_name  = "STM32";
        lv_label_set_text(ui_versionValue, stm_ota_get_stm_version());
        target_str = stm_ota_get_target_version();
        fw_size    = stm_ota_get_firmware_size();

        if (ui_previousRow) lv_obj_add_flag(ui_previousRow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_rollbackButton, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(ui_targetValue, (target_str && target_str[0]) ? target_str : "--");

    char size_buf[24];
    lv_label_set_text(ui_sizeValue, format_size_str(fw_size, size_buf, sizeof(size_buf)));

    /* ── Action button: "Check X" or "Update X to vX.X.X" ── */
    if (has_update) {
        target_str = (s_current_chip == OTA_CHIP_ESP32)
            ? ota_get_target_version() : stm_ota_get_target_version();
        char label[48];
        snprintf(label, sizeof(label), "Update %s to %s", chip_name,
                 (target_str && target_str[0]) ? target_str : "?");
        lv_label_set_text(ui_actionButtonLabel, label);
        lv_obj_set_style_bg_color(ui_actionButton, lv_color_hex(0x0D9040),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        char label[32];
        snprintf(label, sizeof(label), "Check %s", chip_name);
        lv_label_set_text(ui_actionButtonLabel, label);
        lv_obj_set_style_bg_color(ui_actionButton, lv_color_hex(0x1A73E8),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    ui_chip_update_dots();
}

/* ==========================================================================
 * Chip switch event handlers
 * ========================================================================== */

void ui_event_chipSwitchEsp(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_current_chip == OTA_CHIP_ESP32) return;
    s_current_chip = OTA_CHIP_ESP32;
    ui_ota_refresh_card();
}

void ui_event_chipSwitchStm(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_current_chip == OTA_CHIP_STM32) return;
    s_current_chip = OTA_CHIP_STM32;
    ui_ota_refresh_card();
}

/* ==========================================================================
 * Unified action button — Check or Update based on has_update flag
 * ========================================================================== */

void ui_event_checkUpdate(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ui_ota_show_status("Wi-Fi not connected");
        return;
    }

    bool has_update = (s_current_chip == OTA_CHIP_ESP32)
        ? s_esp32_has_update : s_stm32_has_update;

    if (has_update) {
        /* ================================================================
         * Phase 2: start update
         * ================================================================ */
        lv_obj_add_state(ui_actionButton, LV_STATE_DISABLED);
        lv_obj_add_state(ui_rollbackButton, LV_STATE_DISABLED);
        lv_obj_add_state(ui_chipEspLabel, LV_STATE_DISABLED);
        lv_obj_add_state(ui_chipStmLabel, LV_STATE_DISABLED);

        /* Hide action button, show progress in its place */
        lv_obj_add_flag(ui_actionButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(ui_progressBar, 0, LV_ANIM_OFF);
        lv_label_set_text(ui_progressLabel, "0%");

        if (s_ota_status_clear_timer) {
            lv_timer_del(s_ota_status_clear_timer);
            s_ota_status_clear_timer = NULL;
        }

        if (s_current_chip == OTA_CHIP_ESP32) {
            ui_ota_show_status("ESP32 OTA starting...");
            ato_start();
        } else {
            ui_ota_show_status("STM32 OTA starting...");
            stm_ota_start();
        }
        return;
    }

    /* ================================================================
     * Phase 1: check for update
     * ================================================================ */
    lv_obj_add_state(ui_actionButton, LV_STATE_DISABLED);
    ui_ota_show_status("Checking...");

    if (s_current_chip == OTA_CHIP_ESP32) {
        if (onenet_ota_upload_version_separate(
                ota_get_current_version(),
                stm_ota_get_stm_version()) != ESP_OK) {
            ui_ota_show_status("Version upload failed");
            lv_obj_clear_state(ui_actionButton, LV_STATE_DISABLED);
            return;
        }
        if (onenet_ota_check_task("1", ota_get_current_version()) != ESP_OK) {
            ui_ota_show_status("No ESP32 update available");
            s_esp32_has_update = false;
            lv_obj_clear_state(ui_actionButton, LV_STATE_DISABLED);
            ui_ota_refresh_card();
            return;
        }
        s_esp32_has_update = true;
        ui_ota_show_status("ESP32 update found");
    } else {
        if (onenet_ota_upload_version_separate(
                ota_get_current_version(),
                stm_ota_get_stm_version()) != ESP_OK) {
            ui_ota_show_status("Version upload failed");
            lv_obj_clear_state(ui_actionButton, LV_STATE_DISABLED);
            return;
        }
        if (stm_ota_check_task(stm_ota_get_stm_version()) != ESP_OK) {
            ui_ota_show_status("No STM32 update available");
            s_stm32_has_update = false;
            lv_obj_clear_state(ui_actionButton, LV_STATE_DISABLED);
            ui_ota_refresh_card();
            return;
        }
        s_stm32_has_update = true;
        ui_ota_show_status("STM32 update found");
    }

    lv_obj_clear_state(ui_actionButton, LV_STATE_DISABLED);
    ui_ota_refresh_card();
}

/* ==========================================================================
 * Rollback — ESP32 only
 * ========================================================================== */

void ui_event_rollback(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *prev_ver = ota_get_previous_version();
    if (prev_ver == NULL || prev_ver[0] == '\0') {
        ui_ota_show_status("No previous version");
        return;
    }
    ui_ota_show_status("Rolling back...");
    if (ota_rollback_to_previous() != ESP_OK) ui_ota_show_status("Rollback failed");
}

/* ==========================================================================
 * Screen gesture
 * ========================================================================== */

void ui_event_detailOTA(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        _ui_screen_change(&ui_setting, LV_SCR_LOAD_ANIM_NONE, 250, 0, &ui_setting_screen_init);
    }
}

/* ==========================================================================
 * Progress timer
 * ========================================================================== */

static void ui_ota_progress_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    int progress = 0;
    const char *label = "";

    bool is_running = false;
    if (ota_is_running())          { progress = ota_get_progress(); label = "ESP32";  is_running = true; }
    else if (stm_ota_is_running()) { progress = stm_ota_get_progress(); label = "STM32"; is_running = true; }

    static bool s_was_running = false;

    if (is_running) {
        s_was_running = true;
        if (lv_obj_has_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_actionButton, LV_OBJ_FLAG_HIDDEN);
        }
        lv_bar_set_value(ui_progressBar, progress, LV_ANIM_ON);

        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d%%", label, progress);
        lv_label_set_text(ui_progressLabel, buf);

        if (s_ota_status_clear_timer != NULL) {
            lv_timer_del(s_ota_status_clear_timer);
            s_ota_status_clear_timer = NULL;
        }
    } else if (s_was_running) {
        // OTA 刚刚完成 — 恢复 UI
        s_was_running = false;
        s_esp32_has_update = false;
        s_stm32_has_update = false;
        lv_obj_add_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_actionButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ui_actionButton, LV_STATE_DISABLED);
        lv_obj_remove_state(ui_rollbackButton, LV_STATE_DISABLED);
        ui_ota_refresh_card();
    }
}

/* ==========================================================================
 * Screen init
 * ========================================================================== */

void ui_detailOTA_screen_init(void)
{
    ui_detailOTA = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_detailOTA, lv_color_hex(0xE5E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_detailOTA, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Title bar (y=4, h=34) ── */
    ui_otaTitlePanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_size(ui_otaTitlePanel, 230, 34);
    lv_obj_set_pos(ui_otaTitlePanel, 0, 4);
    lv_obj_set_align(ui_otaTitlePanel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_otaTitlePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_otaTitlePanel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_otaTitleLabel = lv_label_create(ui_otaTitlePanel);
    lv_obj_center(ui_otaTitleLabel);
    lv_label_set_text(ui_otaTitleLabel, "OTA Update");
    lv_obj_set_style_text_font(ui_otaTitleLabel, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Chip switch bar (y=42, h=28) ── */
    ui_chipSwitchBar = lv_obj_create(ui_detailOTA);
    lv_obj_set_size(ui_chipSwitchBar, 210, 28);
    lv_obj_set_pos(ui_chipSwitchBar, 0, 42);
    lv_obj_set_align(ui_chipSwitchBar, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_chipSwitchBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui_chipSwitchBar, 0, 0);
    lv_obj_set_style_border_width(ui_chipSwitchBar, 0, 0);
    lv_obj_set_style_pad_all(ui_chipSwitchBar, 0, 0);
    lv_obj_set_flex_flow(ui_chipSwitchBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_chipSwitchBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_chipEspLabel = lv_label_create(ui_chipSwitchBar);
    lv_obj_set_style_pad_hor(ui_chipEspLabel, 14, 0);
    lv_obj_set_style_pad_ver(ui_chipEspLabel, 4, 0);
    lv_obj_set_style_radius(ui_chipEspLabel, 6, 0);
    lv_label_set_text(ui_chipEspLabel, "ESP32");
    lv_obj_set_style_text_font(ui_chipEspLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_chipEspLabel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_chipEspLabel, ui_event_chipSwitchEsp, LV_EVENT_CLICKED, NULL);

    ui_chipEspDot = lv_obj_create(ui_chipSwitchBar);
    lv_obj_set_size(ui_chipEspDot, 8, 8);
    lv_obj_set_style_radius(ui_chipEspDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_chipEspDot, lv_color_hex(0xE37400), 0);
    lv_obj_set_style_bg_opa(ui_chipEspDot, 255, 0);
    lv_obj_set_style_border_width(ui_chipEspDot, 0, 0);
    lv_obj_add_flag(ui_chipEspDot, LV_OBJ_FLAG_HIDDEN);

    ui_chipStmLabel = lv_label_create(ui_chipSwitchBar);
    lv_obj_set_style_pad_hor(ui_chipStmLabel, 14, 0);
    lv_obj_set_style_pad_ver(ui_chipStmLabel, 4, 0);
    lv_obj_set_style_radius(ui_chipStmLabel, 6, 0);
    lv_label_set_text(ui_chipStmLabel, "STM32");
    lv_obj_set_style_text_font(ui_chipStmLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_chipStmLabel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_chipStmLabel, ui_event_chipSwitchStm, LV_EVENT_CLICKED, NULL);

    ui_chipStmDot = lv_obj_create(ui_chipSwitchBar);
    lv_obj_set_size(ui_chipStmDot, 8, 8);
    lv_obj_set_style_radius(ui_chipStmDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_chipStmDot, lv_color_hex(0xE37400), 0);
    lv_obj_set_style_bg_opa(ui_chipStmDot, 255, 0);
    lv_obj_set_style_border_width(ui_chipStmDot, 0, 0);
    lv_obj_add_flag(ui_chipStmDot, LV_OBJ_FLAG_HIDDEN);

    /* ── Version info card (y=76, h=100, flex) ── */
    ui_versionPanel = lv_obj_create(ui_detailOTA);
    lv_obj_set_size(ui_versionPanel, 230, 100);
    lv_obj_set_pos(ui_versionPanel, 0, 76);
    lv_obj_set_align(ui_versionPanel, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(ui_versionPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_versionPanel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_versionPanel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_versionPanel, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_versionPanel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ui_versionPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_versionPanel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Current
    {
        lv_obj_t * row = lv_obj_create(ui_versionPanel);
        lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        ui_versionLabel = lv_label_create(row);
        lv_label_set_text(ui_versionLabel, "Current:");
        lv_obj_set_style_text_font(ui_versionLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_versionValue = lv_label_create(row);
        lv_obj_set_style_text_font(ui_versionValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(ui_versionValue, ota_get_current_version());
        lv_obj_set_style_text_color(ui_versionValue, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Target
    {
        lv_obj_t * row = lv_obj_create(ui_versionPanel);
        lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        ui_targetLabel = lv_label_create(row);
        lv_label_set_text(ui_targetLabel, "Target:");
        lv_obj_set_style_text_font(ui_targetLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_targetValue = lv_label_create(row);
        lv_label_set_text(ui_targetValue, "--");
        lv_obj_set_style_text_font(ui_targetValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_targetValue, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Size
    {
        lv_obj_t * row = lv_obj_create(ui_versionPanel);
        lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        ui_sizeLabel = lv_label_create(row);
        lv_label_set_text(ui_sizeLabel, "Size:");
        lv_obj_set_style_text_font(ui_sizeLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_sizeValue = lv_label_create(row);
        lv_label_set_text(ui_sizeValue, "--");
        lv_obj_set_style_text_font(ui_sizeValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_sizeValue, lv_color_hex(0x5F6368), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Previous (hidden for STM32)
    ui_previousRow = lv_obj_create(ui_versionPanel);
    lv_obj_set_size(ui_previousRow, LV_SIZE_CONTENT, 20);
    lv_obj_remove_flag(ui_previousRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui_previousRow, 0, 0);
    lv_obj_set_style_border_width(ui_previousRow, 0, 0);
    lv_obj_set_style_pad_all(ui_previousRow, 0, 0);
    lv_obj_set_flex_flow(ui_previousRow, LV_FLEX_FLOW_ROW);
    ui_previousLabel = lv_label_create(ui_previousRow);
    lv_label_set_text(ui_previousLabel, "Previous:");
    lv_obj_set_style_text_font(ui_previousLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_previousValue = lv_label_create(ui_previousRow);
    {
        const char *prev = ota_get_previous_version();
        lv_label_set_text(ui_previousValue, (prev && prev[0]) ? prev : "--");
    }
    lv_obj_set_style_text_font(ui_previousValue, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_previousValue, lv_color_hex(0x7B1FA2), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Action button (y=182, h=36) — toggles between Check / Update ── */
    #define BTN_Y  182
    #define BTN_H   36
    #define BTN_W  200

    ui_actionButton = lv_button_create(ui_detailOTA);
    lv_obj_set_size(ui_actionButton, BTN_W, BTN_H);
    lv_obj_set_pos(ui_actionButton, 0, BTN_Y);
    lv_obj_set_align(ui_actionButton, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(ui_actionButton, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_actionButton, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_actionButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_actionButton, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_actionButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_actionButtonLabel = lv_label_create(ui_actionButton);
    lv_obj_center(ui_actionButtonLabel);
    lv_label_set_text(ui_actionButtonLabel, "Check ESP32");
    lv_obj_set_style_text_font(ui_actionButtonLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_actionButtonLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_actionButton, ui_event_checkUpdate, LV_EVENT_CLICKED, NULL);

    /* ── Rollback button (y=224, h=36, ESP32 only) ── */
    #define ROLLBACK_Y  (BTN_Y + BTN_H + 6)

    ui_rollbackButton = lv_button_create(ui_detailOTA);
    lv_obj_set_size(ui_rollbackButton, BTN_W, BTN_H);
    lv_obj_set_pos(ui_rollbackButton, 0, ROLLBACK_Y);
    lv_obj_set_align(ui_rollbackButton, LV_ALIGN_TOP_MID);
    lv_obj_set_style_radius(ui_rollbackButton, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_rollbackButton, lv_color_hex(0xE37400), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_rollbackButton, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_rollbackButton, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_rollbackButton, 40, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_rollbackButtonLabel = lv_label_create(ui_rollbackButton);
    lv_obj_center(ui_rollbackButtonLabel);
    {
        const char *prev = ota_get_previous_version();
        if (prev && prev[0]) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Rollback to %s", prev);
            lv_label_set_text(ui_rollbackButtonLabel, buf);
        } else {
            lv_label_set_text(ui_rollbackButtonLabel, "No prev. version");
        }
    }
    lv_obj_set_style_text_font(ui_rollbackButtonLabel, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_rollbackButtonLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_rollbackButton, ui_event_rollback, LV_EVENT_CLICKED, NULL);

    /* ── Progress bar (same y as action button, hidden until OTA runs) ── */
    #define PROGRESS_Y  BTN_Y

    ui_progressBar = lv_bar_create(ui_detailOTA);
    lv_obj_set_size(ui_progressBar, 210, 16);
    lv_obj_set_pos(ui_progressBar, 0, PROGRESS_Y);
    lv_obj_set_align(ui_progressBar, LV_ALIGN_TOP_MID);
    lv_bar_set_range(ui_progressBar, 0, 100);
    lv_bar_set_value(ui_progressBar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(ui_progressBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ui_progressBar, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_progressBar, lv_color_hex(0xDDDDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_progressBar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_progressBar, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_progressBar, lv_color_hex(0x1A73E8), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_progressLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_pos(ui_progressLabel, 0, PROGRESS_Y + 20);
    lv_obj_set_align(ui_progressLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_progressLabel, "0%");
    lv_obj_set_style_text_font(ui_progressLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_progressLabel, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_progressLabel, LV_OBJ_FLAG_HIDDEN);

    /* ── Status label ── */
    #define STATUS_Y  (ROLLBACK_Y + BTN_H + 8)

    ui_otaStatusLabel = lv_label_create(ui_detailOTA);
    lv_obj_set_width(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_otaStatusLabel, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_otaStatusLabel, 0, STATUS_Y);
    lv_obj_set_align(ui_otaStatusLabel, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_otaStatusLabel, "");
    lv_obj_set_style_text_font(ui_otaStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ── Gesture ── */
    lv_obj_add_event_cb(ui_detailOTA, ui_event_detailOTA, LV_EVENT_GESTURE, NULL);

    /* ── Progress poll timer ── */
    s_ota_progress_timer = lv_timer_create(ui_ota_progress_timer_cb, 500, NULL);

    /* ── Initial state ── */
    ui_chip_switch_update_styles();
    {
        const char *prev = ota_get_previous_version();
        if (prev == NULL || prev[0] == '\0') lv_obj_add_flag(ui_rollbackButton, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ==========================================================================
 * Screen destroy
 * ========================================================================== */

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
    ui_chipSwitchBar = NULL;
    ui_chipEspLabel = NULL;
    ui_chipStmLabel = NULL;
    ui_chipEspDot = NULL;
    ui_chipStmDot = NULL;
    ui_versionPanel = NULL;
    ui_versionLabel = NULL;
    ui_versionValue = NULL;
    ui_targetLabel = NULL;
    ui_targetValue = NULL;
    ui_sizeLabel = NULL;
    ui_sizeValue = NULL;
    ui_previousLabel = NULL;
    ui_previousValue = NULL;
    ui_previousRow = NULL;
    ui_actionButton = NULL;
    ui_actionButtonLabel = NULL;
    ui_rollbackButton = NULL;
    ui_rollbackButtonLabel = NULL;
    ui_progressBar = NULL;
    ui_progressLabel = NULL;
    ui_otaStatusLabel = NULL;

    s_current_chip = OTA_CHIP_ESP32;
    s_esp32_has_update = false;
    s_stm32_has_update = false;
}
