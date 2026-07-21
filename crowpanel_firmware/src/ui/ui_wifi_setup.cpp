#include "ui_wifi_setup.h"
#include "ui_manager.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"
#include <ArduinoJson.h>

// ── static object handles ──────────────────────────────────────────────────
static lv_obj_t *scr_wifi       = NULL;
static lv_obj_t *ta_ssid        = NULL;
static lv_obj_t *ta_pass        = NULL;
static lv_obj_t *lbl_status     = NULL;   // pill label (Online/Offline)
static lv_obj_t *lbl_err        = NULL;   // footer status message
static lv_obj_t *panel_networks = NULL;   // scrollable list of found SSIDs
static lv_obj_t *lbl_scan_btn   = NULL;   // ref to the Scan button label
static lv_obj_t *pill           = NULL;   // status pill object
static lv_obj_t *lbl_hint       = NULL;   // empty state hint
static lv_obj_t *lbl_step       = NULL;   // "Step 1 of 3"
static lv_obj_t *lbl_cont       = NULL;   // continue button label

const int MAX_NETWORKS = 5;
static lv_obj_t *network_rows[MAX_NETWORKS];
static lv_obj_t *network_labels[MAX_NETWORKS];

static lv_obj_t *saved_network_rows[5];
static lv_obj_t *saved_network_labels[5];
static lv_obj_t *btn_continue = NULL;
static lv_obj_t *wifi_img_pill = NULL;
extern const lv_img_dsc_t icon_wifi;

static bool wifi_is_connected = false;

// Scan-timeout timer: clears "Scanning..." if WROOM never replies
static lv_timer_t *scan_timeout_timer = NULL;

extern void uiShowActivation();

// ── Layout constants (800 x 480) ───────────────────────────────────────────
#define HEADER_H    72   // matches UIManager::buildHeader height
#define FOOTER_H    54
#define DIVIDER_X   400
#define PANEL_PAD   18
#define CONTENT_Y   (HEADER_H + 4)
#define CONTENT_H   (480 - HEADER_H - FOOTER_H - 4)

// ── helper: set footer status text and colour ──────────────────────────────
static void setErr(const char* txt, bool success) {
    if (!lbl_err) return;
    lv_label_set_text(lbl_err, txt);
    lv_obj_set_style_text_color(lbl_err,
        success ? UIManager::rgb(COLOR_GREEN_MAIN) : UIManager::rgb(COLOR_DANGER), 0);
}



// ── text area focus → show keyboard ───────────────────────────────────────
static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_current_target(e);
    if (code == LV_EVENT_FOCUSED) {
        UIManager::openKeyboardFor(ta);
    }
    if (code == LV_EVENT_VALUE_CHANGED && ta == ta_ssid) {
        // We no longer clear saved credentials just because the user types something.
    }
}

// ── show password button ──────────────────────────────────────────────────
static void btn_show_pass_cb(lv_event_t *e) {
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lv_textarea_get_password_mode(ta_pass)) {
        lv_textarea_set_password_mode(ta_pass, false);
        lv_label_set_text(lbl, LV_SYMBOL_EYE_CLOSE);
    } else {
        lv_textarea_set_password_mode(ta_pass, true);
        lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN);
    }
}

// ── saved network row tap → auto connect ──────────────────────────────────
static void saved_network_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        int idx = (intptr_t)lv_event_get_user_data(e);
        String ssid = DataManager::getWifiSsid(idx);
        String pass = DataManager::getWifiPass(idx);
        
        lv_textarea_set_text(ta_ssid, ssid.c_str());
        lv_textarea_set_text(ta_pass, pass.c_str());
        
        setErr(String("Connecting to " + ssid + "...").c_str(), true);
        
        StaticJsonDocument<256> doc;
        doc["cmd"]  = "WIFI_CONNECT";
        doc["ssid"] = ssid;
        doc["pass"] = pass;
        String out;
        serializeJson(doc, out);
        CommManager::sendCommand(out);
    }
}

