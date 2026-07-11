#include "ui_activation.h"
#include "ui_manager.h"
#include "data_manager.h"

static lv_obj_t *scr_activation = NULL;

extern const lv_img_dsc_t icon_battery;

static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_step = NULL;

// View 1: Show Hardware Code
static lv_obj_t *view_hw_code = NULL;
static lv_obj_t *btn_have_code = NULL;

// View 2: Enter Activation Code
static lv_obj_t *view_enter_code = NULL;
static lv_obj_t *ta_code1 = NULL;
static lv_obj_t *ta_code2 = NULL;
static lv_obj_t *ta_code3 = NULL;
static lv_obj_t *btn_activate = NULL;
static lv_obj_t *kb_code = NULL;
static lv_obj_t *lbl_err = NULL;
static lv_timer_t *lockout_timer = NULL;

extern void uiShowIdle();
extern void uiShowWifiSetup();

static void update_lockout_ui() {
    if (DataManager::isLockedOut()) {
        unsigned long elapsed = millis() - DataManager::getLockoutStartTime();
        unsigned long remaining = 600000 - elapsed;
        int mins = remaining / 60000;
        int secs = (remaining % 60000) / 1000;
        char buf[64];
        snprintf(buf, sizeof(buf), "Locked out. Try again in %d:%02d", mins, secs);
        lv_label_set_text(lbl_err, buf);
        lv_obj_add_state(btn_activate, LV_STATE_DISABLED);
        lv_obj_add_state(ta_code1, LV_STATE_DISABLED);
        lv_obj_add_state(ta_code2, LV_STATE_DISABLED);
        lv_obj_add_state(ta_code3, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(btn_activate, LV_STATE_DISABLED);
        lv_obj_clear_state(ta_code1, LV_STATE_DISABLED);
        lv_obj_clear_state(ta_code2, LV_STATE_DISABLED);
        lv_obj_clear_state(ta_code3, LV_STATE_DISABLED);
        if (DataManager::getFailedAttempts() > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Invalid code. %d tries left.", 5 - DataManager::getFailedAttempts());
            lv_label_set_text(lbl_err, buf);
        } else {
            lv_label_set_text(lbl_err, "");
        }
    }
}

static void lockout_timer_cb(lv_timer_t * timer) {
    if (!lv_obj_has_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN)) {
        update_lockout_ui();
    }
}

static void btn_change_wifi_cb(lv_event_t * e) {
    if (lockout_timer) { lv_timer_del(lockout_timer); lockout_timer = NULL; }
    uiShowWifiSetup();
}

static void btn_back_cb(lv_event_t * e) {
    // Context-aware: if we're on Step 3 (enter code), go back to Step 2 (hw code)
    // If we're already on Step 2 (hw code), go back to WiFi setup
    if (!lv_obj_has_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN)) {
        // Currently on Step 3 — go back to Step 2
        lv_obj_add_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_activate, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kb_code, LV_OBJ_FLAG_HIDDEN);

        lv_obj_clear_flag(view_hw_code, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_have_code, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(lbl_title, "Register this Device");
        lv_label_set_text(lbl_step, ". . Step 2 of 3");
    } else {
        // Currently on Step 2 — go back to WiFi setup
        if (lockout_timer) { lv_timer_del(lockout_timer); lockout_timer = NULL; }
        uiShowWifiSetup();
    }
}

static void btn_have_code_cb(lv_event_t * e) {
    // Hide Hardware Code view, show Enter Code view
    lv_obj_add_flag(view_hw_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_have_code, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_clear_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_activate, LV_OBJ_FLAG_HIDDEN);
    // Keyboard only shows when they tap a box, but we can show it now
    lv_obj_clear_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(kb_code, ta_code1);
    
    lv_label_set_text(lbl_title, "Enter Activation Code");
    lv_label_set_text(lbl_step, ". . Step 3 of 3");
}

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t*)lv_event_get_target(e);
    
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_code, ta);
        lv_obj_clear_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (!DataManager::isLockedOut()) lv_label_set_text(lbl_err, "");
        
        // Force uppercase
        const char *txt = lv_textarea_get_text(ta);
        String s = txt;
        s.toUpperCase();
        if (s != txt) {
            lv_textarea_set_text(ta, s.c_str());
        }
        
        // Auto-advance if 4 chars typed
        if (s.length() >= 4) {
            if (ta == ta_code1) {
                lv_event_send(ta_code2, LV_EVENT_FOCUSED, NULL);
                lv_obj_add_state(ta_code2, LV_STATE_FOCUSED);
                lv_obj_clear_state(ta_code1, LV_STATE_FOCUSED);
            } else if (ta == ta_code2) {
                lv_event_send(ta_code3, LV_EVENT_FOCUSED, NULL);
                lv_obj_add_state(ta_code3, LV_STATE_FOCUSED);
                lv_obj_clear_state(ta_code2, LV_STATE_FOCUSED);
            } else if (ta == ta_code3) {
                // Done entering
                lv_obj_add_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(ta_code3, LV_STATE_FOCUSED);
            }
        }
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static void btn_activate_cb(lv_event_t * e) {
    if (DataManager::isLockedOut()) return;

    String p1 = lv_textarea_get_text(ta_code1);
    String p2 = lv_textarea_get_text(ta_code2);
    String p3 = lv_textarea_get_text(ta_code3);
    
    if (p1.length() != 4 || p2.length() != 4 || p3.length() != 4) {
        lv_label_set_text(lbl_err, "Please fill all 12 characters.");
        return;
    }
    
    String fullCode = p1 + p2 + p3;
    if (DataManager::activate(fullCode)) {
        if (lockout_timer) { lv_timer_del(lockout_timer); lockout_timer = NULL; }
        uiShowIdle();
    } else {
        update_lockout_ui();
    }
}

