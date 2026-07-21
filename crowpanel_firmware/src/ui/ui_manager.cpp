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

void UIManager::styleTextArea(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_color(obj, rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_text_color(obj, rgb(COLOR_TEXT_MAIN), 0);

    lv_obj_set_style_border_color(obj, rgb(COLOR_GREEN_MAIN), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);

    // Explicitly style the cursor (caret) so it's visible when focused
    lv_obj_set_style_border_color(obj, rgb(COLOR_GREEN_MAIN), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

// Forward declarations for toast (defined later) so openKeyboardFor can dismiss it
static lv_obj_t *g_toast = NULL;
static lv_timer_t *g_toast_timer = NULL;

static lv_obj_t* g_kb_modal = NULL;
static lv_obj_t* g_kb_ta = NULL;
static lv_obj_t* g_kb_keyboard = NULL;
static lv_obj_t* g_current_target_ta = NULL;
static lv_obj_t* g_previous_screen = NULL;

static const char * kb_map_num_qwerty_lower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "-", " ", "_", LV_SYMBOL_OK, ""
};
static const char * kb_map_num_qwerty_upper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "-", " ", "_", LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_num_qwerty[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 6, 2, 2
};

static void kb_modal_done_action() {
    if (g_current_target_ta) {
        lv_textarea_set_text(g_current_target_ta, lv_textarea_get_text(g_kb_ta));
        lv_event_send(g_current_target_ta, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_clear_state(g_current_target_ta, LV_STATE_FOCUSED);
    }
    if (g_previous_screen) lv_scr_load_anim(g_previous_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    g_current_target_ta = NULL;
}

static void kb_modal_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        kb_modal_done_action();
    } else if (code == LV_EVENT_CANCEL) {
        if (g_current_target_ta) {
            lv_obj_clear_state(g_current_target_ta, LV_STATE_FOCUSED);
        }
        if (g_previous_screen) lv_scr_load_anim(g_previous_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        g_current_target_ta = NULL;
    }
}

static void kb_done_btn_cb(lv_event_t * e) {
    kb_modal_done_action();
}

void UIManager::openKeyboardFor(lv_obj_t* target_ta) {
    if (!g_kb_modal) {
        g_kb_modal = lv_obj_create(NULL); // Independent screen instead of layer_top to fix typing lag
        lv_obj_set_style_bg_color(g_kb_modal, rgb(0xFFFFFF), 0); // Clean white background
        lv_obj_set_style_bg_opa(g_kb_modal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_kb_modal, 0, 0);
        lv_obj_set_style_radius(g_kb_modal, 0, 0);
        lv_obj_set_style_pad_all(g_kb_modal, 0, 0);
        lv_obj_clear_flag(g_kb_modal, LV_OBJ_FLAG_SCROLLABLE);

        // Top section container
        lv_obj_t* top_cont = lv_obj_create(g_kb_modal);
        lv_obj_set_size(top_cont, LCD_WIDTH, 120);
        lv_obj_align(top_cont, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(top_cont, rgb(COLOR_WIFI_BG), 0); // Slight off-white to separate from keyboard
        lv_obj_set_style_bg_opa(top_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(top_cont, 0, 0);
        lv_obj_set_style_radius(top_cont, 0, 0);
        lv_obj_set_style_pad_all(top_cont, 10, 0);

        // Text area
        g_kb_ta = lv_textarea_create(top_cont);
        lv_obj_set_size(g_kb_ta, 640, 100);
        lv_obj_align(g_kb_ta, LV_ALIGN_LEFT_MID, 0, 0);
        styleTextArea(g_kb_ta);
        lv_obj_set_style_radius(g_kb_ta, 8, 0); // Standard app corner radius
        lv_obj_set_style_text_font(g_kb_ta, &lv_font_montserrat_28, 0);

        // Done button
        lv_obj_t* btn_done = lv_btn_create(top_cont);
        lv_obj_set_size(btn_done, 120, 100); // Big, prominent button matching TA height
        lv_obj_align(btn_done, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_done, rgb(COLOR_GREEN_MAIN), 0); // App standard green
        lv_obj_set_style_radius(btn_done, 8, 0);
        lv_obj_add_event_cb(btn_done, kb_done_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* lbl_done = lv_label_create(btn_done);
        lv_label_set_text(lbl_done, "Done");
        styleLabel(lbl_done, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER); // White text
        lv_obj_center(lbl_done);

        g_kb_keyboard = lv_keyboard_create(g_kb_modal);
        lv_keyboard_set_textarea(g_kb_keyboard, g_kb_ta);
        lv_obj_set_size(g_kb_keyboard, LCD_WIDTH, 360);
        lv_obj_align(g_kb_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        
        // Apply custom 5-row map with numbers
        lv_keyboard_set_map(g_kb_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_num_qwerty_lower, kb_ctrl_num_qwerty);
        lv_keyboard_set_map(g_kb_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_num_qwerty_upper, kb_ctrl_num_qwerty);
        
        lv_obj_add_event_cb(g_kb_keyboard, kb_modal_event_cb, LV_EVENT_ALL, NULL);
    }
    
    g_previous_screen = lv_scr_act();
    
    g_current_target_ta = target_ta;
    
    const char* txt = lv_textarea_get_text(target_ta);
    const char* placeholder = lv_textarea_get_placeholder_text(target_ta);
    bool is_pw = lv_textarea_get_password_mode(target_ta);
    uint32_t max_len = lv_textarea_get_max_length(target_ta);
    
    lv_textarea_set_text(g_kb_ta, txt ? txt : "");
    lv_textarea_set_placeholder_text(g_kb_ta, placeholder ? placeholder : "");
    lv_textarea_set_password_mode(g_kb_ta, is_pw);
    lv_textarea_set_max_length(g_kb_ta, max_len);
    // Dismiss any active toast before switching screens to avoid tearing
    if (g_toast) { lv_obj_del(g_toast); g_toast = NULL; }
    if (g_toast_timer) { lv_timer_del(g_toast_timer); g_toast_timer = NULL; }

    lv_scr_load_anim(g_kb_modal, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); // No anim = no tearing
    lv_obj_add_state(g_kb_ta, LV_STATE_FOCUSED);
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
