#include "ui_settings_server.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

static void destroy_screen() {
    if (scr) {
        lv_obj_del_async(scr);
        scr = NULL;
    }
}

static void btn_back_cb(lv_event_t * e) {
    destroy_screen();
    UIManager::showSettings();
}

static void btn_save_cb(lv_event_t * e) {
    // Save logic can be added here
    btn_back_cb(e);
}

void buildSettingsServerScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Server & Device", "Device Settings — 2 of 3", btn_back_cb, true);

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

    create_input_field(body, "Device name", "Main office device", 30, 360, 0, false);
    create_input_field(body, "Device ID", "ESP32-A1", 30, 360, 400, false);

    // Server connection
    lv_obj_t *lbl_serv = lv_label_create(body);
    lv_label_set_text(lbl_serv, "Server connection");
    UIManager::styleLabel(lbl_serv, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_serv, LV_ALIGN_TOP_LEFT, 0, 100);

    lv_obj_t *ta_api = create_input_field(body, "API endpoint", "https://api.manpro-attendance.com/v1", 130, 760, 0, false);
    lv_textarea_set_text(ta_api, "https://api.manpro-attendance.com/v1");

    create_input_field(body, "Device token", "••••••••••••••••••••••••", 210, 360, 0, true);

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

    // Bottom Action Bar
    lv_obj_t *bottom = lv_obj_create(body);
    lv_obj_set_size(bottom, 760, 60);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    
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
    lv_obj_set_style_bg_color(btn_save, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_save, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_save, 1, 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Save changes");
    UIManager::styleLabel(lbl_save, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_save);
}

void uiShowSettingsServer() {
    if (!scr) buildSettingsServerScreen();
    lv_scr_load(scr);
}
