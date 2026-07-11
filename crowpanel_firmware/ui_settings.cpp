#include "ui_settings.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"
#include <ArduinoJson.h>

// ── Static object handles ──────────────────────────────────────────────────
lv_obj_t *scr_settings = NULL;
static lv_obj_t *page_cont = NULL; // Main scrollable container

// Top Grid Elements
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *sw_tz_auto = NULL;
static lv_obj_t *sw_time_auto = NULL;

// Form Elements
static lv_obj_t *ta_dev_name = NULL;
static lv_obj_t *ta_dev_id = NULL;
static lv_obj_t *dd_wifi_net = NULL;
static lv_obj_t *ta_wifi_pass = NULL;
static lv_obj_t *ta_api_end = NULL;
static lv_obj_t *ta_dev_token = NULL;

// Status & Action
static lv_obj_t *lbl_conn_status = NULL;
static lv_obj_t *kb_settings = NULL;
static lv_obj_t *active_ta = NULL;

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_48);

extern const lv_img_dsc_t icon_settings;
extern const lv_font_t lv_font_montserrat_144;

// ── Callbacks ──────────────────────────────────────────────────────────────

static void destroy_settings() {
    if (scr_settings) {
        lv_obj_del(scr_settings);
        scr_settings = NULL;
    }
}

static void btn_back_cb(lv_event_t * e) {
    UIManager::showMainMenu();
    destroy_settings();
}

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t*)lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_settings, ta);
        lv_obj_clear_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);
        active_ta = ta;
        
        // Ensure textarea is visible (scroll to it)
        lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    }
}

static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);
        if(active_ta) {
            lv_obj_clear_state(active_ta, LV_STATE_FOCUSED);
            active_ta = NULL;
        }
    }
}

static void btn_factory_reset_cb(lv_event_t * e) {
    // Factory reset logic here
    CommManager::sendCommand("{\"cmd\":\"FACTORY_RESET\"}");
    DataManager::factoryReset();
}

static void btn_cancel_cb(lv_event_t * e) {
    UIManager::showMainMenu();
    destroy_settings();
}

static void btn_save_cb(lv_event_t * e) {
    // Save logic here
    UIManager::showMainMenu();
    destroy_settings();
}

// ── Screen Builder ─────────────────────────────────────────────────────────

