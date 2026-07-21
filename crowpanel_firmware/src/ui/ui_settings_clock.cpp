#include "ui_settings_clock.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"
#include <ArduinoJson.h>

static lv_obj_t *scr           = NULL;
static lv_obj_t *lbl_time      = NULL;
static lv_obj_t *lbl_date      = NULL;
static lv_obj_t *lbl_wifi_ssid = NULL;
static lv_obj_t *lbl_wifi_pill = NULL;
static lv_obj_t *lbl_ntp_status = NULL;  // shows last NTP sync result
static lv_obj_t *btn_sync_ntp   = NULL;
static lv_obj_t *sw_auto_time   = NULL;
static lv_obj_t *cont_manual    = NULL;
static lv_obj_t *dd_year        = NULL;
static lv_obj_t *dd_month       = NULL;
static lv_obj_t *dd_day         = NULL;
static lv_obj_t *dd_hour        = NULL;
static lv_obj_t *dd_minute      = NULL;
static lv_obj_t *dd_ampm        = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_48);

static void destroy_screen() {
    if (scr) {
        lv_obj_t *to_del = scr;
        scr              = NULL;
        lbl_time         = NULL;
        lbl_date         = NULL;
        lbl_wifi_ssid    = NULL;
        lbl_wifi_pill    = NULL;
        lbl_ntp_status   = NULL;
        btn_sync_ntp     = NULL;
        sw_auto_time     = NULL;
        cont_manual      = NULL;
        dd_year          = NULL;
        dd_month         = NULL;
        dd_day           = NULL;
        dd_hour          = NULL;
        dd_minute        = NULL;
        dd_ampm          = NULL;
        lv_obj_del_async(to_del);
    }
}

static void btn_back_cb(lv_event_t *e) {
    if (Serial) Serial.println("UI Clock: btn_back_cb triggered");
    destroy_screen();
    UIManager::showSettings();
}

// "Manage Wi-Fi" routes to the dedicated WiFi setup screen (no scanning here)
static void btn_manage_wifi_cb(lv_event_t *e) {
    if (Serial) Serial.println("UI Clock: btn_manage_wifi_cb triggered");
    destroy_screen();
    UIManager::showWifiSetup();
}

// "Sync Now" — asks WROOM to fire an NTP sync immediately
static void btn_sync_ntp_cb(lv_event_t *e) {
    if (Serial) Serial.println("UI Clock: btn_sync_ntp_cb triggered");
    if (lbl_ntp_status) lv_label_set_text(lbl_ntp_status, "Syncing...");
    CommManager::sendCommand("{\"cmd\":\"SYNC_NTP\"}");
}

