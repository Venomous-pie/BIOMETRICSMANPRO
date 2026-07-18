#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <lvgl.h>

void buildSettingsScreen();
void uiShowSettings();

// Exposed update functions if needed from CommManager / DataManager
void uiSettingsUpdateWifiList();
void uiSettingsUpdateClock(const char *ts);
void uiSettingsUpdateStatus(bool connected, int pending, const char* last_synced, int buffer_pct);

#endif // UI_SETTINGS_H
