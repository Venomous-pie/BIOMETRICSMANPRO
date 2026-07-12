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

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
extern const lv_img_dsc_t icon_battery;

// The most-recently created shared header pill label.
// Every screen that calls buildHeader(show_wifi_pill=true) overwrites this pointer,
// so it always points to the currently visible header's pill.
static lv_obj_t *g_header_wifi_lbl = NULL;

lv_obj_t* UIManager::buildHeader(lv_obj_t* scr, const char* title, const char* subtitle, lv_event_cb_t back_cb, bool show_wifi_pill) {
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 85);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    if (back_cb != NULL) {
        lv_obj_t *btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 56, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_set_style_bg_color(btn_back, rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_radius(btn_back, 8, 0);
        lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
        styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
        lv_obj_center(lbl_back);
    }

    if (title != NULL) {
        lv_obj_t *lbl_title = lv_label_create(header);
        lv_label_set_text(lbl_title, title);
        styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
        if (subtitle != NULL) {
            lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 15);
            lv_obj_t *lbl_sub = lv_label_create(header);
            lv_label_set_text(lbl_sub, subtitle);
            styleLabel(lbl_sub, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
            lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 40);
        } else {
            lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
        }
    }

    if (show_wifi_pill) {
        lv_obj_t *pill = lv_obj_create(header);
        lv_obj_set_size(pill, 180, 40);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -20, 0);
        lv_obj_set_style_bg_color(pill, rgb(COLOR_GREEN_LIGHT), 0);
        lv_obj_set_style_radius(pill, 20, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

        bool wifiNow = DataManager::isWifiConnected();
        lv_obj_t *lbl_status = lv_label_create(pill);
        lv_label_set_text(lbl_status, wifiNow ? LV_SYMBOL_WIFI " Online" : LV_SYMBOL_WIFI " Offline");
        styleLabel(lbl_status, wifiNow ? COLOR_GREEN_MAIN : COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 15, 0);
        g_header_wifi_lbl = lbl_status;  // track so we can update it live

        lv_obj_t *batt_img = lv_img_create(pill);
        lv_img_set_src(batt_img, &icon_battery);
        lv_obj_set_style_img_recolor(batt_img, rgb(COLOR_GREEN_DARK), 0);
        lv_obj_set_style_img_recolor_opa(batt_img, LV_OPA_COVER, 0);
        lv_obj_align(batt_img, LV_ALIGN_RIGHT_MID, -15, 0);
    }

    return header;
}

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
    // Only pre-build the 4 core navigation screens that are always needed.
    // Result, Enroll, and EmpList are lazy-built on first use to conserve
    // heap at boot time and leave room for the Settings screen.
    buildWifiSetupScreen();
    buildActivationScreen();
    buildIdleScreen();
    buildMainMenuScreen();
}

void uiFactoryResetComplete() {
    uiShowWifiSetup();
}

void UIManager::updateHeaderWifi(bool connected) {
    if (!g_header_wifi_lbl) return;
    lv_label_set_text(g_header_wifi_lbl, connected ? LV_SYMBOL_WIFI " Online" : LV_SYMBOL_WIFI " Offline");
    lv_obj_set_style_text_color(g_header_wifi_lbl,
        rgb(connected ? COLOR_GREEN_MAIN : COLOR_GREEN_DARK), 0);
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
