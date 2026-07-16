#include "ui_settings_danger.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

static void destroy_screen() {
    if (scr) {
        lv_obj_t *to_del = scr;
        scr = NULL;        // null BEFORE async delete so re-entry always rebuilds
        lv_obj_del_async(to_del);
    }
}

static void btn_back_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Danger: btn_back_cb triggered");
    destroy_screen();
    UIManager::showSettings();
}

static void btn_factory_reset_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Danger: btn_factory_reset_cb triggered");
    CommManager::sendCommand("{\"cmd\":\"FACTORY_RESET\"}");
    DataManager::factoryReset();
}

static void btn_reboot_wroom_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Danger: btn_reboot_wroom_cb triggered");
    CommManager::sendCommand("{\"cmd\":\"RESET\"}");
}

void buildSettingsDangerScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Device Info", "Device Settings", btn_back_cb, true);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 800, 408);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 20, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Device Information Card
    lv_obj_t *card_info = lv_obj_create(body);
    lv_obj_set_size(card_info, 760, 140);
    lv_obj_align(card_info, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(card_info, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(card_info, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(card_info, 1, 0);
    lv_obj_set_style_radius(card_info, 8, 0);
    lv_obj_set_style_pad_all(card_info, 20, 0);

    auto add_info_row = [](lv_obj_t *parent, const char *lbl, const char *val, int x_ofs, int y_ofs) {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_text(l, lbl);
        UIManager::styleLabel(l, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs);
        
        lv_obj_t *v = lv_label_create(parent);
        lv_label_set_text(v, val);
        UIManager::styleLabel(v, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(v, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs + 20);
    };

    // Firmware version from build macros
    add_info_row(card_info, "Firmware version", "v1.0.0 (" __DATE__ ")", 0, 0);
    add_info_row(card_info, "Activation status", DataManager::isActivated() ? "Activated" : "Not Activated", 250, 0);
    add_info_row(card_info, "WiFi status", DataManager::isWifiConnected() ? "Connected" : "Disconnected", 500, 0);

    // Show full Device ID (P001-2607-6AEC-YRH5)
    String devId = DataManager::getDeviceId();
    add_info_row(card_info, "Device ID", devId.c_str(), 0, 70);

    // Danger Zone Card
    lv_obj_t *card_danger = lv_obj_create(body);
    lv_obj_set_size(card_danger, 760, 160);
    lv_obj_align(card_danger, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_bg_color(card_danger, UIManager::rgb(0xfff0f3), 0); // Light red
    lv_obj_set_style_border_color(card_danger, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(card_danger, 1, 0);
    lv_obj_set_style_radius(card_danger, 8, 0);
    lv_obj_set_style_pad_all(card_danger, 20, 0);

    lv_obj_t *lbl_danger = lv_label_create(card_danger);
    lv_label_set_text(lbl_danger, LV_SYMBOL_WARNING " Danger Zone");
    UIManager::styleLabel(lbl_danger, COLOR_DANGER, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_danger, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_danger_desc = lv_label_create(card_danger);
    lv_label_set_text(lbl_danger_desc, "Factory reset — Unpairs device and clears fingerprint database");
    UIManager::styleLabel(lbl_danger_desc, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_danger_desc, LV_ALIGN_TOP_LEFT, 0, 30);

    lv_obj_t *btn_reset = lv_btn_create(card_danger);
    lv_obj_set_size(btn_reset, 180, 40);
    lv_obj_align(btn_reset, LV_ALIGN_TOP_LEFT, 0, 60);
    lv_obj_set_style_bg_color(btn_reset, UIManager::rgb(0xffe3e8), 0);
    lv_obj_set_style_border_color(btn_reset, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(btn_reset, 1, 0);
    lv_obj_set_style_radius(btn_reset, 8, 0);
    lv_obj_add_event_cb(btn_reset, btn_factory_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_reset = lv_label_create(btn_reset);
    lv_label_set_text(lbl_reset, "Factory Reset");
    UIManager::styleLabel(lbl_reset, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_reset);

    // Reboot WROOM button
    lv_obj_t *btn_reboot = lv_btn_create(card_danger);
    lv_obj_set_size(btn_reboot, 180, 40);
    lv_obj_align(btn_reboot, LV_ALIGN_TOP_LEFT, 200, 60);
    lv_obj_set_style_bg_color(btn_reboot, UIManager::rgb(0xfff3e0), 0);
    lv_obj_set_style_border_color(btn_reboot, UIManager::rgb(0xf57c00), 0);
    lv_obj_set_style_border_width(btn_reboot, 1, 0);
    lv_obj_set_style_radius(btn_reboot, 8, 0);
    lv_obj_add_event_cb(btn_reboot, btn_reboot_wroom_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_reboot = lv_label_create(btn_reboot);
    lv_label_set_text(lbl_reboot, "Reboot System");
    UIManager::styleLabel(lbl_reboot, 0xf57c00, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_reboot);
}

void uiShowSettingsDanger() {
    if (!scr) buildSettingsDangerScreen();
    lv_scr_load(scr);
}
