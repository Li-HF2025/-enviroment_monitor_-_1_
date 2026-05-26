#ifndef UI_SCREENLOCK_H
#define UI_SCREENLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_ScreenLock_screen_init(void);
void ui_ScreenLock_screen_destroy(void);
void ui_event_ScreenLock(lv_event_t *e);

extern lv_obj_t *ui_ScreenLock;
extern lv_obj_t *ui_lockTime;
extern lv_obj_t *ui_lockDate;
extern lv_obj_t *ui_lockIconBg;
extern lv_obj_t *ui_lockIcon;
extern lv_obj_t *ui_lockHint;

#ifdef __cplusplus
}
#endif

#endif
