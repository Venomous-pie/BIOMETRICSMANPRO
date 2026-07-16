#include "ui_settings_clock.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"
#include <ArduinoJson.h>

static lv_obj_t *scr           = NULL;
static lv_obj_t *lbl_time      = NULL;
static lv_obj_t *lbl_date      = NULL;
static lv_obj_t *lbl_wifi_ssid = NULL;
static lv_obj_t *lbl_wifi_pill = NULL;
static lv_obj_t *lbl_ntp_status = NULL;  // shows last NTP sync result

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

    lv_obj_t *lbl_auto = lv_label_create(col_left);
    lv_label_set_text(lbl_auto, "Auto settings");
    UIManager::styleLabel(lbl_auto, 0x999999, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_auto, LV_ALIGN_TOP_LEFT, 0, 120);

    auto create_toggle = [](lv_obj_t *parent, const char *label, int y) {
        lv_obj_t *cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 340, 50);
        lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

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

    // Sync Now button
    lv_obj_t *btn_sync = lv_btn_create(col_left);
    lv_obj_set_size(btn_sync, 140, 36);
    lv_obj_align(btn_sync, LV_ALIGN_TOP_LEFT, 0, 260);
    lv_obj_set_style_bg_color(btn_sync, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_sync, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_sync, 1, 0);
    lv_obj_set_style_radius(btn_sync, 6, 0);
    lv_obj_add_event_cb(btn_sync, btn_sync_ntp_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_sync_btn = lv_label_create(btn_sync);
    lv_label_set_text(lbl_sync_btn, LV_SYMBOL_REFRESH " Sync Now");
    UIManager::styleLabel(lbl_sync_btn, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_sync_btn);

    // NTP Status label (to the right of the button)
    lbl_ntp_status = lv_label_create(col_left);
    lv_label_set_text(lbl_ntp_status, ""); // empty by default
    UIManager::styleLabel(lbl_ntp_status, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_ntp_status, LV_ALIGN_TOP_LEFT, 160, 270);

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

    // ── Bottom action bar ─────────────────────────────────────────────────
    lv_obj_t *bottom = lv_obj_create(body);
    lv_obj_set_size(bottom, 760, 60);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_pad_all(bottom, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_cancel = lv_btn_create(bottom);
    lv_obj_set_size(btn_cancel, 760, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_cancel, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_add_event_cb(btn_cancel, btn_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Back to Device Settings");
    UIManager::styleLabel(lbl_cancel, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cancel);
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
