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
extern lv_obj_t * ui_otaTitlePanel;
extern lv_obj_t * ui_otaTitleLabel;
extern lv_obj_t * ui_versionPanel;
extern lv_obj_t * ui_versionLabel;
extern lv_obj_t * ui_versionValue;
extern lv_obj_t * ui_targetLabel;
extern lv_obj_t * ui_targetValue;
extern lv_obj_t * ui_sizeLabel;
extern lv_obj_t * ui_sizeValue;
extern void ui_event_checkUpdate(lv_event_t * e);
extern lv_obj_t * ui_checkUpdateButton;
extern lv_obj_t * ui_checkUpdateButtonLabel;
extern lv_obj_t * ui_otaStatusLabel;
extern lv_obj_t * ui_progressBar;
extern lv_obj_t * ui_progressLabel;
extern lv_obj_t * ui_previousLabel;
extern lv_obj_t * ui_previousValue;
extern void ui_event_rollback(lv_event_t * e);
extern lv_obj_t * ui_rollbackButton;
extern lv_obj_t * ui_rollbackButtonLabel;
// CUSTOM VARIABLES

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
