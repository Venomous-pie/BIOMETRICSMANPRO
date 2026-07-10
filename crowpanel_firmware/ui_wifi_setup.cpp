#include "ui_wifi_setup.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"
#include <ArduinoJson.h>

// ── static object handles ──────────────────────────────────────────────────
static lv_obj_t *scr_wifi      = NULL;
static lv_obj_t *ta_ssid       = NULL;
static lv_obj_t *ta_pass       = NULL;
static lv_obj_t *kb_wifi       = NULL;
static lv_obj_t *lbl_status    = NULL;
static lv_obj_t *lbl_err       = NULL;
static lv_obj_t *lbl_battery   = NULL;
static lv_obj_t *panel_networks = NULL;   // scrollable list of found SSIDs
static lv_obj_t *lbl_scan_btn  = NULL;    // ref to the Scan button label

static bool     wifi_is_connected = false;

// Scan-timeout timer: clears "Scanning..." if WROOM never replies
static lv_timer_t *scan_timeout_timer = NULL;

extern void uiShowActivation();

// ── helper: set error label text and colour ────────────────────────────────
static void setErr(const char* txt, bool success) {
    lv_label_set_text(lbl_err, txt);
    lv_obj_set_style_text_color(lbl_err,
        success ? UIManager::rgb(COLOR_GREEN_MAIN) : UIManager::rgb(COLOR_DANGER), 0);
}

// ── keyboard event ─────────────────────────────────────────────────────────
static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta_ssid, LV_STATE_FOCUSED);
        lv_obj_clear_state(ta_pass, LV_STATE_FOCUSED);
    }
}

// ── text area focus → show keyboard ───────────────────────────────────────
static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_wifi, ta);
        lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── show password button ──────────────────────────────────────────────────
static void btn_show_pass_cb(lv_event_t *e) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lv_textarea_get_password_mode(ta_pass)) {
        lv_textarea_set_password_mode(ta_pass, false);
        lv_label_set_text(lbl, LV_SYMBOL_EYE_CLOSE);
    } else {
        lv_textarea_set_password_mode(ta_pass, true);
        lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN);
    }
}

// ── network row tap → fill SSID field ─────────────────────────────────────
static void network_row_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
        // First child of row is the SSID label
        lv_obj_t *lbl = lv_obj_get_child(row, 0);
        if (lbl) {
            const char *name = lv_label_get_text(lbl);
            lv_textarea_set_text(ta_ssid, name);
        }
        setErr("Network selected. Enter password and tap Connect.", true);
    }
}

// ── scan timeout callback ──────────────────────────────────────────────────
static void scan_timeout_cb(lv_timer_t *t) {
    // If still showing "Scanning...", WROOM did not respond — give up
    const char *cur = lv_label_get_text(lbl_err);
    if (strcmp(cur, "Scanning networks...") == 0) {
        setErr("Scan timed out. Try again.", false);
        lv_label_set_text(lbl_scan_btn, "Scan networks " LV_SYMBOL_REFRESH);
    }
    lv_timer_del(t);
    scan_timeout_timer = NULL;
}

// ── Scan button ────────────────────────────────────────────────────────────
static void btn_scan_cb(lv_event_t *e) {
    lv_label_set_text(lbl_scan_btn, LV_SYMBOL_REFRESH " Scanning...");
    lv_label_set_text(lbl_err, "Scanning networks...");
    lv_obj_set_style_text_color(lbl_err, UIManager::rgb(COLOR_TEXT_MAIN), 0);

    // Cancel existing timeout timer if any
    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
        scan_timeout_timer = NULL;
    }
    // 25 s timeout to accommodate slow ESP32 radio scans in dense areas
    scan_timeout_timer = lv_timer_create(scan_timeout_cb, 25000, NULL);
    lv_timer_set_repeat_count(scan_timeout_timer, 1);

    CommManager::sendCommand("{\"cmd\":\"WIFI_SCAN\"}");
}

// ── Connect button ─────────────────────────────────────────────────────────
static void btn_connect_cb(lv_event_t *e) {
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);

    if (strlen(ssid) == 0) {
        setErr("SSID cannot be empty!", false);
        return;
    }

    setErr("Connecting...", true);

    StaticJsonDocument<256> doc;
    doc["cmd"]  = "WIFI_CONNECT";
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    String out;
    serializeJson(doc, out);
    CommManager::sendCommand(out);
}

