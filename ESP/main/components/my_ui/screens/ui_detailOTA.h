#ifndef UI_DETAILOTA_H
#define UI_DETAILOTA_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_detailOTA
extern void ui_detailOTA_screen_init(void);
extern void ui_detailOTA_screen_destroy(void);
extern void ui_event_detailOTA(lv_event_t * e);
extern lv_obj_t * ui_detailOTA;

// Title
extern lv_obj_t * ui_otaTitlePanel;
extern lv_obj_t * ui_otaTitleLabel;

// Chip switch bar
extern lv_obj_t * ui_chipSwitchBar;
extern lv_obj_t * ui_chipEspLabel;
extern lv_obj_t * ui_chipStmLabel;
extern lv_obj_t * ui_chipEspDot;
extern lv_obj_t * ui_chipStmDot;

// Version info card — labels (shared, content switches per chip)
extern lv_obj_t * ui_versionPanel;
extern lv_obj_t * ui_versionLabel;
extern lv_obj_t * ui_versionValue;
extern lv_obj_t * ui_targetLabel;
extern lv_obj_t * ui_targetValue;
extern lv_obj_t * ui_sizeLabel;
extern lv_obj_t * ui_sizeValue;
extern lv_obj_t * ui_previousLabel;
extern lv_obj_t * ui_previousValue;

// Buttons
extern lv_obj_t * ui_actionButton;
extern lv_obj_t * ui_actionButtonLabel;
extern lv_obj_t * ui_rollbackButton;
extern lv_obj_t * ui_rollbackButtonLabel;

// Progress & status
extern lv_obj_t * ui_progressBar;
extern lv_obj_t * ui_progressLabel;
extern lv_obj_t * ui_otaStatusLabel;

// Event handlers
extern void ui_event_chipSwitchEsp(lv_event_t * e);
extern void ui_event_chipSwitchStm(lv_event_t * e);
extern void ui_event_checkUpdate(lv_event_t * e);
extern void ui_event_rollback(lv_event_t * e);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
