#include "ui_manager.h"
#include "data_manager.h"
#include "ui_wifi_setup.h"
#include "ui_settings.h"
#include "ui_enroll.h"
#include "ui_result.h"
#include "ui_main_menu.h"

// External screen builders
extern void buildActivationScreen();
extern void buildIdleScreen();
extern void buildResultScreen();
extern void buildEnrollScreen();
extern void buildEmpListScreen();
extern void buildWifiSetupScreen();
extern void buildSettingsScreen();
extern void buildMainMenuScreen();

// External screen show functions
extern void uiShowIdle();
extern void uiShowActivation();
extern void uiShowWifiSetup();
extern void uiShowSettings();
extern void uiShowMainMenu();

void UIManager::begin() {
    buildAllScreens();
    if (!DataManager::isWifiConfigured()) {
        uiShowWifiSetup();
    } else if (DataManager::isActivated()) {
        uiShowIdle();
    } else {
        uiShowActivation();
    }
}

lv_color_t UIManager::rgb(uint32_t c) {
#if LVGL_VERSION_MAJOR >= 9
    return lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
#else
    return lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
#endif
}

void UIManager::styleLabel(lv_obj_t *obj, uint32_t color, const lv_font_t *font, lv_text_align_t align) {
    lv_obj_set_style_text_color(obj, rgb(color), 0);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_align(obj, align, 0);
}

void UIManager::buildAllScreens() {
    buildWifiSetupScreen();
    buildActivationScreen();
    buildIdleScreen();
    buildResultScreen();
    buildEnrollScreen();
    buildEmpListScreen();
    buildMainMenuScreen();
}

void uiFactoryResetComplete() {
    uiShowWifiSetup();
}

void UIManager::showIdle() {
    uiShowIdle();
}

void UIManager::showActivation() {
    uiShowActivation();
}

void UIManager::showWifiSetup() {
    uiShowWifiSetup();
}

extern lv_obj_t *scr_settings;

void UIManager::showSettings() {
    if (scr_settings == NULL) {
        buildSettingsScreen();
    }
    uiShowSettings();
}

void UIManager::showMainMenu() {
    uiShowMainMenu();
}