static void sw_auto_time_cb(lv_event_t *e) {
    bool is_auto = lv_obj_has_state(sw_auto_time, LV_STATE_CHECKED);
    if (is_auto) {
        if (btn_sync_ntp) lv_obj_clear_flag(btn_sync_ntp, LV_OBJ_FLAG_HIDDEN);
        if (lbl_ntp_status) lv_obj_clear_flag(lbl_ntp_status, LV_OBJ_FLAG_HIDDEN);
        if (cont_manual) lv_obj_add_flag(cont_manual, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (btn_sync_ntp) lv_obj_add_flag(btn_sync_ntp, LV_OBJ_FLAG_HIDDEN);
        if (lbl_ntp_status) lv_obj_add_flag(lbl_ntp_status, LV_OBJ_FLAG_HIDDEN);
        if (cont_manual) lv_obj_clear_flag(cont_manual, LV_OBJ_FLAG_HIDDEN);
    }
}

static void btn_save_cb(lv_event_t *e) {
    if (lv_obj_has_state(sw_auto_time, LV_STATE_CHECKED)) {
        // Just trigger NTP sync
        CommManager::sendCommand("{\"cmd\":\"SYNC_NTP\"}");
    } else {
        if (!dd_year || !dd_month || !dd_day || !dd_hour || !dd_minute) return;
        
        char buf[16];
        lv_dropdown_get_selected_str(dd_year, buf, sizeof(buf));
        int y = atoi(buf);
        lv_dropdown_get_selected_str(dd_month, buf, sizeof(buf));
        int m = atoi(buf);
        lv_dropdown_get_selected_str(dd_day, buf, sizeof(buf));
        int d = atoi(buf);
        lv_dropdown_get_selected_str(dd_hour, buf, sizeof(buf));
        int h = atoi(buf);
        lv_dropdown_get_selected_str(dd_minute, buf, sizeof(buf));
        int min = atoi(buf);
        lv_dropdown_get_selected_str(dd_ampm, buf, sizeof(buf));
        bool is_pm = (strcmp(buf, "PM") == 0);

        if (is_pm && h != 12) h += 12;
        if (!is_pm && h == 12) h = 0;

        DynamicJsonDocument doc(256);
        doc["cmd"] = "SET_TIME";
        doc["y"] = y;
        doc["m"] = m;
        doc["d"] = d;
        doc["h"] = h;
        doc["min"] = min;
        
        String out;
        serializeJson(doc, out);
        CommManager::sendCommand(out);
    }
    
    // Go back to settings menu
    btn_back_cb(e);
}

static void set_dd_range(lv_obj_t *dd, int start, int end, bool pad) {
    String opts = "";
    for (int i = start; i <= end; i++) {
        if (pad && i < 10) opts += "0";
        opts += String(i);
        if (i < end) opts += "\n";
    }
    lv_dropdown_set_options(dd, opts.c_str());
}

// ── CommManager callbacks ──────────────────────────────────────────────────
void uiSettingsUpdateClock(const char *ts) {
    if (!scr || !lbl_time || !lbl_date) return;
    if (strlen(ts) < 19) return;

    int year   = atoi(ts);
    int month  = atoi(ts + 5);
    int day    = atoi(ts + 8);
    int hour   = atoi(ts + 11);
    int minute = atoi(ts + 14);

    // Zeller's congruence for day-of-week
    int y = year, m = month;
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100, j = y / 100;
    int h = (day + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    const char *days[] = {"Saturday","Sunday","Monday","Tuesday","Wednesday","Thursday","Friday"};

    const char *ampm = (hour >= 12) ? "PM" : "AM";
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", h12, minute, ampm);
    lv_label_set_text(lbl_time, timeStr);

    char dateStr[40];
    snprintf(dateStr, sizeof(dateStr), "%s, %d/%d/%d", days[h], month, day, year);
    lv_label_set_text(lbl_date, dateStr);

    // Populate manual controls with current time so it starts at the right time
    if (dd_year && lv_obj_has_flag(cont_manual, LV_OBJ_FLAG_HIDDEN)) {
        if (year >= 2024 && year <= 2035) lv_dropdown_set_selected(dd_year, year - 2024);
        if (month >= 1 && month <= 12)    lv_dropdown_set_selected(dd_month, month - 1);
        if (day >= 1 && day <= 31)        lv_dropdown_set_selected(dd_day, day - 1);
        
        int disp_h = hour % 12;
        if (disp_h == 0) disp_h = 12;
        lv_dropdown_set_selected(dd_hour, disp_h - 1); // 1-12 range
        
        if (minute >= 0 && minute <= 59)  lv_dropdown_set_selected(dd_minute, minute);
        
        lv_dropdown_set_selected(dd_ampm, (hour >= 12) ? 1 : 0);
    }
}

// No-op stubs — scan/connect UI removed from this page
void uiSettingsUpdateWifiScan(const char *ssids) { (void)ssids; }

void uiSettingsUpdateNtpStatus(bool ok, const char *ts, const char *err) {
    if (!scr || !lbl_ntp_status) return;
    char buf[64];
    if (ok && ts && strlen(ts) > 0) {
        // Show just the time portion (HH:MM:SS) for brevity
        const char *timePart = (strlen(ts) >= 19) ? ts + 11 : ts;
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Synced at %s (UTC+8)", timePart);
        lv_obj_set_style_text_color(lbl_ntp_status, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE " %s", (err && strlen(err)) ? err : "Sync failed");
        lv_obj_set_style_text_color(lbl_ntp_status, UIManager::rgb(COLOR_DANGER), 0);
    }
    lv_label_set_text(lbl_ntp_status, buf);
}

void uiSettingsUpdateWifiStatus(bool connected) {
    if (!scr) return;

    if (lbl_wifi_pill) {
        lv_label_set_text(lbl_wifi_pill, connected ? LV_SYMBOL_WIFI " Online" : LV_SYMBOL_WIFI " Offline");
        lv_obj_t *pill = lv_obj_get_parent(lbl_wifi_pill);
        if (pill) {
            lv_obj_set_style_bg_color(pill, UIManager::rgb(connected ? COLOR_GREEN_LIGHT : 0xFFE5E5), 0);
            lv_obj_set_style_text_color(lbl_wifi_pill,
                UIManager::rgb(connected ? COLOR_GREEN_MAIN : COLOR_DANGER), 0);
        }
    }

    if (lbl_wifi_ssid) {
        if (connected && DataManager::hasSavedWifi()) {
            String ssid = DataManager::getWifiSsid();
            char buf[80];
            snprintf(buf, sizeof(buf), "Connected to: %s", ssid.c_str());
            lv_label_set_text(lbl_wifi_ssid, buf);
        } else {
            lv_label_set_text(lbl_wifi_ssid, "Not connected to any network");
        }
    }
}

// ── Build screen ───────────────────────────────────────────────────────────
void buildSettingsClockScreen() {
    if (scr) return;

    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    UIManager::buildHeader(scr, "Clock & Network", "Device Settings", btn_back_cb, true);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_size(body, 800, 408);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 20, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // ── Left column: Clock ────────────────────────────────────────────────
    lv_obj_t *col_left = lv_obj_create(body);
    lv_obj_set_size(col_left, 380, 320);
    lv_obj_align(col_left, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(col_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_left, 0, 0);
    lv_obj_set_style_pad_all(col_left, 0, 0);
    lv_obj_clear_flag(col_left, LV_OBJ_FLAG_SCROLLABLE);

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

    // Set Automatically toggle
    lv_obj_t *lbl_auto = lv_label_create(col_left);
    lv_label_set_text(lbl_auto, "Set time automatically");
    UIManager::styleLabel(lbl_auto, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_auto, LV_ALIGN_TOP_LEFT, 0, 115);

    sw_auto_time = lv_switch_create(col_left);
    lv_obj_align(sw_auto_time, LV_ALIGN_TOP_LEFT, 170, 110);
    lv_obj_add_state(sw_auto_time, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_auto_time, sw_auto_time_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Sync Now button
    btn_sync_ntp = lv_btn_create(col_left);
    lv_obj_set_size(btn_sync_ntp, 140, 36);
    lv_obj_align(btn_sync_ntp, LV_ALIGN_TOP_LEFT, 0, 150);
    lv_obj_set_style_bg_color(btn_sync_ntp, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_sync_ntp, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_sync_ntp, 1, 0);
    lv_obj_set_style_radius(btn_sync_ntp, 6, 0);
    lv_obj_add_event_cb(btn_sync_ntp, btn_sync_ntp_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_sync_btn = lv_label_create(btn_sync_ntp);
    lv_label_set_text(lbl_sync_btn, LV_SYMBOL_REFRESH " Sync Now");
    UIManager::styleLabel(lbl_sync_btn, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_sync_btn);

    // NTP Status label (to the right of the button)
    lbl_ntp_status = lv_label_create(col_left);
    lv_label_set_text(lbl_ntp_status, ""); // empty by default
    UIManager::styleLabel(lbl_ntp_status, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_ntp_status, LV_ALIGN_TOP_LEFT, 160, 160);

    // Manual controls container (hidden by default)
    cont_manual = lv_obj_create(col_left);
    lv_obj_set_size(cont_manual, 380, 150);
    lv_obj_align(cont_manual, LV_ALIGN_TOP_LEFT, 0, 150);
    lv_obj_set_style_bg_opa(cont_manual, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_manual, 0, 0);
    lv_obj_set_style_pad_all(cont_manual, 0, 0);
    lv_obj_add_flag(cont_manual, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_manual, LV_OBJ_FLAG_SCROLLABLE);

    dd_year = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_year, 80, 40);
    lv_obj_align(dd_year, LV_ALIGN_TOP_LEFT, 0, 0);
    set_dd_range(dd_year, 2024, 2035, false);

    dd_month = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_month, 60, 40);
    lv_obj_align(dd_month, LV_ALIGN_TOP_LEFT, 90, 0);
    set_dd_range(dd_month, 1, 12, true);

    dd_day = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_day, 60, 40);
    lv_obj_align(dd_day, LV_ALIGN_TOP_LEFT, 160, 0);
    set_dd_range(dd_day, 1, 31, true);

    dd_hour = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_hour, 60, 40);
    lv_obj_align(dd_hour, LV_ALIGN_TOP_LEFT, 0, 50);
    set_dd_range(dd_hour, 1, 12, true);

    lv_obj_t *lbl_colon = lv_label_create(cont_manual);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_align(lbl_colon, LV_ALIGN_TOP_LEFT, 65, 60);

    dd_minute = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_minute, 60, 40);
    lv_obj_align(dd_minute, LV_ALIGN_TOP_LEFT, 75, 50);
    set_dd_range(dd_minute, 0, 59, true);

    dd_ampm = lv_dropdown_create(cont_manual);
    lv_obj_set_size(dd_ampm, 70, 40);
    lv_obj_align(dd_ampm, LV_ALIGN_TOP_LEFT, 145, 50);
    lv_dropdown_set_options(dd_ampm, "AM\nPM");

    // ── Right column: Wi-Fi status (read-only) + Manage button ───────────
    lv_obj_t *col_right = lv_obj_create(body);
    lv_obj_set_size(col_right, 360, 320);
    lv_obj_align(col_right, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(col_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_right, 0, 0);
    lv_obj_set_style_pad_all(col_right, 0, 0);
    lv_obj_clear_flag(col_right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_wifi_title = lv_label_create(col_right);
    lv_label_set_text(lbl_wifi_title, "Wi-Fi Network");
    UIManager::styleLabel(lbl_wifi_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_wifi_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status pill
    lv_obj_t *pill = lv_obj_create(col_right);
    lv_obj_set_size(pill, 300, 40);
    lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_radius(pill, 8, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    bool connected = DataManager::isWifiConnected();
    lv_obj_set_style_bg_color(pill, UIManager::rgb(connected ? COLOR_GREEN_LIGHT : 0xFFE5E5), 0);

    lbl_wifi_pill = lv_label_create(pill);
    lv_label_set_text(lbl_wifi_pill, connected ? LV_SYMBOL_WIFI " Online" : LV_SYMBOL_WIFI " Offline");
    UIManager::styleLabel(lbl_wifi_pill,
        connected ? COLOR_GREEN_MAIN : COLOR_DANGER,
        &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_wifi_pill);

    // SSID sub-label
    lbl_wifi_ssid = lv_label_create(col_right);
    if (connected && DataManager::hasSavedWifi()) {
        String ssid = DataManager::getWifiSsid();
        char buf[80];
        snprintf(buf, sizeof(buf), "Connected to: %s", ssid.c_str());
        lv_label_set_text(lbl_wifi_ssid, buf);
    } else {
        lv_label_set_text(lbl_wifi_ssid, "Not connected to any network");
    }
    UIManager::styleLabel(lbl_wifi_ssid, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_wifi_ssid, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_label_set_long_mode(lbl_wifi_ssid, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_wifi_ssid, 300);

    // "Manage Wi-Fi" button — routes to dedicated WiFi setup screen
    lv_obj_t *btn_manage = lv_btn_create(col_right);
    lv_obj_set_size(btn_manage, 300, 44);
    lv_obj_align(btn_manage, LV_ALIGN_TOP_LEFT, 0, 110);
    lv_obj_set_style_bg_color(btn_manage, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_manage, 8, 0);
    lv_obj_add_event_cb(btn_manage, btn_manage_wifi_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_manage = lv_label_create(btn_manage);
    lv_label_set_text(lbl_manage, LV_SYMBOL_WIFI "  Manage Wi-Fi");
    UIManager::styleLabel(lbl_manage, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_manage);

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
}

void uiShowSettingsClock() {
    if (Serial) Serial.println("UI Clock: uiShowSettingsClock called");
    if (!scr) buildSettingsClockScreen();
    lv_scr_load(scr);
    // NOTE: NO WIFI_SCAN here — scanning is done only from the dedicated WiFi
    // Setup screen (ui_wifi_setup.cpp) via an explicit user button tap.
    // Firing WIFI_SCAN on every open was blocking the WROOM radio and breaking
    // the ESP-NOW bidirectional comms.
}
