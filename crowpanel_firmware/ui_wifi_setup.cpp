#include "ui_wifi_setup.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"
#include <ArduinoJson.h>

static lv_obj_t *scr_wifi = NULL;
static lv_obj_t *ta_ssid = NULL;
static lv_obj_t *ta_pass = NULL;
static lv_obj_t *kb_wifi = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_err = NULL;
static lv_obj_t *lbl_battery = NULL;

static bool wifi_is_connected = false;

extern void uiShowActivation();

static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta_ssid, LV_STATE_FOCUSED);
        lv_obj_clear_state(ta_pass, LV_STATE_FOCUSED);
    }
}

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t*)lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_wifi, ta);
        lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
    }
}

static void btn_connect_cb(lv_event_t * e) {
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);
    
    if (strlen(ssid) == 0) {
        lv_label_set_text(lbl_err, "SSID cannot be empty!");
        return;
    }
    
    lv_label_set_text(lbl_err, "Connecting...");
    
    StaticJsonDocument<256> doc;
    doc["cmd"] = "WIFI_CONNECT";
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    String out;
    serializeJson(doc, out);
    CommManager::sendCommand(out);
}

static void btn_scan_cb(lv_event_t * e) {
    lv_label_set_text(lbl_err, "Scanning networks...");
    CommManager::sendCommand("{\"cmd\":\"WIFI_SCAN\"}");
}

static void btn_continue_cb(lv_event_t * e) {
    if (wifi_is_connected) {
        DataManager::setWifiConfigured(true);
        uiShowActivation();
    } else {
        lv_label_set_text(lbl_err, "Please connect to WiFi first.");
    }
}

void uiWifiUpdateStatus(bool connected) {
    wifi_is_connected = connected;
    if (connected) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Online \u2022 Sensor: Waiting...");
        lv_label_set_text(lbl_err, "Connected successfully!");
        lv_obj_set_style_text_color(lbl_err, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    } else {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Offline \u2022 Sensor: Waiting...");
        lv_label_set_text(lbl_err, "Connection failed.");
        lv_obj_set_style_text_color(lbl_err, UIManager::rgb(COLOR_DANGER), 0);
    }
}

void uiWifiUpdateScanResult(const char* ssids) {
    if (strlen(ssids) > 0) {
        String s = ssids;
        int comma = s.indexOf(',');
        if (comma > 0) s = s.substring(0, comma);
        lv_textarea_set_text(ta_ssid, s.c_str());
    }
    lv_label_set_text(lbl_err, "Scan complete.");
    lv_obj_set_style_text_color(lbl_err, UIManager::rgb(COLOR_GREEN_MAIN), 0);
}

void buildWifiSetupScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(scr_wifi, LV_OPA_COVER, 0);

    // Title
    lv_obj_t *lbl_title = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_title, LV_SYMBOL_WIFI " WiFi set-up");
    UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *lbl_step = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_step, ". . Step 1 of 2");
    UIManager::styleLabel(lbl_step, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align_to(lbl_step, lbl_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // Status Pill
    lv_obj_t *pill = lv_obj_create(scr_wifi);
    lv_obj_set_size(pill, 280, 40);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -20, 30);
    lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(pill, 20, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    
    lbl_status = lv_label_create(pill);
    lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Offline \u2022 Sensor: Waiting...");
    UIManager::styleLabel(lbl_status, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 5, 0);

    // Battery Box
    lv_obj_t *batt_box = lv_obj_create(pill);
    lv_obj_set_size(batt_box, 35, 25);
    lv_obj_align(batt_box, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(batt_box, UIManager::rgb(COLOR_GREEN_DARK), 0);
    lv_obj_set_style_radius(batt_box, 4, 0);
    lv_obj_set_style_border_width(batt_box, 0, 0);
    lv_obj_clear_flag(batt_box, LV_OBJ_FLAG_SCROLLABLE);

    lbl_battery = lv_label_create(batt_box);
    lv_label_set_text(lbl_battery, "98");
    UIManager::styleLabel(lbl_battery, 0xFFFFFF, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_battery);

    // Back Button
    lv_obj_t *btn_back = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 30, 30);
    lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    // Subtitle
    lv_obj_t *lbl_sub = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_sub, "Connect the WiFi before registering this device");
    UIManager::styleLabel(lbl_sub, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_LEFT, 30, 110);

    // SSID Input
    lv_obj_t *lbl_ssid = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_ssid, "Network name (SSID)");
    UIManager::styleLabel(lbl_ssid, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 30, 150);

    ta_ssid = lv_textarea_create(scr_wifi);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_obj_set_width(ta_ssid, 350);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 30, 175);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_color(ta_ssid, UIManager::rgb(COLOR_STROKE), 0);

    // Password Input
    lv_obj_t *lbl_pass = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_pass, "Password");
    UIManager::styleLabel(lbl_pass, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 400, 150);

    ta_pass = lv_textarea_create(scr_wifi);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_obj_set_width(ta_pass, 350);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 400, 175);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_color(ta_pass, UIManager::rgb(COLOR_STROKE), 0);

    // Scan Button
    lv_obj_t *btn_scan = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_scan, 350, 40);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_LEFT, 30, 230);
    lv_obj_set_style_bg_color(btn_scan, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_width(btn_scan, 1, 0);
    lv_obj_set_style_border_color(btn_scan, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "Scan networks " LV_SYMBOL_REFRESH);
    UIManager::styleLabel(lbl_scan, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_scan);
    lv_obj_add_event_cb(btn_scan, btn_scan_cb, LV_EVENT_CLICKED, NULL);

    // Connect Button
    lv_obj_t *btn_connect = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_connect, 350, 40);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_LEFT, 400, 230);
    lv_obj_set_style_bg_color(btn_connect, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_t *lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    UIManager::styleLabel(lbl_connect, COLOR_GREEN_DARK, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_connect);
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_CLICKED, NULL);

    // Divider line
    lv_obj_t *line = lv_line_create(scr_wifi);
    static lv_point_t line_points[] = { {30, 290}, {750, 290} };
    lv_line_set_points(line, line_points, 2);
    lv_obj_set_style_line_color(line, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_line_width(line, 1, 0);

    // Error / Status Label (Bottom Left)
    lbl_err = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_err, "");
    UIManager::styleLabel(lbl_err, COLOR_DANGER, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_err, LV_ALIGN_BOTTOM_LEFT, 30, -30);

    // Continue Button
    lv_obj_t *btn_continue = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_continue, 250, 45);
    lv_obj_align(btn_continue, LV_ALIGN_BOTTOM_RIGHT, -30, -25);
    lv_obj_set_style_bg_color(btn_continue, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_continue, 8, 0);
    lv_obj_t *lbl_cont = lv_label_create(btn_continue);
    lv_label_set_text(lbl_cont, "Continue to registration " LV_SYMBOL_RIGHT);
    UIManager::styleLabel(lbl_cont, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cont);
    lv_obj_add_event_cb(btn_continue, btn_continue_cb, LV_EVENT_CLICKED, NULL);

    // Keyboard (Hidden by default)
    kb_wifi = lv_keyboard_create(scr_wifi);
    lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_wifi, kb_event_cb, LV_EVENT_ALL, NULL);
}

void uiShowWifiSetup() {
    lv_scr_load(scr_wifi);
}
