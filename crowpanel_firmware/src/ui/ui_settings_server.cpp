#include "ui_settings_server.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"

static lv_obj_t *scr = NULL;
static lv_obj_t *ta_dev_name = NULL;
static lv_obj_t *kb_server = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

static void destroy_screen() {
    if (scr) {
        lv_obj_t *to_del = scr;
        scr = NULL;        // null BEFORE async delete so re-entry always rebuilds
        ta_dev_name = NULL;
        kb_server = NULL;
        lv_obj_del_async(to_del);
    }
}

static void btn_back_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Server: btn_back_cb triggered");
    destroy_screen();
    UIManager::showSettings();
}

static void btn_save_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Server: btn_save_cb triggered");
    if (ta_dev_name) {
        const char* new_name = lv_textarea_get_text(ta_dev_name);
        if (new_name) {
            DataManager::setDeviceName(String(new_name));
        }
    }
    btn_back_cb(e);
}

static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta_dev_name, LV_STATE_FOCUSED);
    }
}

static void btn_test_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Server: btn_test_cb triggered");
    CommManager::sendCommand("{\"cmd\":\"TEST_API\"}");
    UIManager::showToast("Testing connection...", false);
}

static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_server, ta);
        lv_obj_clear_flag(kb_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb_server);
    }
}

void buildSettingsServerScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Server & Device", "Device Settings", btn_back_cb, true);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 800, 408);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 20, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    auto create_input_field = [](lv_obj_t *parent, const char *label, const char *placeholder, int y_ofs, int w, int x_ofs, bool is_pw) -> lv_obj_t* {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, label);
        UIManager::styleLabel(lbl, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs);

        lv_obj_t *ta = lv_textarea_create(parent);
        lv_obj_set_size(ta, w, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs + 25);
        lv_textarea_set_placeholder_text(ta, placeholder);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, is_pw);
        lv_obj_set_style_border_color(ta, UIManager::rgb(COLOR_STROKE), 0);
        lv_obj_set_style_radius(ta, 8, 0);
        return ta;
    };

    // Device Identity
    lv_obj_t *lbl_dev_id = lv_label_create(body);
    lv_label_set_text(lbl_dev_id, "Device identity");
    UIManager::styleLabel(lbl_dev_id, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_dev_id, LV_ALIGN_TOP_LEFT, 0, 0);

    ta_dev_name = create_input_field(body, "Device Name", "ManPro Biometric", 30, 360, 0, false);
    lv_textarea_set_text(ta_dev_name, DataManager::getDeviceName().c_str());
    lv_obj_add_event_cb(ta_dev_name, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *ta_dev_id = create_input_field(body, "Device ID", "P001-XXXX-XXXX-XXXX", 30, 360, 400, false);
    lv_textarea_set_text(ta_dev_id, DataManager::getDeviceId().c_str());  // P001-2607-6AEC-YRH5
    lv_obj_clear_flag(ta_dev_id, LV_OBJ_FLAG_CLICKABLE); // Device ID is read-only

    // Server connection
    lv_obj_t *lbl_serv = lv_label_create(body);
    lv_label_set_text(lbl_serv, "Server connection");
    UIManager::styleLabel(lbl_serv, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_serv, LV_ALIGN_TOP_LEFT, 0, 100);

    lv_obj_t *ta_api = create_input_field(body, "API Server URL", "https://demo.manpromanagement.com", 130, 760, 0, false);
    // Show the actual API_BASE_URL hardcoded in wroom_firmware — read-only display
    lv_textarea_set_text(ta_api, "https://demo.manpromanagement.com");
    lv_obj_clear_flag(ta_api, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ta_token = create_input_field(body, "Activation Code (token)", "Enter via Register page", 210, 360, 0, true);
    lv_textarea_set_password_show_time(ta_token, 0);
    // Show the saved activation code (hidden behind password dots)
    lv_textarea_set_text(ta_token, DataManager::getActivationCode().c_str());
    lv_obj_clear_flag(ta_token, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *btn_test = lv_btn_create(body);
    lv_obj_set_size(btn_test, 160, 40);
    lv_obj_align(btn_test, LV_ALIGN_TOP_LEFT, 400, 235);
    lv_obj_set_style_bg_color(btn_test, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(btn_test, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(btn_test, 1, 0);
    lv_obj_set_style_radius(btn_test, 8, 0);
    lv_obj_t *lbl_test = lv_label_create(btn_test);
    lv_label_set_text(lbl_test, "Test connection");
    UIManager::styleLabel(lbl_test, COLOR_GREEN_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_test);
    lv_obj_add_event_cb(btn_test, btn_test_cb, LV_EVENT_CLICKED, NULL);

    // Bottom Action Bar
    lv_obj_t *bottom = lv_obj_create(body);
    lv_obj_set_size(bottom, 760, 60);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_pad_all(bottom, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *btn_cancel = lv_btn_create(bottom);
    lv_obj_set_size(btn_cancel, 370, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_cancel, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_add_event_cb(btn_cancel, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    UIManager::styleLabel(lbl_cancel, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_save = lv_btn_create(bottom);
    lv_obj_set_size(btn_save, 370, 40);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_save, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Save changes");
    UIManager::styleLabel(lbl_save, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_save);

    kb_server = lv_keyboard_create(scr);
    lv_obj_add_flag(kb_server, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_server, kb_event_cb, LV_EVENT_ALL, NULL);
}

void uiShowSettingsServer() {
    if (!scr) buildSettingsServerScreen();
    lv_scr_load(scr);
}
