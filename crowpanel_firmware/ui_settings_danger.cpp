#include "ui_settings_danger.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

static lv_obj_t *scr = NULL;
static lv_obj_t *modal_overlay = NULL;
static lv_obj_t *btn_modal_confirm = NULL;
static lv_obj_t *ta_confirm = NULL;

extern const lv_img_dsc_t icon_warning;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);

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

static void btn_reboot_wroom_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Danger: btn_reboot_wroom_cb triggered");
    CommManager::sendCommand("{\"cmd\":\"RESET\"}");
}

static void ta_confirm_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        String input = lv_textarea_get_text(ta);
        String expected = DataManager::getDeviceName();
        if (input == expected) {
            lv_obj_clear_state(btn_modal_confirm, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn_modal_confirm, UIManager::rgb(0xffe3e8), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(btn_modal_confirm, 0), UIManager::rgb(COLOR_DANGER), 0);
        } else {
            lv_obj_add_state(btn_modal_confirm, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(btn_modal_confirm, UIManager::rgb(0xeeeeee), LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(btn_modal_confirm, 0), UIManager::rgb(0x999999), LV_STATE_DISABLED);
        }
    }
}

static void btn_modal_cancel_cb(lv_event_t * e) {
    if (modal_overlay) {
        lv_obj_del_async(modal_overlay);
        modal_overlay = NULL;
    }
    UIManager::showGlobalPill(true);
}

static void btn_modal_confirm_cb(lv_event_t * e) {
    if (Serial) Serial.println("UI Danger: Factory Reset Confirmed!");
    CommManager::sendCommand("{\"cmd\":\"FACTORY_RESET\"}");
    DataManager::factoryReset();
    
    if (modal_overlay) {
        lv_obj_del_async(modal_overlay);
        modal_overlay = NULL;
    }
    UIManager::showGlobalPill(true);
    destroy_screen();
    UIManager::showIdle();
}

static void ta_focus_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t * mod = lv_obj_get_parent(lv_event_get_target(e));
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(mod, LV_ALIGN_CENTER, 0, -150); // Shift modal up
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(mod, LV_ALIGN_CENTER, 0, 0); // Restore modal
    }
}

static void btn_factory_reset_cb(lv_event_t * e) {
    if (modal_overlay) return;
    UIManager::showGlobalPill(false); // Hide the pill so it doesn't overlap the modal overlay

    modal_overlay = lv_obj_create(scr);
    lv_obj_set_size(modal_overlay, 800, 480);
    lv_obj_set_style_bg_color(modal_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(modal_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(modal_overlay, 0, 0);
    lv_obj_set_style_radius(modal_overlay, 0, 0);
    lv_obj_clear_flag(modal_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *modal = lv_obj_create(modal_overlay);
    lv_obj_set_size(modal, 600, 420); 
    lv_obj_align(modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_white(), 0);
    lv_obj_set_style_radius(modal, 16, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    // Header: Icon + Title
    lv_obj_t *header_cont = lv_obj_create(modal);
    lv_obj_set_size(header_cont, 560, 50);
    lv_obj_set_style_bg_opa(header_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    lv_obj_set_style_pad_all(header_cont, 0, 0);
    lv_obj_align(header_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(header_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *icon = lv_img_create(header_cont);
    lv_img_set_src(icon, &icon_warning);
    lv_obj_set_style_img_recolor(icon, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *lbl_title = lv_label_create(header_cont);
    lv_label_set_text(lbl_title, "Factory reset");
    UIManager::styleLabel(lbl_title, COLOR_DANGER, &lv_font_montserrat_28, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 70, 0);

    // Text Description
    String devName = DataManager::getDeviceName();
    int enrolled = DataManager::getEnrolledFingerprintCount();
    int unsynced = DataManager::getUnsyncedAttendanceCount();

    lv_obj_t *lbl_desc = lv_label_create(modal);
    String descText = "This will permanently, on " + devName + " device:";
    lv_label_set_text(lbl_desc, descText.c_str());
    UIManager::styleLabel(lbl_desc, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_desc, LV_ALIGN_TOP_LEFT, 10, 60);

    lv_obj_t *lbl_bullets = lv_label_create(modal);
    String bulletText = LV_SYMBOL_BULLET "  Erase all " + String(enrolled) + " enrolled fingerprints from the device.\n\n" +
                        LV_SYMBOL_BULLET "  Discard " + String(unsynced) + " unsynced attendance records currently buffered\n     locally";
    lv_label_set_text(lbl_bullets, bulletText.c_str());
    UIManager::styleLabel(lbl_bullets, 0x555555, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_bullets, LV_ALIGN_TOP_LEFT, 20, 95);

    lv_obj_t *lbl_note = lv_label_create(modal);
    lv_label_set_text(lbl_note, "Attendance already synced to the server is not affected.");
    UIManager::styleLabel(lbl_note, 0x000000, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_note, LV_ALIGN_TOP_LEFT, 10, 165);

    // Validation Input
    lv_obj_t *lbl_confirm = lv_label_create(modal);
    String confirmTxt = "Type " + devName + " to confirm";
    lv_label_set_text(lbl_confirm, confirmTxt.c_str());
    UIManager::styleLabel(lbl_confirm, 0x333333, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_confirm, LV_ALIGN_TOP_LEFT, 10, 205);

    ta_confirm = lv_textarea_create(modal);
    lv_obj_set_size(ta_confirm, 540, 40);
    lv_obj_align(ta_confirm, LV_ALIGN_TOP_LEFT, 10, 230);
    lv_textarea_set_placeholder_text(ta_confirm, devName.c_str());
    lv_textarea_set_one_line(ta_confirm, true);
    lv_obj_add_event_cb(ta_confirm, ta_confirm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Keyboard
    lv_obj_t *kb = lv_keyboard_create(modal_overlay);
    lv_keyboard_set_textarea(kb, ta_confirm);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); // Hidden by default
    
    // Use ta_focus_event_cb to shift the screen up when the keyboard opens
    lv_obj_add_event_cb(ta_confirm, ta_focus_event_cb, LV_EVENT_ALL, kb);

    // Buttons
    lv_obj_t *btn_cancel = lv_btn_create(modal);
    lv_obj_set_size(btn_cancel, 260, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, -20);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_cancel, UIManager::rgb(0xcccccc), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_add_event_cb(btn_cancel, btn_modal_cancel_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    UIManager::styleLabel(lbl_cancel, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cancel);

    btn_modal_confirm = lv_btn_create(modal);
    lv_obj_set_size(btn_modal_confirm, 260, 40);
    lv_obj_align(btn_modal_confirm, LV_ALIGN_BOTTOM_RIGHT, -10, -20);
    // Initially disabled
    lv_obj_add_state(btn_modal_confirm, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn_modal_confirm, UIManager::rgb(0xeeeeee), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(btn_modal_confirm, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(btn_modal_confirm, 1, 0);
    lv_obj_add_event_cb(btn_modal_confirm, btn_modal_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_confirm_btn = lv_label_create(btn_modal_confirm);
    lv_label_set_text(lbl_confirm_btn, "Factory reset");
    UIManager::styleLabel(lbl_confirm_btn, 0x999999, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_confirm_btn, UIManager::rgb(0x999999), LV_STATE_DISABLED);
    lv_obj_center(lbl_confirm_btn);
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