// ── Continue button ────────────────────────────────────────────────────────
static void btn_continue_cb(lv_event_t *e) {
    if (wifi_is_connected) {
        DataManager::setWifiConfigured(true);
        uiShowActivation();
    } else {
        setErr("Please connect to WiFi first.", false);
    }
}

// ── Public: called by CommManager when WIFI_STATUS arrives ─────────────────
void uiWifiUpdateStatus(bool connected) {
    wifi_is_connected = connected;
    if (connected) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Online");
        setErr("Connected! Tap 'Continue to registration'.", true);
    } else {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Offline");
        setErr("Connection failed. Check SSID / password.", false);
    }
}

// ── Public: called by CommManager when WIFI_SCAN_RESULT arrives ────────────
// Expected JSON from WROOM: {"type":"WIFI_SCAN_RESULT","ssids":"Net1,Net2,Net3"}
void uiWifiUpdateScanResult(const char *ssids) {
    // Cancel the timeout timer
    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
        scan_timeout_timer = NULL;
    }

    lv_label_set_text(lbl_scan_btn, "Scan networks " LV_SYMBOL_REFRESH);

    if (!ssids || strlen(ssids) == 0) {
        setErr("No networks found. Try again.", false);
        return;
    }

    // Build row for each comma-separated SSID
    lv_obj_clean(panel_networks);
    lv_obj_clear_flag(panel_networks, LV_OBJ_FLAG_HIDDEN);

    String all = String(ssids);
    int start = 0;
    int rowIndex = 0;
    while (true) {
        int comma = all.indexOf(',', start);
        String name = (comma < 0) ? all.substring(start) : all.substring(start, comma);
        name.trim();
        if (name.length() > 0) {
            lv_obj_t *row = lv_obj_create(panel_networks);
            lv_obj_set_size(row, 348, 36);
            lv_obj_set_style_bg_color(row, UIManager::rgb((rowIndex % 2 == 0) ? COLOR_WIFI_BG : COLOR_GREEN_LIGHT), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_set_style_pad_all(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *lbl_net = lv_label_create(row);
            lv_label_set_text(lbl_net, name.c_str());
            UIManager::styleLabel(lbl_net, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
            lv_obj_align(lbl_net, LV_ALIGN_LEFT_MID, 6, 0);

            lv_obj_t *ico = lv_label_create(row);
            lv_label_set_text(ico, LV_SYMBOL_WIFI);
            UIManager::styleLabel(ico, COLOR_GREEN_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
            lv_obj_align(ico, LV_ALIGN_RIGHT_MID, -6, 0);

            lv_obj_add_event_cb(row, network_row_cb, LV_EVENT_CLICKED, NULL);
            rowIndex++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }

    setErr("Tap a network below to select it.", true);
}

// ── Build the screen ───────────────────────────────────────────────────────
void buildWifiSetupScreen() {
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(scr_wifi, LV_OPA_COVER, 0);

    // ── Title ──
    lv_obj_t *lbl_title = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_title, LV_SYMBOL_WIFI " WiFi set-up");
    UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *lbl_step = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_step, ". . . Step 1 of 3");
    UIManager::styleLabel(lbl_step, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_align_to(lbl_step, lbl_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    // ── Status Pill ──
    lv_obj_t *pill = lv_obj_create(scr_wifi);
    lv_obj_set_size(pill, 170, 38);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -20, 22);
    lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(pill, 19, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lbl_status = lv_label_create(pill);
    lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Offline");
    UIManager::styleLabel(lbl_status, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *batt_box = lv_obj_create(pill);
    lv_obj_set_size(batt_box, 32, 24);
    lv_obj_align(batt_box, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(batt_box, UIManager::rgb(COLOR_GREEN_DARK), 0);
    lv_obj_set_style_radius(batt_box, 4, 0);
    lv_obj_set_style_border_width(batt_box, 0, 0);
    lv_obj_clear_flag(batt_box, LV_OBJ_FLAG_SCROLLABLE);

    lbl_battery = lv_label_create(batt_box);
    lv_label_set_text(lbl_battery, "98");
    UIManager::styleLabel(lbl_battery, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_battery);

    // ── Back Button ──
    lv_obj_t *btn_back = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_back, 56, 38);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 20, 22);
    lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    // ── Subtitle ──
    lv_obj_t *lbl_sub = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_sub, "Connect the WiFi before registering this device");
    UIManager::styleLabel(lbl_sub, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_LEFT, 30, 90);

    // ── SSID Input ──
    lv_obj_t *lbl_ssid_title = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_ssid_title, "Network name (SSID)");
    UIManager::styleLabel(lbl_ssid_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_ssid_title, LV_ALIGN_TOP_LEFT, 30, 130);

    ta_ssid = lv_textarea_create(scr_wifi);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_obj_set_width(ta_ssid, 350);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 30, 155);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_color(ta_ssid, UIManager::rgb(COLOR_STROKE), 0);

    // ── Password Input ──
    lv_obj_t *lbl_pass_title = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_pass_title, "Password");
    UIManager::styleLabel(lbl_pass_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_pass_title, LV_ALIGN_TOP_LEFT, 400, 130);

    ta_pass = lv_textarea_create(scr_wifi);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_obj_set_width(ta_pass, 305);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 400, 155);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_color(ta_pass, UIManager::rgb(COLOR_STROKE), 0);

    // ── Show Password Button ──
    lv_obj_t *btn_show = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_show, 40, 40);
    lv_obj_align(btn_show, LV_ALIGN_TOP_LEFT, 710, 155);
    lv_obj_set_style_bg_color(btn_show, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_width(btn_show, 1, 0);
    lv_obj_set_style_border_color(btn_show, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_radius(btn_show, 4, 0);
    
    lv_obj_t *lbl_show = lv_label_create(btn_show);
    lv_label_set_text(lbl_show, LV_SYMBOL_EYE_OPEN);
    UIManager::styleLabel(lbl_show, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_show);
    lv_obj_add_event_cb(btn_show, btn_show_pass_cb, LV_EVENT_CLICKED, NULL);

    // ── Scan Button ──
    lv_obj_t *btn_scan = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_scan, 350, 40);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_LEFT, 30, 210);
    lv_obj_set_style_bg_color(btn_scan, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_width(btn_scan, 1, 0);
    lv_obj_set_style_border_color(btn_scan, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_radius(btn_scan, 6, 0);

    lbl_scan_btn = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan_btn, "Scan networks " LV_SYMBOL_REFRESH);
    UIManager::styleLabel(lbl_scan_btn, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_scan_btn);
    lv_obj_add_event_cb(btn_scan, btn_scan_cb, LV_EVENT_CLICKED, NULL);

    // ── Connect Button ──
    lv_obj_t *btn_connect = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_connect, 350, 40);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_LEFT, 400, 210);
    lv_obj_set_style_bg_color(btn_connect, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(btn_connect, 6, 0);
    lv_obj_t *lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    UIManager::styleLabel(lbl_connect, COLOR_GREEN_DARK, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_connect);
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_CLICKED, NULL);

    // ── Scrollable Network List (hidden until scan returns) ──
    panel_networks = lv_obj_create(scr_wifi);
    lv_obj_set_size(panel_networks, 350, 145);
    lv_obj_align(panel_networks, LV_ALIGN_TOP_LEFT, 30, 260);
    lv_obj_set_style_bg_color(panel_networks, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_color(panel_networks, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(panel_networks, 1, 0);
    lv_obj_set_style_radius(panel_networks, 6, 0);
    lv_obj_set_style_pad_all(panel_networks, 0, 0);
    lv_obj_set_flex_flow(panel_networks, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel_networks, LV_OBJ_FLAG_HIDDEN);  // hidden by default

    // ── Divider ──
    lv_obj_t *line = lv_line_create(scr_wifi);
    static lv_point_t line_pts[] = { {30, 415}, {750, 415} };
    lv_line_set_points(line, line_pts, 2);
    lv_obj_set_style_line_color(line, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_line_width(line, 1, 0);

    // ── Status / Error message ──
    lbl_err = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_err, "");
    UIManager::styleLabel(lbl_err, COLOR_DANGER, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_err, LV_ALIGN_BOTTOM_LEFT, 30, -16);

    // ── Continue Button ──
    lv_obj_t *btn_continue = lv_btn_create(scr_wifi);
    lv_obj_set_size(btn_continue, 260, 44);
    lv_obj_align(btn_continue, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_continue, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_continue, 8, 0);
    lv_obj_t *lbl_cont = lv_label_create(btn_continue);
    lv_label_set_text(lbl_cont, "Continue to registration " LV_SYMBOL_RIGHT);
    UIManager::styleLabel(lbl_cont, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cont);
    lv_obj_add_event_cb(btn_continue, btn_continue_cb, LV_EVENT_CLICKED, NULL);

    // ── On-screen Keyboard (hidden by default) ──
    kb_wifi = lv_keyboard_create(scr_wifi);
    lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_wifi, kb_event_cb, LV_EVENT_ALL, NULL);
}

void uiShowWifiSetup() {
    lv_scr_load(scr_wifi);
}
