#include "ui_manager.h"
#include "../core/data_manager.h"
#include "ui_wifi_setup.h"
#include "ui_settings.h"
#include "ui_enroll.h"
#include "ui_result.h"
#include "ui_main_menu.h"
#include "../core/comm_manager.h"
#include "../splash/manpro_splash.h"

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
extern const lv_img_dsc_t icon_charging;

// The most-recently created shared header pill label.
// Every screen that calls buildHeader(show_wifi_pill=true) overwrites this pointer,
// so it always points to the currently visible header's pill.
static lv_obj_t *g_header_wifi_lbl = NULL;
static lv_obj_t *g_header_wifi_img = NULL;
extern const lv_img_dsc_t icon_wifi;

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
    return header;
}

static lv_obj_t *g_global_wifi_pill = NULL;

void UIManager::initGlobalPill() {
    if (g_global_wifi_pill) return;
    
    g_global_wifi_pill = lv_obj_create(lv_layer_top());
    lv_obj_add_flag(g_global_wifi_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(g_global_wifi_pill, 190, 40);
    // Align to top-right corner
    lv_obj_align(g_global_wifi_pill, LV_ALIGN_TOP_RIGHT, -15, 22);
    lv_obj_set_style_bg_color(g_global_wifi_pill, rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(g_global_wifi_pill, 20, 0);
    lv_obj_set_style_border_width(g_global_wifi_pill, 0, 0);
    lv_obj_clear_flag(g_global_wifi_pill, LV_OBJ_FLAG_SCROLLABLE);

    g_header_wifi_img = lv_img_create(g_global_wifi_pill);
    lv_img_set_src(g_header_wifi_img, &icon_wifi);
    lv_obj_set_style_img_recolor(g_header_wifi_img, rgb(COLOR_GREEN_DARK), 0);
    lv_obj_set_style_img_recolor_opa(g_header_wifi_img, LV_OPA_COVER, 0);
    lv_obj_align(g_header_wifi_img, LV_ALIGN_LEFT_MID, 15, 0);

    g_header_wifi_lbl = lv_label_create(g_global_wifi_pill);
    lv_label_set_text(g_header_wifi_lbl, "Offline");
    styleLabel(g_header_wifi_lbl, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align_to(g_header_wifi_lbl, g_header_wifi_img, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    lv_obj_t *batt_img = lv_img_create(g_global_wifi_pill);
    lv_img_set_src(batt_img, &icon_charging);
    lv_obj_set_style_img_recolor(batt_img, rgb(COLOR_GREEN_DARK), 0);
    lv_obj_set_style_img_recolor_opa(batt_img, LV_OPA_COVER, 0);
    lv_obj_align(batt_img, LV_ALIGN_RIGHT_MID, -15, 0);
}

void UIManager::showGlobalPill(bool show) {
    if (!g_global_wifi_pill) return;
    if (show) {
        lv_obj_clear_flag(g_global_wifi_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_global_wifi_pill, LV_OBJ_FLAG_HIDDEN);
    }
}

// External screen show functions
extern void uiShowIdle();
extern void uiShowActivation();
extern void uiShowWifiSetup();
extern void uiShowSettings();
extern void uiShowMainMenu();

void UIManager::begin() {
    initGlobalPill();
    buildAllScreens();
}

void UIManager::loadInitialScreen() {
    if (!DataManager::isWifiConfigured()) {
        showWifiSetup();
    } else if (DataManager::isActivated()) {
        showIdle();
    } else {
        showActivation();
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

#include "../core/display_driver.h"
extern LGFX lcd;

void uiFactoryResetComplete() {
    if (Serial) Serial.println("[SYSTEM] Factory Reset complete. Rebooting...");
    lcd.setBrightness(0);
    delay(200);
    ESP.restart();
}

void UIManager::updateHeaderWifi(bool connected) {
    if (!g_header_wifi_lbl) return;
    lv_label_set_text(g_header_wifi_lbl, connected ? "Online" : "Offline");
    lv_obj_set_style_text_color(g_header_wifi_lbl, rgb(connected ? COLOR_GREEN_MAIN : COLOR_GREEN_DARK), 0);
    if (g_header_wifi_img) {
        lv_obj_set_style_img_recolor(g_header_wifi_img, rgb(connected ? COLOR_GREEN_MAIN : COLOR_GREEN_DARK), 0);
    }
}

static lv_obj_t *g_toast = NULL;
static lv_timer_t *g_toast_timer = NULL;

static void toast_timer_cb(lv_timer_t *timer) {
    if (g_toast) {
        lv_obj_del(g_toast);
        g_toast = NULL;
    }
    g_toast_timer = NULL;
}

static void toast_close_cb(lv_event_t *e) {
    if (g_toast_timer) {
        lv_timer_del(g_toast_timer);
        g_toast_timer = NULL;
    }
    if (g_toast) {
        lv_obj_del(g_toast);
        g_toast = NULL;
    }
}

void UIManager::showToast(const char* msg, bool is_error) {
    if (manpro_is_splash_active()) return;

    if (g_toast) {
        lv_obj_del(g_toast);
        g_toast = NULL;
    }
    if (g_toast_timer) {
        lv_timer_del(g_toast_timer);
        g_toast_timer = NULL;
    }

    g_toast = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(g_toast, 300, LV_SIZE_CONTENT);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(g_toast, rgb(is_error ? COLOR_DANGER : 0x333333), 0);
    lv_obj_set_style_radius(g_toast, 20, 0);
    lv_obj_set_style_border_width(g_toast, 0, 0);
    lv_obj_set_style_pad_all(g_toast, 10, 0);
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(g_toast, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_toast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_toast, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(g_toast);
    lv_label_set_text(lbl, msg);
    styleLabel(lbl, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(lbl, 230); // flex sizing handles the rest
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    lv_obj_t *btn_close = lv_btn_create(g_toast);
    lv_obj_set_size(btn_close, 30, 30);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_close, 0, 0);
    lv_obj_add_event_cb(btn_close, toast_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    styleLabel(lbl_close, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_close);

    g_toast_timer = lv_timer_create(toast_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_toast_timer, 1);
}

void UIManager::showIdle() {
    showGlobalPill(true);
    CommManager::sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":true}");
    uiShowIdle();
}

void UIManager::showActivation() {
    showGlobalPill(false);
    CommManager::sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":false}");
    uiShowActivation();
}

void UIManager::showWifiSetup() {
    showGlobalPill(false);
    CommManager::sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":false}");
    uiShowWifiSetup();
}

extern lv_obj_t *scr_settings;

void UIManager::showSettings() {
    showGlobalPill(true);
    CommManager::sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":false}");
    uiShowSettings();
}

void UIManager::showMainMenu() {
    showGlobalPill(true);
    CommManager::sendCommand("{\"cmd\":\"SET_IDLE\",\"idle\":false}");
    uiShowMainMenu();
}
