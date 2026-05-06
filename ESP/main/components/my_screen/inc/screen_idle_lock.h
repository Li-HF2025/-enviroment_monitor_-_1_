#ifndef SCREEN_IDLE_LOCK_H
#define SCREEN_IDLE_LOCK_H
#include <stdio.h>
#include <stdbool.h>
void screen_idle_lock_init(uint32_t timeout_ms);
void screen_idle_lock_mark_activity(void);
void screen_idle_lock_poll(void);
bool screen_idle_lock_is_locked(void);
#endif //SCREEN_IDLE_LOCK_H