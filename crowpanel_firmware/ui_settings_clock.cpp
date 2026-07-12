#include "ui_settings_clock.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"
#include <ArduinoJson.h>

static lv_obj_t *scr = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *dd_wifi = NULL;
static lv_obj_t *ta_pass = NULL;
static lv_obj_t *kb_wifi = NULL;
static lv_obj_t *lbl_conn = NULL;

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

// ── Keyboard and text area logic ──
static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta_pass, LV_STATE_FOCUSED);
    }
}

static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_wifi, ta_pass);
        lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kb_wifi);
    }
}

static void btn_conn_cb(lv_event_t * e) {
    char ssid_buf[64];
    lv_dropdown_get_selected_str(dd_wifi, ssid_buf, sizeof(ssid_buf));
    const char *pass = lv_textarea_get_text(ta_pass);

    if (strlen(ssid_buf) == 0 || strcmp(ssid_buf, "Scanning...") == 0) return;

    if (lbl_conn) lv_label_set_text(lbl_conn, "Connecting...");

    StaticJsonDocument<256> doc;
    doc["cmd"]  = "WIFI_CONNECT";
    doc["ssid"] = ssid_buf;
    doc["pass"] = pass;
    String out;
    serializeJson(doc, out);
    CommManager::sendCommand(out);
}

static void btn_disc_cb(lv_event_t * e) {
    DataManager::clearWifiCredentials();
    CommManager::sendCommand("{\"cmd\":\"WIFI_DISCONNECT\"}");
}

// ── CommManager callbacks ──
void uiSettingsUpdateClock(const char *ts) {
    if (!scr || !lbl_time || !lbl_date) return;
    if (strlen(ts) >= 19) {
        int year = atoi(ts);
        int month = atoi(ts + 5);
        int day = atoi(ts + 8);
        int hour = atoi(ts + 11);
        int minute = atoi(ts + 14);

        int y = year, m = month;
        if (m < 3) { m += 12; y -= 1; }
        int k = y % 100;
        int j = y / 100;
        int h = (day + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
        const char *days[] = {"Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};

        const char *ampm = (hour >= 12) ? "PM" : "AM";
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;

        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", h12, minute, ampm);
        lv_label_set_text(lbl_time, timeStr);

        char dateStr[40];
        snprintf(dateStr, sizeof(dateStr), "%s, %d/%d/%d", days[h], month, day, year);
        lv_label_set_text(lbl_date, dateStr);
    }
}

void uiSettingsUpdateWifiScan(const char *ssids) {
    if (!scr || !dd_wifi) return;
    if (!ssids || strlen(ssids) == 0) {
        lv_dropdown_set_options(dd_wifi, "No networks found");
        return;
    }

    String all = String(ssids);
    all.replace(",", "\n");
    
    // Limit to first 10 networks to prevent UI freeze
    const int MAX_NETWORKS = 10;
    int newlineCount = 0;
    int lastNewline = 0;
    for (int i = 0; i < all.length(); i++) {
        if (all.charAt(i) == '\n') {
            newlineCount++;
            if (newlineCount >= MAX_NETWORKS) {
                all = all.substring(0, i);
                break;
            }
        }
    }
    
    lv_dropdown_set_options(dd_wifi, all.c_str());
}

void uiSettingsUpdateWifiStatus(bool connected) {
    if (!scr || !lbl_conn) return;
    if (connected) {
        lv_label_set_text(lbl_conn, "Connected!");
        // Save current so we can auto-reconnect later
        char ssid_buf[64];
        lv_dropdown_get_selected_str(dd_wifi, ssid_buf, sizeof(ssid_buf));
        const char *pass = lv_textarea_get_text(ta_pass);
        DataManager::saveWifiCredentials(String(ssid_buf), String(pass));
    } else {
        lv_label_set_text(lbl_conn, "Connect");
    }
}

void buildSettingsClockScreen() {
    if (scr) return;
    
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Clock & Network", "Device Settings", btn_back_cb, true);

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

    lbl_time = lv_label_create(col_left);
    lv_label_set_text(lbl_time, "--:-- --");
    UIManager::styleLabel(lbl_time, COLOR_TEXT_MAIN, &lv_font_montserrat_48, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 0, 20);

    lbl_date = lv_label_create(col_left);
    lv_label_set_text(lbl_date, "Waiting for sync...");
    UIManager::styleLabel(lbl_date, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_LEFT, 5, 80);

    lv_obj_t *lbl_auto = lv_label_create(col_left);
    lv_label_set_text(lbl_auto, "Auto settings");
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

    dd_wifi = lv_dropdown_create(col_right);
    lv_dropdown_set_options(dd_wifi, "Scanning...");
    lv_obj_set_size(dd_wifi, 300, 40);
    lv_obj_align(dd_wifi, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_radius(dd_wifi, 8, 0);

    ta_pass = lv_textarea_create(col_right);
    lv_obj_set_size(ta_pass, 300, 40);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_one_line(ta_pass, true);
    lv_obj_set_style_radius(ta_pass, 8, 0);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_conn = lv_btn_create(col_right);
    lv_obj_set_size(btn_conn, 145, 40);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_style_bg_color(btn_conn, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_conn, 8, 0);
    lv_obj_add_event_cb(btn_conn, btn_conn_cb, LV_EVENT_CLICKED, NULL);

    lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, DataManager::isWifiConnected() ? "Connected!" : "Connect");
    UIManager::styleLabel(lbl_conn, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_conn);

    lv_obj_t *btn_disc = lv_btn_create(col_right);
    lv_obj_set_size(btn_disc, 145, 40);
    lv_obj_align(btn_disc, LV_ALIGN_TOP_LEFT, 155, 130);
    lv_obj_set_style_bg_color(btn_disc, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(btn_disc, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_disc, 1, 0);
    lv_obj_set_style_radius(btn_disc, 8, 0);
    lv_obj_add_event_cb(btn_disc, btn_disc_cb, LV_EVENT_CLICKED, NULL);
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
    lv_obj_set_style_pad_all(bottom, 0, 0);
    
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

    // Keyboard
    kb_wifi = lv_keyboard_create(scr);
    lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_wifi, kb_event_cb, LV_EVENT_ALL, NULL);
}

void uiShowSettingsClock() {
    if (!scr) buildSettingsClockScreen();
    lv_scr_load(scr);
    // Trigger a fresh scan every time the user opens this screen
    CommManager::sendCommand("{\"cmd\":\"WIFI_SCAN\"}");
}