void buildSettingsScreen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, UIManager::rgb(0x222222), 0);

    // Main scrollable white card (centered with margin)
    page_cont = lv_obj_create(scr_settings);
    lv_obj_set_size(page_cont, 760, 480);
    lv_obj_align(page_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(page_cont, lv_color_white(), 0);
    lv_obj_set_style_radius(page_cont, 0, 0); 
    lv_obj_set_style_border_width(page_cont, 0, 0);
    lv_obj_set_style_pad_all(page_cont, 20, 0);
    
    // Use Flex layout for the main column
    lv_obj_set_layout(page_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(page_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(page_cont, 20, 0);

    // ── 1. Top Header ──────────────────────────────────────────────────────────
    lv_obj_t *header_cont = lv_obj_create(page_cont);
    lv_obj_set_size(header_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    lv_obj_set_style_pad_all(header_cont, 0, 0);

    // Back Button (Green rectangle)
    lv_obj_t *btn_back = lv_btn_create(header_cont);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lbl_back, lv_color_white(), 0);
    lv_obj_align(lbl_back, LV_ALIGN_CENTER, 0, 0);

    // Header Title Area
    lv_obj_t *title_cont = lv_obj_create(header_cont);
    lv_obj_set_size(title_cont, 300, LV_SIZE_CONTENT);
    lv_obj_align(title_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(title_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_cont, 0, 0);
    lv_obj_set_style_pad_all(title_cont, 0, 0);

    lv_obj_t *icon_gear = lv_img_create(title_cont);
    lv_img_set_src(icon_gear, &icon_settings); // Assuming green settings icon exists
    lv_obj_set_style_img_recolor(icon_gear, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_img_recolor_opa(icon_gear, LV_OPA_COVER, 0);
    // Make icon small enough for header
    lv_img_set_zoom(icon_gear, 128); // half size
    lv_obj_align(icon_gear, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *lbl_title = lv_label_create(title_cont);
    lv_label_set_text(lbl_title, "Device settings");
    UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 50, 0);

    lv_obj_t *lbl_subtitle = lv_label_create(title_cont);
    lv_label_set_text(lbl_subtitle, "Main office device - ESP32-A1"); // Placeholder
    UIManager::styleLabel(lbl_subtitle, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_font(lbl_subtitle, &lv_font_montserrat_14, 0); 
    lv_obj_align(lbl_subtitle, LV_ALIGN_TOP_LEFT, 50, 25);

    // ── 2. Top Grid (3 columns) ────────────────────────────────────────────────
    lv_obj_t *grid_cont = lv_obj_create(page_cont);
    lv_obj_set_size(grid_cont, LV_PCT(100), 220);
    lv_obj_set_style_bg_opa(grid_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_cont, 0, 0);
    lv_obj_set_style_pad_all(grid_cont, 0, 0);
    
    // Col 1: Clock & Toggles
    lv_obj_t *col1 = lv_obj_create(grid_cont);
    lv_obj_set_size(col1, 280, LV_PCT(100));
    lv_obj_align(col1, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(col1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col1, 0, 0);
    lv_obj_set_style_pad_all(col1, 0, 0);

    lbl_time = lv_label_create(col1);
    lv_label_set_text(lbl_time, "12:00 PM"); // Will use smaller font than 144px for this screen, maybe 48px
    UIManager::styleLabel(lbl_time, COLOR_TEXT_MAIN, &lv_font_montserrat_48, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 10, 0);

    lbl_date = lv_label_create(col1);
    lv_label_set_text(lbl_date, "Wednesday, July 1, 2026");
    UIManager::styleLabel(lbl_date, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_LEFT, 12, 50);

    // Toggle 1
    lv_obj_t *tgl_cont1 = lv_obj_create(col1);
    lv_obj_set_size(tgl_cont1, 270, 50);
    lv_obj_align(tgl_cont1, LV_ALIGN_TOP_LEFT, 10, 80);
    lv_obj_set_style_bg_color(tgl_cont1, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(tgl_cont1, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(tgl_cont1, 1, 0);

    lv_obj_t *lbl_tgl1 = lv_label_create(tgl_cont1);
    lv_label_set_text(lbl_tgl1, "Set time zone automatically");
    UIManager::styleLabel(lbl_tgl1, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_tgl1, LV_ALIGN_LEFT_MID, 0, 0);

    sw_tz_auto = lv_switch_create(tgl_cont1);
    lv_obj_align(sw_tz_auto, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_state(sw_tz_auto, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_tz_auto, UIManager::rgb(COLOR_GREEN_MAIN), LV_PART_INDICATOR | LV_STATE_CHECKED);

    // Toggle 2
    lv_obj_t *tgl_cont2 = lv_obj_create(col1);
    lv_obj_set_size(tgl_cont2, 270, 50);
    lv_obj_align(tgl_cont2, LV_ALIGN_TOP_LEFT, 10, 140);
    lv_obj_set_style_bg_color(tgl_cont2, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(tgl_cont2, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(tgl_cont2, 1, 0);

    lv_obj_t *lbl_tgl2 = lv_label_create(tgl_cont2);
    lv_label_set_text(lbl_tgl2, "Set time automatically");
    UIManager::styleLabel(lbl_tgl2, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_tgl2, LV_ALIGN_LEFT_MID, 0, 0);

    sw_time_auto = lv_switch_create(tgl_cont2);
    lv_obj_align(sw_time_auto, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_state(sw_time_auto, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_time_auto, UIManager::rgb(COLOR_GREEN_MAIN), LV_PART_INDICATOR | LV_STATE_CHECKED);

    // Col 2: Timezone and Region
    lv_obj_t *col2 = lv_obj_create(grid_cont);
    lv_obj_set_size(col2, 240, LV_PCT(100));
    lv_obj_align(col2, LV_ALIGN_TOP_LEFT, 300, 0);
    lv_obj_set_style_bg_opa(col2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col2, 0, 0);
    lv_obj_set_style_pad_all(col2, 0, 0);

    lv_obj_t *lbl_tz_title = lv_label_create(col2);
    lv_label_set_text(lbl_tz_title, "Time zone");
    UIManager::styleLabel(lbl_tz_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_tz_title, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *lbl_tz_val = lv_label_create(col2);
    lv_label_set_text(lbl_tz_val, "(UTC +08:00) Kuala Lumpur, Singapore");
    lv_obj_set_width(lbl_tz_val, 200); // Allow wrap
    lv_label_set_long_mode(lbl_tz_val, LV_LABEL_LONG_WRAP);
    UIManager::styleLabel(lbl_tz_val, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_tz_val, LV_ALIGN_TOP_LEFT, 25, 40);

    lv_obj_t *lbl_reg_title = lv_label_create(col2);
    lv_label_set_text(lbl_reg_title, "Region");
    UIManager::styleLabel(lbl_reg_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_reg_title, LV_ALIGN_TOP_LEFT, 0, 100);

    lv_obj_t *lbl_reg_val = lv_label_create(col2);
    lv_label_set_text(lbl_reg_val, "Philippines");
    UIManager::styleLabel(lbl_reg_val, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_reg_val, LV_ALIGN_TOP_LEFT, 25, 120);

    // Col 3: Device Information Card
    lv_obj_t *col3 = lv_obj_create(grid_cont);
    lv_obj_set_size(col3, 170, 200);
    lv_obj_align(col3, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(col3, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(col3, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(col3, 1, 0);
    lv_obj_set_style_pad_all(col3, 15, 0);

    lv_obj_t *lbl_info_title = lv_label_create(col3);
    lv_label_set_text(lbl_info_title, "Device information");
    UIManager::styleLabel(lbl_info_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    
    // Use Flex layout for the content inside info card
    lv_obj_set_layout(col3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col3, 10, 0);

    auto create_info_row = [](lv_obj_t *parent, const char *title, const char *val) {
        lv_obj_t *c = lv_obj_create(parent);
        lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);

        lv_obj_t *lt = lv_label_create(c);
        lv_label_set_text(lt, title);
        UIManager::styleLabel(lt, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lt, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *lv = lv_label_create(c);
        lv_label_set_text(lv, val);
        UIManager::styleLabel(lv, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lv, LV_ALIGN_TOP_LEFT, 0, 18);
    };

    create_info_row(col3, "Firmware version", "v1.0.3");
    create_info_row(col3, "Battery", "98%");
    create_info_row(col3, "Last synced", "07/01/2026   8:15 AM");


    // ── 3. Status Card ─────────────────────────────────────────────────────────
    lv_obj_t *status_card = lv_obj_create(page_cont);
    lv_obj_set_size(status_card, LV_PCT(100), 70);
    lv_obj_set_style_bg_color(status_card, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(status_card, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_radius(status_card, 8, 0);
    lv_obj_set_style_pad_all(status_card, 15, 0);

    lv_obj_t *lbl_conn = lv_label_create(status_card);
    lv_label_set_text(lbl_conn, LV_SYMBOL_WIFI " Connected - Synced");
    UIManager::styleLabel(lbl_conn, COLOR_GREEN_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_conn, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_view_stat = lv_label_create(status_card);
    lv_label_set_text(lbl_view_stat, "View full status >");
    UIManager::styleLabel(lbl_view_stat, 0x0099ff, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(lbl_view_stat, LV_ALIGN_TOP_RIGHT, 0, 0);

    auto add_status_col = [](lv_obj_t *parent, const char* lbl, const char* val, int x_ofs) {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_text(l, lbl);
        UIManager::styleLabel(l, 0x999999, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, x_ofs, 0);
        
        lv_obj_t *v = lv_label_create(parent);
        lv_label_set_text(v, val);
        UIManager::styleLabel(v, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, x_ofs, 20);
    };
    
    add_status_col(status_card, "Pending", "0", 0);
    add_status_col(status_card, "Last synced", "2mins ago", 100);
    add_status_col(status_card, "Buffer used", "9%", 250);

    // Form builder helper
    auto create_form_section = [](lv_obj_t *parent, const char *title) -> lv_obj_t* {
        lv_obj_t *sec = lv_obj_create(parent);
        lv_obj_set_size(sec, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(sec, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sec, 0, 0);
        lv_obj_set_style_border_color(sec, UIManager::rgb(COLOR_STROKE), 0);
        lv_obj_set_style_border_width(sec, 1, 0);
        lv_obj_set_style_border_side(sec, LV_BORDER_SIDE_TOP, 0); // Top border only
        lv_obj_set_style_pad_all(sec, 0, 0);
        lv_obj_set_style_pad_top(sec, 20, 0);

        lv_obj_t *lbl_title = lv_label_create(sec);
        lv_label_set_text(lbl_title, title);
        UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0); // Boldish
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

        return sec;
    };

    auto create_input_field = [](lv_obj_t *parent, const char *label, const char *placeholder, int y_ofs, int w, int x_ofs, bool is_pw) -> lv_obj_t* {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, label);
        UIManager::styleLabel(lbl, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs);

        lv_obj_t *ta = lv_textarea_create(parent);
        lv_obj_set_size(ta, w, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_LEFT, x_ofs, y_ofs + 20);
        lv_textarea_set_placeholder_text(ta, placeholder);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, is_pw);
        lv_obj_set_style_border_color(ta, UIManager::rgb(COLOR_STROKE), 0);
        lv_obj_set_style_radius(ta, 8, 0);
        lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
        return ta;
    };

    // ── 4. Device Identity ─────────────────────────────────────────────────────
    lv_obj_t *sec_id = create_form_section(page_cont, "Device identity");
    
    ta_dev_name = create_input_field(sec_id, "Device name", "Main office device", 30, 340, 0, false);
    ta_dev_id = create_input_field(sec_id, "Device ID", "ESP32-A1", 30, 340, 360, false);
    lv_obj_set_height(sec_id, 100);

    // ── 5. Wi-Fi network ───────────────────────────────────────────────────────
    lv_obj_t *sec_wifi = create_form_section(page_cont, "Wi-Fi network");
    
    // Custom connected indicator top right
    lv_obj_t *lbl_wifi_conn = lv_label_create(sec_wifi);
    lv_label_set_text(lbl_wifi_conn, LV_SYMBOL_WIFI " Connected");
    UIManager::styleLabel(lbl_wifi_conn, COLOR_GREEN_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(lbl_wifi_conn, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Dropdown for Wi-Fi
    lv_obj_t *lbl_net_name = lv_label_create(sec_wifi);
    lv_label_set_text(lbl_net_name, "Network name (SSID)");
    UIManager::styleLabel(lbl_net_name, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_net_name, LV_ALIGN_TOP_LEFT, 0, 30);

    dd_wifi_net = lv_dropdown_create(sec_wifi);
    lv_dropdown_set_options(dd_wifi_net, "Office-Wifi-5G\nGuest-Net\nScanner");
    lv_obj_set_size(dd_wifi_net, 340, 40);
    lv_obj_align(dd_wifi_net, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_obj_set_style_border_color(dd_wifi_net, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_radius(dd_wifi_net, 8, 0);

    // Password field with eye
    ta_wifi_pass = create_input_field(sec_wifi, "Password", "••••••••••••", 30, 340, 360, true);
    
    // Add eye icon button inside password field or next to it
    lv_obj_t *btn_eye = lv_btn_create(sec_wifi);
    lv_obj_set_size(btn_eye, 30, 30);
    lv_obj_align_to(btn_eye, ta_wifi_pass, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_opa(btn_eye, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_eye, 0, 0);
    lv_obj_t *lbl_eye = lv_label_create(btn_eye);
    lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(lbl_eye, UIManager::rgb(0x999999), 0);
    lv_obj_center(lbl_eye);
    
    auto toggle_eye_cb = [](lv_event_t * e) {
        lv_obj_t * ta = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_t * lbl = lv_obj_get_child((lv_obj_t*)lv_event_get_target(e), 0);
        if(lv_textarea_get_password_mode(ta)) {
            lv_textarea_set_password_mode(ta, false);
            lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN);
        } else {
            lv_textarea_set_password_mode(ta, true);
            lv_label_set_text(lbl, LV_SYMBOL_EYE_CLOSE);
        }
    };
    lv_obj_add_event_cb(btn_eye, toggle_eye_cb, LV_EVENT_CLICKED, ta_wifi_pass);

    // Buttons
    lv_obj_t *btn_test_conn = lv_btn_create(sec_wifi);
    lv_obj_set_size(btn_test_conn, 160, 40);
    lv_obj_align(btn_test_conn, LV_ALIGN_TOP_LEFT, 360, 100);
    lv_obj_set_style_bg_color(btn_test_conn, UIManager::rgb(COLOR_WIFI_BG), 0);
    lv_obj_set_style_border_color(btn_test_conn, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_border_width(btn_test_conn, 1, 0);
    lv_obj_t *lbl_test = lv_label_create(btn_test_conn);
    lv_label_set_text(lbl_test, "Test connection");
    UIManager::styleLabel(lbl_test, COLOR_GREEN_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_test);

    lv_obj_t *btn_disc = lv_btn_create(sec_wifi);
    lv_obj_set_size(btn_disc, 160, 40);
    lv_obj_align(btn_disc, LV_ALIGN_TOP_LEFT, 540, 100);
    lv_obj_set_style_bg_color(btn_disc, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_disc, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_disc, 1, 0);
    lv_obj_t *lbl_disc = lv_label_create(btn_disc);
    lv_label_set_text(lbl_disc, "Disconnect");
    UIManager::styleLabel(lbl_disc, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_disc);

    lv_obj_set_height(sec_wifi, 160);

    // ── 6. Server connection ───────────────────────────────────────────────────
    lv_obj_t *sec_server = create_form_section(page_cont, "Server connection");
    
    ta_api_end = create_input_field(sec_server, "API endpoint", "https://api.manpro-attendance.com/v1", 30, 700, 0, false);
    lv_textarea_set_text(ta_api_end, "https://api.manpro-attendance.com/v1");

    ta_dev_token = create_input_field(sec_server, "Device token", "••••••••••••••••••••••••", 100, 340, 0, true);
    
    lv_obj_set_height(sec_server, 170);

    // ── 7. Danger Zone ─────────────────────────────────────────────────────────
    lv_obj_t *sec_danger = lv_obj_create(page_cont);
    lv_obj_set_size(sec_danger, 700, 120);
    lv_obj_set_style_bg_color(sec_danger, UIManager::rgb(0xfff0f3), 0); // Very light red
    lv_obj_set_style_border_color(sec_danger, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(sec_danger, 1, 0);
    lv_obj_set_style_radius(sec_danger, 8, 0);
    lv_obj_set_style_pad_all(sec_danger, 20, 0);

    lv_obj_t *lbl_danger = lv_label_create(sec_danger);
    lv_label_set_text(lbl_danger, LV_SYMBOL_WARNING " Factory reset");
    UIManager::styleLabel(lbl_danger, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_danger, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_danger_desc = lv_label_create(sec_danger);
    lv_label_set_text(lbl_danger_desc, "Unpairs this device and clears its local fingerprint database.");
    UIManager::styleLabel(lbl_danger_desc, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_danger_desc, LV_ALIGN_TOP_LEFT, 0, 25);

    lv_obj_t *btn_reset = lv_btn_create(sec_danger);
    lv_obj_set_size(btn_reset, 160, 40);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_reset, UIManager::rgb(0xffe3e8), 0);
    lv_obj_set_style_border_color(btn_reset, UIManager::rgb(COLOR_DANGER), 0);
    lv_obj_set_style_border_width(btn_reset, 1, 0);
    lv_obj_add_event_cb(btn_reset, btn_factory_reset_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_reset_btn = lv_label_create(btn_reset);
    lv_label_set_text(lbl_reset_btn, "Factory reset");
    UIManager::styleLabel(lbl_reset_btn, COLOR_DANGER, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_reset_btn);

    // ── 8. Action Bar ──────────────────────────────────────────────────────────
    lv_obj_t *sec_actions = lv_obj_create(page_cont);
    lv_obj_set_size(sec_actions, 700, 60);
    lv_obj_set_style_bg_opa(sec_actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sec_actions, 0, 0);
    lv_obj_set_style_pad_all(sec_actions, 0, 0);

    lv_obj_t *btn_cancel = lv_btn_create(sec_actions);
    lv_obj_set_size(btn_cancel, 340, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_cancel, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_add_event_cb(btn_cancel, btn_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_btn_cancel, "Cancel");
    UIManager::styleLabel(lbl_btn_cancel, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_btn_cancel);

    lv_obj_t *btn_save = lv_btn_create(sec_actions);
    lv_obj_set_size(btn_save, 340, 40);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_save, UIManager::rgb(COLOR_STROKE), 0);
    lv_obj_set_style_border_width(btn_save, 1, 0);
    lv_obj_add_event_cb(btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_btn_save, "Save changes");
    UIManager::styleLabel(lbl_btn_save, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_btn_save);

    kb_settings = lv_keyboard_create(scr_settings);
    lv_obj_add_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb_settings, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(kb_settings); // Ensure it's on top
}

void uiShowSettings() {
    lv_scr_load_anim(scr_settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void uiSettingsUpdateClock(const char *ts) {
    // Update clock logic here
}

void uiSettingsUpdateWifiList() {
    // Update wifi dropdown
}

void uiSettingsUpdateStatus(bool connected, int pending, const char* last_synced, int buffer_pct) {
    // Update status banner
}