void buildActivationScreen() {
    scr_activation = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_activation, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(scr_activation, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_activation, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title ──
    lbl_title = lv_label_create(scr_activation);
    lv_label_set_text(lbl_title, "Register this Device");
    UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

    lbl_step = lv_label_create(scr_activation);
    lv_label_set_text(lbl_step, ". . Step 2 of 3");
    UIManager::styleLabel(lbl_step, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align_to(lbl_step, lbl_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    // ── Back Button ──
    lv_obj_t *btn_back = lv_btn_create(scr_activation);
    lv_obj_set_size(btn_back, 100, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 20, 22);
    lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_back);

    // ── Status Pill ──
    lv_obj_t *pill = lv_obj_create(scr_activation);
    lv_obj_set_size(pill, 160, 40);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -20, 22);
    lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(pill, 20, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_status = lv_label_create(pill);
    lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Online");
    UIManager::styleLabel(lbl_status, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *batt_img = lv_img_create(pill);
    lv_img_set_src(batt_img, &icon_battery);
    lv_obj_set_style_img_recolor(batt_img, UIManager::rgb(COLOR_GREEN_DARK), 0);
    lv_obj_set_style_img_recolor_opa(batt_img, LV_OPA_COVER, 0);
    lv_obj_align(batt_img, LV_ALIGN_RIGHT_MID, 0, 0);

    // ==============================================================
    // VIEW 1: Hardware Code
    // ==============================================================
    view_hw_code = lv_obj_create(scr_activation);
    lv_obj_set_size(view_hw_code, 700, 300);
    lv_obj_align(view_hw_code, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_opa(view_hw_code, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view_hw_code, 0, 0);
    lv_obj_clear_flag(view_hw_code, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_inst1 = lv_label_create(view_hw_code);
    lv_label_set_text(lbl_inst1, "Go to manpro.app/register and enter the Device Code below,\nthen click \"Next\" to enter your Activation code.");
    lv_label_set_long_mode(lbl_inst1, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_inst1, 660);
    UIManager::styleLabel(lbl_inst1, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_inst1, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *box_device_code = lv_obj_create(view_hw_code);
    lv_obj_set_size(box_device_code, 350, 120);
    lv_obj_align(box_device_code, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(box_device_code, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_border_color(box_device_code, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(box_device_code, 1, 0);
    lv_obj_set_style_radius(box_device_code, 8, 0);
    lv_obj_clear_flag(box_device_code, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_code_title = lv_label_create(box_device_code);
    lv_label_set_text(lbl_code_title, "DEVICE CODE:");
    UIManager::styleLabel(lbl_code_title, COLOR_GREEN_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_code_title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *lbl_hw = lv_label_create(box_device_code);
    String hwText = DataManager::getHardwareCode(); // Now returns XXXX-XXXX
    lv_label_set_text(lbl_hw, hwText.c_str());
    UIManager::styleLabel(lbl_hw, COLOR_GREEN_MAIN, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_hw, LV_ALIGN_CENTER, 0, 15);

    btn_have_code = lv_btn_create(scr_activation);
    lv_obj_set_size(btn_have_code, 280, 45);
    lv_obj_align(btn_have_code, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(btn_have_code, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_have_code, 8, 0);
    lv_obj_add_event_cb(btn_have_code, btn_have_code_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_have_code = lv_label_create(btn_have_code);
    lv_label_set_text(lbl_have_code, "I have my Activation Code " LV_SYMBOL_RIGHT);
    UIManager::styleLabel(lbl_have_code, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_have_code);

    // ==============================================================
    // VIEW 2: Enter Activation Code
    // ==============================================================
    view_enter_code = lv_obj_create(scr_activation);
    lv_obj_set_size(view_enter_code, 700, 180);
    lv_obj_align(view_enter_code, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_opa(view_enter_code, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view_enter_code, 0, 0);
    lv_obj_clear_flag(view_enter_code, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl_inst2 = lv_label_create(view_enter_code);
    lv_label_set_text(lbl_inst2, "Enter the Activation Code provided by the ManPro portal");
    UIManager::styleLabel(lbl_inst2, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_inst2, LV_ALIGN_TOP_MID, 0, 0);



    // 3 Text Boxes Layout
    lv_obj_t *box_container = lv_obj_create(view_enter_code);
    lv_obj_set_size(box_container, 550, 85);
    lv_obj_align(box_container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(box_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box_container, 0, 0);
    lv_obj_set_style_pad_all(box_container, 0, 0);
    lv_obj_clear_flag(box_container, LV_OBJ_FLAG_SCROLLABLE);

    auto style_ta = [](lv_obj_t *ta) {
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, 4);
        lv_textarea_set_placeholder_text(ta, "XXXX");
        lv_obj_set_size(ta, 165, 75);
        lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(ta, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
        lv_obj_set_style_border_color(ta, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_border_width(ta, 2, 0);
        lv_obj_set_style_text_color(ta, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
        
        // Force the padding to be equal on left and right to ensure the placeholder centers perfectly
        lv_obj_set_style_pad_left(ta, 0, 0);
        lv_obj_set_style_pad_right(ta, 0, 0);
        lv_obj_set_style_pad_top(ta, 20, 0);
        
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_28, 0);
        lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
    };

    ta_code1 = lv_textarea_create(box_container);
    style_ta(ta_code1);
    lv_obj_align(ta_code1, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *dash1 = lv_obj_create(box_container);
    lv_obj_set_size(dash1, 15, 4);
    lv_obj_set_style_bg_color(dash1, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(dash1, 0, 0);
    lv_obj_set_style_radius(dash1, 2, 0);
    lv_obj_clear_flag(dash1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dash1, LV_ALIGN_LEFT_MID, 171, 0);

    ta_code2 = lv_textarea_create(box_container);
    style_ta(ta_code2);
    lv_obj_align(ta_code2, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *dash2 = lv_obj_create(box_container);
    lv_obj_set_size(dash2, 15, 4);
    lv_obj_set_style_bg_color(dash2, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(dash2, 0, 0);
    lv_obj_set_style_radius(dash2, 2, 0);
    lv_obj_clear_flag(dash2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dash2, LV_ALIGN_RIGHT_MID, -171, 0);

    ta_code3 = lv_textarea_create(box_container);
    style_ta(ta_code3);
    lv_obj_align(ta_code3, LV_ALIGN_RIGHT_MID, 0, 0);

    // Error Label
    lbl_err = lv_label_create(scr_activation);
    lv_label_set_text(lbl_err, "");
    UIManager::styleLabel(lbl_err, COLOR_DANGER, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_err, LV_ALIGN_BOTTOM_MID, 0, -110);

    // Activate Button
    btn_activate = lv_btn_create(scr_activation);
    lv_obj_set_size(btn_activate, 140, 40);
    lv_obj_align(btn_activate, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(btn_activate, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_activate, 8, 0);
    lv_obj_add_event_cb(btn_activate, btn_activate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_activate = lv_label_create(btn_activate);
    lv_label_set_text(lbl_activate, "Activate " LV_SYMBOL_RIGHT);
    UIManager::styleLabel(lbl_activate, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_activate);
    lv_obj_add_flag(btn_activate, LV_OBJ_FLAG_HIDDEN);

    // Keyboard
    kb_code = lv_keyboard_create(scr_activation);
    lv_obj_add_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
#if LVGL_VERSION_MAJOR < 9
    lv_keyboard_set_mode(kb_code, LV_KEYBOARD_MODE_TEXT_UPPER);
#endif
}

void uiShowActivation() {
    lv_scr_load(scr_activation);
    
    if (!lockout_timer) {
        lockout_timer = lv_timer_create(lockout_timer_cb, 1000, NULL);
    }

    // Reset to Step 2 (hardware code view)
    lv_obj_add_flag(view_enter_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_activate, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kb_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view_hw_code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_have_code, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_title, "Register this Device");
    lv_label_set_text(lbl_step, ". . Step 2 of 3");
    
    lv_textarea_set_text(ta_code1, "");
    lv_textarea_set_text(ta_code2, "");
    lv_textarea_set_text(ta_code3, "");
    update_lockout_ui();
}
