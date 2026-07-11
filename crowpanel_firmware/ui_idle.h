#ifndef UI_IDLE_H
#define UI_IDLE_H

#include <lvgl.h>

void buildIdleScreen();
void uiShowIdle();
void uiUpdateClock(const char *ts);
void uiShowPlaceFinger();
void uiIdleUpdateWifi(bool connected);

extern int pending_action;

#endif // UI_IDLE_H
