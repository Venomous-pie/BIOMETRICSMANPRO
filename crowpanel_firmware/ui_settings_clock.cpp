#include "ui_settings_clock.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_48);

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

void buildSettingsClockScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Clock & Network", "Device Settings — 1 of 3", btn_back_cb, true);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 800, 408); // 480 - 72
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 20, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Left Column
    lv_obj_t *col_left = lv_obj_create(body);
    lv_obj_set_size(col_left, 380, 320);
    lv_obj_align(col_left, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(col_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_left, 0, 0);
    lv_obj_set_style_pad_all(col_left, 0, 0);

    lv_obj_t *lbl_time_title = lv_label_create(col_left);
    lv_label_set_text(lbl_time_title, "Current time");
    UIManager::styleLabel(lbl_time_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_time_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_time = lv_label_create(col_left);
    lv_label_set_text(lbl_time, "12:00 PM");
    UIManager::styleLabel(lbl_time, COLOR_TEXT_MAIN, &lv_font_montserrat_48, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *lbl_date = lv_label_create(col_left);
    lv_label_set_text(lbl_date, "Wednesday, July 1 2026");
    UIManager::styleLabel(lbl_date, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_LEFT, 5, 80);

    lv_obj_t *lbl_auto = lv_label_create(col_left);
    lv_label_set_text(lbl_auto, "─ Auto settings ─");
    UIManager::styleLabel(lbl_auto, 0x999999, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_auto, LV_ALIGN_TOP_LEFT, 0, 120);

    auto create_toggle = [](lv_obj_t *parent, const char *label, int y) {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 300, 50);
        lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);

        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, label);
        UIManager::styleLabel(lbl, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *sw = lv_switch_create(cont);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, UIManager::rgb(COLOR_GREEN_MAIN), LV_PART_INDICATOR | LV_STATE_CHECKED);
        return sw;
    };

    create_toggle(col_left, "Set time zone automatically", 150);
    create_toggle(col_left, "Set time automatically", 200);

    // Right Column
    lv_obj_t *col_right = lv_obj_create(body);
    lv_obj_set_size(col_right, 360, 320);
    lv_obj_align(col_right, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(col_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_right, 0, 0);
    lv_obj_set_style_pad_all(col_right, 0, 0);

    lv_obj_t *lbl_wifi_title = lv_label_create(col_right);
    lv_label_set_text(lbl_wifi_title, "Wi-Fi Network");
    UIManager::styleLabel(lbl_wifi_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_wifi_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *dd_wifi = lv_dropdown_create(col_right);
    lv_dropdown_set_options(dd_wifi, "Office-Wifi-5G\nGuest-Net\nScanner");
    lv_obj_set_size(dd_wifi, 300, 40);
    lv_obj_align(dd_wifi, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_radius(dd_wifi, 8, 0);

    lv_obj_t *ta_pass = lv_textarea_create(col_right);
    lv_obj_set_size(ta_pass, 300, 40);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_one_line(ta_pass, true);
    lv_obj_set_style_radius(ta_pass, 8, 0);

    lv_obj_t *btn_conn = lv_btn_create(col_right);
    lv_obj_set_size(btn_conn, 145, 40);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_style_bg_color(btn_conn, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_conn, 8, 0);
    lv_obj_t *lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, "Connect");
    UIManager::styleLabel(lbl_conn, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_conn);

    lv_obj_t *btn_disc = lv_btn_create(col_right);
    lv_obj_set_size(btn_disc, 145, 40);
    lv_obj_align(btn_disc, LV_ALIGN_TOP_LEFT, 155, 130);
    lv_obj_set_style_bg_color(btn_disc, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(btn_disc, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_disc, 1, 0);
    lv_obj_set_style_radius(btn_disc, 8, 0);
    lv_obj_t *lbl_disc = lv_label_create(btn_disc);
    lv_label_set_text(lbl_disc, "Disconnect");
    UIManager::styleLabel(lbl_disc, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_disc);

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

void uiShowSettingsClock() {
    if (!scr) buildSettingsClockScreen();
    lv_scr_load(scr);
}