// ── network row tap → fill SSID field ─────────────────────────────────────
static void network_row_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *row = (lv_obj_t *)lv_event_get_current_target(e);
        lv_obj_t *lbl = lv_obj_get_child(row, 1);  // child 0=ico, 1=SSID name, 2=chevron
        if (lbl) {
            const char *name = lv_label_get_text(lbl);
            lv_textarea_set_text(ta_ssid, name);
            
            // Check if we have a saved password for this network
            String savedPass = "";
            for (int i = 0; i < DataManager::getSavedWifiCount(); i++) {
                if (DataManager::getWifiSsid(i) == String(name)) {
                    savedPass = DataManager::getWifiPass(i);
                    break;
                }
            }
            lv_textarea_set_text(ta_pass, savedPass.c_str());

            for (int i = 0; i < MAX_NETWORKS; i++) {
                if (network_rows[i]) {
                    lv_obj_set_style_bg_color(network_rows[i], UIManager::rgb(i % 2 == 0 ? 0xFFFFFF : COLOR_STROKE), 0);
                }
            }
            lv_obj_set_style_bg_color(row, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
        }
        setErr("Network selected \xe2\x80\x94 enter password and tap Connect", true);
    }
}

// ── scan timeout callback ──────────────────────────────────────────────────
static void scan_timeout_cb(lv_timer_t *t) {
    const char *cur = lbl_err ? lv_label_get_text(lbl_err) : "";
    if (strcmp(cur, "Scanning for networks...") == 0) {
        setErr("Scan timed out. Tap Scan to retry.", false);
        if (lbl_scan_btn) lv_label_set_text(lbl_scan_btn, LV_SYMBOL_REFRESH " Scan");
    }
    lv_timer_del(t);
    scan_timeout_timer = NULL;
}

// ── Scan button ────────────────────────────────────────────────────────────
static void btn_scan_cb(lv_event_t *e) {
    if (lbl_scan_btn) lv_label_set_text(lbl_scan_btn, LV_SYMBOL_REFRESH " ...");
    setErr("Scanning for networks...", true);
    lv_obj_set_style_text_color(lbl_err, UIManager::rgb(COLOR_GREEN_MAIN), 0);

    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
        scan_timeout_timer = NULL;
    }
    scan_timeout_timer = lv_timer_create(scan_timeout_cb, 25000, NULL);
    lv_timer_set_repeat_count(scan_timeout_timer, 1);

    CommManager::sendCommand("{\"cmd\":\"WIFI_SCAN\"}");
}

// ── Connect button ─────────────────────────────────────────────────────────
static void btn_connect_cb(lv_event_t *e) {
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);

    if (!ssid || strlen(ssid) == 0) {
        setErr("Network name (SSID) cannot be empty!", false);
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
        setErr("Connect to a WiFi network first.", false);
    }
}

static void btn_header_back_cb(lv_event_t *e) {
    UIManager::showSettings();
}

