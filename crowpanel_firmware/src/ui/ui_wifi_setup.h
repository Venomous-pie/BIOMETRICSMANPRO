#ifndef UI_WIFI_SETUP_H
#define UI_WIFI_SETUP_H

#include <lvgl.h>

void buildWifiSetupScreen();
void uiShowWifiSetup();
void uiWifiUpdateStatus(bool connected);
void uiWifiUpdateScanResult(const char* ssids);

#endif // UI_WIFI_SETUP_H