// ── Public: WIFI_STATUS from CommManager ───────────────────────────────────
void uiWifiUpdateStatus(bool connected) {
    wifi_is_connected = connected;
    if (!lbl_status || !pill) return;

    if (connected) {
        lv_label_set_text(lbl_status, "Online");
        lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
        lv_obj_set_style_text_color(lbl_status, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        if (wifi_img_pill) lv_obj_set_style_img_recolor(wifi_img_pill, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        
        if (ta_ssid && ta_pass) {
            const char *ssid = lv_textarea_get_text(ta_ssid);
            const char *pass = lv_textarea_get_text(ta_pass);
            if (ssid && strlen(ssid) > 0) {
                DataManager::saveWifiCredentials(String(ssid), String(pass));
            }
        }
        
        if (!DataManager::isActivated()) {
            setErr("Connected! Proceeding to Registration...", true);
            if (btn_continue) lv_obj_add_flag(btn_continue, LV_OBJ_FLAG_HIDDEN);
            DataManager::setWifiConfigured(true);
            uiShowActivation(); // Auto-proceed when connected during setup
        } else {
            setErr("Connected!", true);
        }
    } else {
        lv_label_set_text(lbl_status, "Offline");
        lv_obj_set_style_bg_color(pill, UIManager::rgb(0xFFE5E5), 0);
        lv_obj_set_style_text_color(lbl_status, UIManager::rgb(COLOR_DANGER), 0);
        if (wifi_img_pill) lv_obj_set_style_img_recolor(wifi_img_pill, UIManager::rgb(COLOR_DANGER), 0);
        setErr("Connection failed. Check SSID / password.", false);
    }
}

// ── Public: WIFI_SCAN_RESULT from CommManager ──────────────────────────────
void uiWifiUpdateScanResult(const char *ssids) {
    if (!panel_networks) return;  // screen not built yet — drop silently (boot-ordering guard)
    if (scan_timeout_timer) {
        lv_timer_del(scan_timeout_timer);
        scan_timeout_timer = NULL;
    }
    if (lbl_scan_btn) lv_label_set_text(lbl_scan_btn, LV_SYMBOL_REFRESH " Scan");

    if (!ssids || strlen(ssids) == 0) {
        setErr("No networks found. Move closer to router and retry.", false);
        return;
    }

    // Hide the "Tap Scan" hint
    if (lbl_hint) lv_obj_add_flag(lbl_hint, LV_OBJ_FLAG_HIDDEN);

    String all = String(ssids);
    int start = 0, rowIndex = 0;
    while (true) {
        int comma = all.indexOf(',', start);
        String name = (comma < 0) ? all.substring(start) : all.substring(start, comma);
        name.trim();
        if (name.length() > 0 && rowIndex < MAX_NETWORKS) {
            // Update the pre-allocated row instead of dynamically creating a new one
            if (network_labels[rowIndex]) {
                lv_label_set_text(network_labels[rowIndex], name.c_str());
            }
            if (network_rows[rowIndex]) {
                lv_obj_clear_flag(network_rows[rowIndex], LV_OBJ_FLAG_HIDDEN);
                // Reset background color in case it was previously highlighted
                lv_obj_set_style_bg_color(network_rows[rowIndex], UIManager::rgb(rowIndex % 2 == 0 ? 0xFFFFFF : COLOR_STROKE), 0);
            }
            rowIndex++;
        }
        if (comma < 0) break;
        start = comma + 1;
    }

    // Hide any remaining unused rows in the static pool
    for (int i = rowIndex; i < MAX_NETWORKS; i++) {
        if (network_rows[i]) {
            lv_obj_add_flag(network_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (rowIndex >= MAX_NETWORKS) {
        setErr(String("Showing " + String(MAX_NETWORKS) + " networks. More available but not shown.").c_str(), true);
    } else {
        setErr("Tap a network to select it, then enter the password.", true);
    }
}

// ── Build the screen ───────────────────────────────────────────────────────
void buildWifiSetupScreen() {
    // ── Screen base ──
    scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_wifi, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(scr_wifi, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_wifi, LV_SCROLLBAR_MODE_OFF);

    // ════════════════════════════════════════════════════════════
    // HEADER — shared style (transparent bg, centered title)
    // ════════════════════════════════════════════════════════════
    lv_event_cb_t back_cb = NULL;
    if (DataManager::isActivated()) {
        back_cb = btn_header_back_cb;
    }
    UIManager::buildHeader(scr_wifi, "WiFi Setup", NULL, back_cb, false);

    // Step label placed directly on scr_wifi so it is never clipped by the header bounds
    lbl_step = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_step, ". . . Step 1 of 3");
    UIManager::styleLabel(lbl_step, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(lbl_step, LV_ALIGN_TOP_MID, 0, 50);

    // Status pill — top-right, same position as other screens
    pill = lv_obj_create(scr_wifi);
    lv_obj_set_size(pill, 140, 36);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -20, 18);
    lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_radius(pill, 18, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    wifi_img_pill = lv_img_create(pill);
    lv_img_set_src(wifi_img_pill, &icon_wifi);
    lv_obj_set_style_img_recolor_opa(wifi_img_pill, LV_OPA_COVER, 0);
    lv_obj_align(wifi_img_pill, LV_ALIGN_LEFT_MID, 15, 0);

    lbl_status = lv_label_create(pill);
    wifi_is_connected = DataManager::isWifiConnected();  // seed from shared state
    if (wifi_is_connected) {
        lv_label_set_text(lbl_status, "Online");
        lv_obj_set_style_bg_color(pill, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
        lv_obj_set_style_text_color(lbl_status, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_img_recolor(wifi_img_pill, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    } else {
        lv_label_set_text(lbl_status, "Offline");
        lv_obj_set_style_text_color(lbl_status, UIManager::rgb(COLOR_GREEN_DARK), 0);
        lv_obj_set_style_img_recolor(wifi_img_pill, UIManager::rgb(COLOR_GREEN_DARK), 0);
    }
    UIManager::styleLabel(lbl_status, COLOR_GREEN_DARK, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align_to(lbl_status, wifi_img_pill, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    // ════════════════════════════════════════════════════════════
    // FOOTER BAR — light green, full width
    // ════════════════════════════════════════════════════════════
    lv_obj_t *footer = lv_obj_create(scr_wifi);
    lv_obj_set_size(footer, 800, FOOTER_H);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, UIManager::rgb(COLOR_GREEN_LIGHT), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, UIManager::rgb(COLOR_STROKE), LV_PART_MAIN);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    // Status message (footer left)
    lbl_err = lv_label_create(footer);
    lv_label_set_text(lbl_err, "Select a network or type the SSID manually.");
    UIManager::styleLabel(lbl_err, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_err, LV_ALIGN_LEFT_MID, 20, 0);
    lv_label_set_long_mode(lbl_err, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_err, 430);

    // Continue button (footer right)
    btn_continue = lv_btn_create(footer);
    lv_obj_set_size(btn_continue, 270, 38);
    lv_obj_align(btn_continue, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_bg_color(btn_continue, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_continue, 8, 0);
    lv_obj_set_style_shadow_width(btn_continue, 0, 0);
    lbl_cont = lv_label_create(btn_continue);
    lv_label_set_text(lbl_cont, "Continue to Registration " LV_SYMBOL_RIGHT);
    UIManager::styleLabel(lbl_cont, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_cont);
    lv_obj_add_event_cb(btn_continue, btn_continue_cb, LV_EVENT_CLICKED, NULL);

    if (DataManager::isActivated()) {
        lv_obj_add_flag(btn_continue, LV_OBJ_FLAG_HIDDEN); // Remove continue button if activated
    }



    // ════════════════════════════════════════════════════════════
    // LEFT PANEL — Available Networks
    // ════════════════════════════════════════════════════════════
    lv_obj_t *left_panel = lv_obj_create(scr_wifi);
    lv_obj_set_size(left_panel, DIVIDER_X - 1, CONTENT_H);
    lv_obj_align(left_panel, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(left_panel, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_panel, 0, 0);
    lv_obj_set_style_pad_all(left_panel, PANEL_PAD, 0);
    lv_obj_set_style_radius(left_panel, 0, 0);
    lv_obj_clear_flag(left_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Section header row: "Available Networks" + Scan button
    lv_obj_t *sec_left = lv_label_create(left_panel);
    lv_label_set_text(sec_left, "Available Networks");
    UIManager::styleLabel(sec_left, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_opa(sec_left, LV_OPA_80, 0);
    lv_obj_align(sec_left, LV_ALIGN_TOP_LEFT, 0, 0);

    // Scan button
    lv_obj_t *btn_scan = lv_btn_create(left_panel);
    lv_obj_set_size(btn_scan, 90, 32);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_color(btn_scan, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_scan, 6, 0);
    lv_obj_set_style_shadow_width(btn_scan, 0, 0);
    lbl_scan_btn = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan_btn, LV_SYMBOL_REFRESH " Scan");
    UIManager::styleLabel(lbl_scan_btn, 0xFFFFFF, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_scan_btn);
    lv_obj_add_event_cb(btn_scan, btn_scan_cb, LV_EVENT_CLICKED, NULL);

    // Divider under section header
    lv_obj_t *sep_l = lv_obj_create(left_panel);
    lv_obj_set_size(sep_l, lv_pct(100), 1);
    lv_obj_align(sep_l, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(sep_l, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(sep_l, 0, 0);
    lv_obj_set_style_radius(sep_l, 0, 0);
    lv_obj_set_style_pad_all(sep_l, 0, 0);
    lv_obj_clear_flag(sep_l, LV_OBJ_FLAG_SCROLLABLE);

    // Network list panel
    panel_networks = lv_obj_create(left_panel);
    lv_obj_set_size(panel_networks, lv_pct(100), CONTENT_H - 46);
    lv_obj_align(panel_networks, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(panel_networks, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel_networks, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel_networks, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(panel_networks, 1, 0);
    lv_obj_set_style_radius(panel_networks, 8, 0);
    lv_obj_set_style_pad_all(panel_networks, 0, 0);
    lv_obj_set_flex_flow(panel_networks, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(panel_networks, LV_LAYOUT_FLEX);
    lv_obj_set_scrollbar_mode(panel_networks, LV_SCROLLBAR_MODE_AUTO);

    // Empty state hint
    lbl_hint = lv_label_create(panel_networks);
    lv_label_set_text(lbl_hint, "Tap \"Scan\" to discover networks");
    UIManager::styleLabel(lbl_hint, 0xAAAAAA, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(lbl_hint, lv_pct(100));
    lv_obj_set_style_pad_top(lbl_hint, 30, 0);

    // Pre-allocate saved network rows
    int savedCount = DataManager::getSavedWifiCount();
    for (int i = 0; i < 5; i++) {
        saved_network_rows[i] = lv_obj_create(panel_networks);
        lv_obj_set_size(saved_network_rows[i], lv_pct(100), 40);
        lv_obj_set_style_bg_color(saved_network_rows[i], UIManager::rgb(0xFFF9E6), 0); // distinct light yellow/gold
        lv_obj_set_style_bg_opa(saved_network_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(saved_network_rows[i], 0, 0);
        lv_obj_set_style_radius(saved_network_rows[i], 0, 0);
        lv_obj_set_style_pad_left(saved_network_rows[i], 10, 0);
        lv_obj_set_style_pad_right(saved_network_rows[i], 10, 0);
        lv_obj_clear_flag(saved_network_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(saved_network_rows[i], LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *ico = lv_label_create(saved_network_rows[i]);
        lv_label_set_text(ico, LV_SYMBOL_SAVE);
        UIManager::styleLabel(ico, 0xDAA520, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        saved_network_labels[i] = lv_label_create(saved_network_rows[i]);
        UIManager::styleLabel(saved_network_labels[i], COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(saved_network_labels[i], LV_ALIGN_LEFT_MID, 28, 0);

        lv_obj_t *chev = lv_label_create(saved_network_rows[i]);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        UIManager::styleLabel(chev, 0xAAAAAA, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(saved_network_rows[i], saved_network_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        
        if (i >= savedCount) {
            lv_obj_add_flag(saved_network_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(saved_network_labels[i], DataManager::getWifiSsid(i).c_str());
        }
    }

    // Pre-allocate the network rows to avoid dynamic heap fragmentation
    for (int i = 0; i < MAX_NETWORKS; i++) {
        network_rows[i] = lv_obj_create(panel_networks);
        lv_obj_set_size(network_rows[i], lv_pct(100), 40);
        lv_obj_set_style_bg_color(network_rows[i], UIManager::rgb(i % 2 == 0 ? 0xFFFFFF : COLOR_STROKE), 0);
        lv_obj_set_style_bg_opa(network_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(network_rows[i], 0, 0);
        lv_obj_set_style_radius(network_rows[i], 0, 0);
        lv_obj_set_style_pad_left(network_rows[i], 10, 0);
        lv_obj_set_style_pad_right(network_rows[i], 10, 0);
        lv_obj_clear_flag(network_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(network_rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(network_rows[i], LV_OBJ_FLAG_HIDDEN); // Hidden by default

        // Wi-Fi icon
        lv_obj_t *ico = lv_img_create(network_rows[i]);
        lv_img_set_src(ico, &icon_wifi);
        lv_obj_set_style_img_recolor(ico, UIManager::rgb(COLOR_GREEN_MAIN), 0);
        lv_obj_set_style_img_recolor_opa(ico, LV_OPA_COVER, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        // SSID name
        network_labels[i] = lv_label_create(network_rows[i]);
        lv_label_set_text(network_labels[i], "");
        UIManager::styleLabel(network_labels[i], COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(network_labels[i], LV_ALIGN_LEFT_MID, 28, 0);

        // Chevron
        lv_obj_t *chev = lv_label_create(network_rows[i]);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        UIManager::styleLabel(chev, 0xAAAAAA, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(network_rows[i], network_row_cb, LV_EVENT_CLICKED, NULL);
    }

    // ════════════════════════════════════════════════════════════
    // VERTICAL DIVIDER
    // ════════════════════════════════════════════════════════════
    lv_obj_t *vdiv = lv_obj_create(scr_wifi);
    lv_obj_set_size(vdiv, 1, CONTENT_H);
    lv_obj_align(vdiv, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(vdiv, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(vdiv, 0, 0);
    lv_obj_set_style_radius(vdiv, 0, 0);
    lv_obj_set_style_pad_all(vdiv, 0, 0);
    lv_obj_clear_flag(vdiv, LV_OBJ_FLAG_SCROLLABLE);

    // ════════════════════════════════════════════════════════════
    // RIGHT PANEL — Network Credentials
    // ════════════════════════════════════════════════════════════
    lv_obj_t *right_panel = lv_obj_create(scr_wifi);
    lv_obj_set_size(right_panel, 800 - DIVIDER_X, CONTENT_H);
    lv_obj_align(right_panel, LV_ALIGN_TOP_RIGHT, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(right_panel, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, PANEL_PAD, 0);
    lv_obj_set_style_radius(right_panel, 0, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Section label
    lv_obj_t *sec_right = lv_label_create(right_panel);
    lv_label_set_text(sec_right, "Network Credentials");
    UIManager::styleLabel(sec_right, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_opa(sec_right, LV_OPA_80, 0);
    lv_obj_align(sec_right, LV_ALIGN_TOP_LEFT, 0, 0);

    // Divider under section header
    lv_obj_t *sep_r = lv_obj_create(right_panel);
    lv_obj_set_size(sep_r, lv_pct(100), 1);
    lv_obj_align(sep_r, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(sep_r, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(sep_r, 0, 0);
    lv_obj_set_style_radius(sep_r, 0, 0);
    lv_obj_set_style_pad_all(sep_r, 0, 0);
    lv_obj_clear_flag(sep_r, LV_OBJ_FLAG_SCROLLABLE);

    // ── SSID label ──
    lv_obj_t *lbl_ssid_title = lv_label_create(right_panel);
    lv_label_set_text(lbl_ssid_title, "Network Name (SSID)");
    UIManager::styleLabel(lbl_ssid_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_ssid_title, LV_ALIGN_TOP_LEFT, 0, 42);

    // ── SSID input ──
    ta_ssid = lv_textarea_create(right_panel);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_obj_set_size(ta_ssid, lv_pct(100), 46);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_MID, 0, 64);
    UIManager::styleTextArea(ta_ssid);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);
    if (DataManager::hasSavedWifi()) {
        lv_textarea_set_text(ta_ssid, DataManager::getWifiSsid().c_str());
    }

    // ── Password label ──
    lv_obj_t *lbl_pass_title = lv_label_create(right_panel);
    lv_label_set_text(lbl_pass_title, "Password");
    UIManager::styleLabel(lbl_pass_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_pass_title, LV_ALIGN_TOP_LEFT, 0, 124);

    // ── Password input — full width, eye button overlaid on right edge ──
    ta_pass = lv_textarea_create(right_panel);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_obj_set_size(ta_pass, lv_pct(100), 46);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 0, 146);
    UIManager::styleTextArea(ta_pass);
    // Extra right padding so typed text doesn't hide under the eye button
    lv_obj_set_style_pad_right(ta_pass, 52, 0);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);
    if (DataManager::hasSavedWifi()) {
        lv_textarea_set_text(ta_pass, DataManager::getWifiPass().c_str());
    }

    // ── Show/Hide password button — overlaid on the right edge of ta_pass ──
    lv_obj_t *btn_show = lv_btn_create(right_panel);
    lv_obj_set_size(btn_show, 44, 44);
    lv_obj_align(btn_show, LV_ALIGN_TOP_RIGHT, -1, 147);  // 1px inset from edge
    lv_obj_set_style_bg_color(btn_show, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_width(btn_show, 0, 0);
    lv_obj_set_style_radius(btn_show, 7, 0);
    lv_obj_set_style_shadow_width(btn_show, 0, 0);
    lv_obj_t *lbl_show = lv_label_create(btn_show);
    lv_label_set_text(lbl_show, LV_SYMBOL_EYE_OPEN);
    UIManager::styleLabel(lbl_show, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_show);
    lv_obj_add_event_cb(btn_show, btn_show_pass_cb, LV_EVENT_CLICKED, NULL);

    // ── Connect button ──
    lv_obj_t *btn_connect = lv_btn_create(right_panel);
    lv_obj_set_size(btn_connect, lv_pct(100), 48);
    lv_obj_align(btn_connect, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_connect, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_connect, 10, 0);
    lv_obj_set_style_shadow_width(btn_connect, 0, 0);
    lv_obj_t *lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connect");
    UIManager::styleLabel(lbl_connect, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_connect);
    lv_obj_add_event_cb(btn_connect, btn_connect_cb, LV_EVENT_CLICKED, NULL);

}

void uiShowWifiSetup() {
    if (lbl_step) {
        if (DataManager::isActivated()) {
            lv_obj_add_flag(lbl_step, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(lbl_step, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (lbl_cont) {
        if (DataManager::isActivated()) {
            lv_obj_add_flag(btn_continue, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(btn_continue, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lbl_cont, "Continue to Registration " LV_SYMBOL_RIGHT);
        }
    }
    
    // Check if we came from settings and it's not connected. 
    // We already allow going back in btn_continue_cb by checking DataManager::isActivated().
    
    lv_scr_load(scr_wifi);
}
