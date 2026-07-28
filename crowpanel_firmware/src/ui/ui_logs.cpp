#include "ui_logs.h"
#include "ui_manager.h"
#include "../core/data_manager.h"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);

lv_obj_t *scr_logs = NULL;
lv_obj_t *logs_list_obj = NULL;
lv_obj_t *ta_search_name = NULL;
lv_obj_t *ta_search_date = NULL;
lv_timer_t *logs_search_debounce_timer = NULL;

static int current_page_logs = 0;
static int g_log_status_filter = 0;  // 0=All, 1=Time In, 2=Time Out
static lv_obj_t *lbl_page_info_logs = NULL;
static lv_obj_t *btn_prev_page_logs = NULL;
static lv_obj_t *btn_next_page_logs = NULL;
static lv_obj_t *dd_log_filter = NULL;

extern const lv_img_dsc_t icon_calendar;

static void populate_logs_list(const char* name_filter, const char* date_filter);

static void btn_back_cb(lv_event_t * e) {
  UIManager::showMainMenu();
}

static void logs_prev_page_cb(lv_event_t * e) {
    if (current_page_logs > 0) {
        current_page_logs--;
        const char *n = ta_search_name ? lv_textarea_get_text(ta_search_name) : "";
        const char *d = ta_search_date ? lv_textarea_get_text(ta_search_date) : "";
        populate_logs_list(n, d);
    }
}

static void logs_next_page_cb(lv_event_t * e) {
    current_page_logs++;
    const char *n = ta_search_name ? lv_textarea_get_text(ta_search_name) : "";
    const char *d = ta_search_date ? lv_textarea_get_text(ta_search_date) : "";
    populate_logs_list(n, d);
}

static void debounce_timer_cb(lv_timer_t * timer) {
    const char *n = ta_search_name ? lv_textarea_get_text(ta_search_name) : "";
    const char *d = ta_search_date ? lv_textarea_get_text(ta_search_date) : "";
    current_page_logs = 0;
    populate_logs_list(n, d);
    // The timer has repeat_count=1, so LVGL will auto-delete it after this callback.
    // We MUST null our reference so we don't try to lv_timer_del() it later (e.g. on exit)
    logs_search_debounce_timer = NULL;
}

static void logs_search_cb(lv_event_t * e) {
    if (logs_search_debounce_timer) {
        lv_timer_reset(logs_search_debounce_timer);
    } else {
        logs_search_debounce_timer = lv_timer_create(debounce_timer_cb, 400, NULL);
        lv_timer_set_repeat_count(logs_search_debounce_timer, 1);
    }
}

static void logs_ta_focus_cb(lv_event_t * e) {
    lv_obj_t *ta = lv_event_get_current_target(e);
    UIManager::openKeyboardFor(ta);
}

void buildLogsScreen() {
    if (scr_logs != NULL) return;

    scr_logs = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_logs, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr_logs, LV_OPA_COVER, 0);

    UIManager::buildHeader(scr_logs, "Attendance logs", "", btn_back_cb, true);

    // --- Filters Container ---
    lv_obj_t *filter_cont = lv_obj_create(scr_logs);
    lv_obj_set_size(filter_cont, 760, 60);
    lv_obj_align(filter_cont, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_opa(filter_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filter_cont, 0, 0);
    lv_obj_set_style_pad_all(filter_cont, 0, 0);
    lv_obj_clear_flag(filter_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Search by name (narrower to make room for dropdown)
    ta_search_name = lv_textarea_create(filter_cont);
    lv_obj_set_size(ta_search_name, 540, 40);
    lv_obj_align(ta_search_name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_textarea_set_placeholder_text(ta_search_name, "Search by name");
    lv_textarea_set_one_line(ta_search_name, true);
    UIManager::styleTextArea(ta_search_name);
    lv_obj_add_event_cb(ta_search_name, logs_search_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ta_search_name, logs_ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    // Status filter dropdown (Time In / Time Out / All)
    dd_log_filter = lv_dropdown_create(filter_cont);
    lv_dropdown_set_options(dd_log_filter, "All\nTime In\nTime Out");
    lv_obj_set_size(dd_log_filter, 200, 40);
    lv_obj_align(dd_log_filter, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(dd_log_filter, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_color(dd_log_filter, UIManager::rgb(0xE0E0E0), 0);
    lv_obj_set_style_border_width(dd_log_filter, 1, 0);
    lv_obj_set_style_radius(dd_log_filter, 8, 0);
    lv_obj_set_style_text_color(dd_log_filter, UIManager::rgb(COLOR_TEXT_MAIN), 0);
    lv_obj_add_event_cb(dd_log_filter, [](lv_event_t *e) {
        lv_obj_t *dd = (lv_obj_t*)lv_event_get_current_target(e);
        if (lv_dropdown_get_selected(dd) == 0) {
            lv_dropdown_set_text(dd, "Status");
        } else {
            lv_dropdown_set_text(dd, NULL);
        }
        g_log_status_filter = lv_dropdown_get_selected(dd);
        current_page_logs = 0;
        const char *n = ta_search_name ? lv_textarea_get_text(ta_search_name) : "";
        const char *d = ta_search_date ? lv_textarea_get_text(ta_search_date) : "";
        populate_logs_list(n, d);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    lv_dropdown_set_text(dd_log_filter, "Status");

    // --- Table Header ---
    lv_obj_t *col_hdr = lv_obj_create(scr_logs);
    lv_obj_set_size(col_hdr, 760, 40);
    lv_obj_align(col_hdr, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_color(col_hdr, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_width(col_hdr, 0, 0);
    lv_obj_set_style_pad_all(col_hdr, 0, 0);
    lv_obj_clear_flag(col_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ch_name = lv_label_create(col_hdr);
    lv_label_set_text(ch_name, "Name");
    UIManager::styleLabel(ch_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(ch_name, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *ch_time = lv_label_create(col_hdr);
    lv_label_set_text(ch_time, "Time");
    UIManager::styleLabel(ch_time, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(ch_time, LV_ALIGN_LEFT_MID, 400, 0);

    lv_obj_t *ch_status = lv_label_create(col_hdr);
    lv_label_set_text(ch_status, "Status");
    UIManager::styleLabel(ch_status, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(ch_status, LV_ALIGN_RIGHT_MID, -40, 0);

    // --- List Container ---
    logs_list_obj = lv_obj_create(scr_logs);
    lv_obj_set_size(logs_list_obj, 760, 210); // fits exactly 4 rows (4x52 + borders)
    lv_obj_align(logs_list_obj, LV_ALIGN_TOP_MID, 0, 185);
    lv_obj_set_style_bg_color(logs_list_obj, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_color(logs_list_obj, UIManager::rgb(0xE0E0E0), 0);
    lv_obj_set_style_border_width(logs_list_obj, 1, 0);
    lv_obj_set_style_radius(logs_list_obj, 10, 0);
    lv_obj_set_style_pad_all(logs_list_obj, 0, 0);
    lv_obj_set_style_pad_row(logs_list_obj, 0, 0);
    lv_obj_set_flex_flow(logs_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_layout(logs_list_obj, LV_LAYOUT_FLEX);
    lv_obj_set_scrollbar_mode(logs_list_obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(logs_list_obj, LV_OBJ_FLAG_SCROLLABLE);

    // --- Pagination Bar ---
    lv_obj_t *page_cont = lv_obj_create(scr_logs);
    lv_obj_set_size(page_cont, 760, 50);
    lv_obj_align(page_cont, LV_ALIGN_TOP_MID, 0, 400);
    lv_obj_set_style_bg_opa(page_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page_cont, 0, 0);
    lv_obj_set_style_pad_all(page_cont, 0, 0);
    lv_obj_clear_flag(page_cont, LV_OBJ_FLAG_SCROLLABLE);

    btn_prev_page_logs = lv_btn_create(page_cont);
    lv_obj_set_size(btn_prev_page_logs, 100, 40);
    lv_obj_align(btn_prev_page_logs, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_prev_page_logs, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_bg_color(btn_prev_page_logs, UIManager::rgb(0x999999), LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn_prev_page_logs, logs_prev_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev_page_logs);
    lv_label_set_text(lbl_prev, "Prev");
    lv_obj_center(lbl_prev);

    lbl_page_info_logs = lv_label_create(page_cont);
    lv_label_set_text(lbl_page_info_logs, "Page 1 of 1");
    UIManager::styleLabel(lbl_page_info_logs, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_page_info_logs);

    btn_next_page_logs = lv_btn_create(page_cont);
    lv_obj_set_size(btn_next_page_logs, 100, 40);
    lv_obj_align(btn_next_page_logs, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_next_page_logs, UIManager::rgb(COLOR_GREEN_MAIN), 0);
    lv_obj_set_style_bg_color(btn_next_page_logs, UIManager::rgb(0x999999), LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn_next_page_logs, logs_next_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = lv_label_create(btn_next_page_logs);
    lv_label_set_text(lbl_next, "Next");
    lv_obj_center(lbl_next);
}

static void populate_logs_list(const char* name_filter, const char* date_filter) {
    if (!logs_list_obj) return;
    lv_obj_clean(logs_list_obj);

    const AttendanceLog* db = DataManager::getAttendanceLogs();
    int count = DataManager::getAttendanceLogCount();

    String nFilt = name_filter ? String(name_filter) : "";
    nFilt.toLowerCase();
    String dFilt = date_filter ? String(date_filter) : "";
    dFilt.toLowerCase();
    
    int items_per_page = 4;
    int filtered_count = 0;

    for (int i = count - 1; i >= 0; i--) {
        String nStr = db[i].name; nStr.toLowerCase();
        String dStr = db[i].time_str; dStr.toLowerCase();
        if (nFilt.length() > 0 && nStr.indexOf(nFilt) == -1) continue;
        if (dFilt.length() > 0 && dStr.indexOf(dFilt) == -1) continue;
        if (g_log_status_filter == 1 && (db[i].action_type != 1 && db[i].action_type != 3)) continue;
        if (g_log_status_filter == 2 && (db[i].action_type != 2 && db[i].action_type != 4)) continue;
        filtered_count++;
    }

    int total_pages = (filtered_count + items_per_page - 1) / items_per_page;
    if (total_pages == 0) total_pages = 1;
    if (current_page_logs >= total_pages) current_page_logs = total_pages - 1;

    if (lbl_page_info_logs) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Page %d of %d", current_page_logs + 1, total_pages);
        lv_label_set_text(lbl_page_info_logs, buf);
    }
    
    if (btn_prev_page_logs) {
        if (current_page_logs == 0) lv_obj_add_state(btn_prev_page_logs, LV_STATE_DISABLED);
        else lv_obj_clear_state(btn_prev_page_logs, LV_STATE_DISABLED);
    }
    if (btn_next_page_logs) {
        if (current_page_logs >= total_pages - 1) lv_obj_add_state(btn_next_page_logs, LV_STATE_DISABLED);
        else lv_obj_clear_state(btn_next_page_logs, LV_STATE_DISABLED);
    }

    int start_idx = current_page_logs * items_per_page;
    int end_idx = start_idx + items_per_page;
    int current_idx = 0;

    if (filtered_count == 0) {
        lv_obj_t *empty_label = lv_label_create(logs_list_obj);
        lv_label_set_text(empty_label, "No attendance logs found");
        UIManager::styleLabel(empty_label, 0x999999, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(empty_label, 760);
        lv_obj_set_style_pad_top(empty_label, 80, 0);
        return;
    }

    for (int i = count - 1; i >= 0; i--) {
        String nStr = db[i].name; nStr.toLowerCase();
        String dStr = db[i].time_str; dStr.toLowerCase();
        if (nFilt.length() > 0 && nStr.indexOf(nFilt) == -1) continue;
        if (dFilt.length() > 0 && dStr.indexOf(dFilt) == -1) continue;
        if (g_log_status_filter == 1 && (db[i].action_type != 1 && db[i].action_type != 3)) continue;
        if (g_log_status_filter == 2 && (db[i].action_type != 2 && db[i].action_type != 4)) continue;

        if (current_idx >= start_idx && current_idx < end_idx) {
            lv_obj_t *row = lv_obj_create(logs_list_obj);
            if (!row) break;
            lv_obj_set_size(row, lv_pct(100), 52);
            lv_obj_set_style_bg_color(row, UIManager::rgb(0xFFFFFF), 0);
            lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_style_border_color(row, UIManager::rgb(0xE0E0E0), 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            // Name
            lv_obj_t *lbl_name = lv_label_create(row);
            lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(lbl_name, 250);
            lv_label_set_text(lbl_name, db[i].name.c_str());
            UIManager::styleLabel(lbl_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
            lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 20, 0);

            // Time
            lv_obj_t *lbl_time = lv_label_create(row);
            lv_label_set_text(lbl_time, db[i].time_str.c_str());
            UIManager::styleLabel(lbl_time, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
            lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 400, 0);

            // Status Pill
            lv_obj_t *pill = lv_obj_create(row);
            lv_obj_set_size(pill, 90, 30);
            lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -20, 0);
            lv_obj_set_style_radius(pill, 6, 0);
            lv_obj_set_style_pad_all(pill, 0, 0);
            lv_obj_set_style_border_width(pill, 0, 0);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *pill_lbl = lv_label_create(pill);
            if (db[i].action_type == 1) {
                lv_label_set_text(pill_lbl, "Time in");
                lv_obj_set_style_bg_color(pill, UIManager::rgb(0xDDF9E5), 0); // Light green
                UIManager::styleLabel(pill_lbl, 0x2A800F, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
            } else if (db[i].action_type == 2) {
                lv_label_set_text(pill_lbl, "Time out");
                lv_obj_set_style_bg_color(pill, UIManager::rgb(0xFCE4E4), 0); // Light red
                UIManager::styleLabel(pill_lbl, 0xC62828, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
            } else if (db[i].action_type == 3) {
                lv_label_set_text(pill_lbl, "OT in");
                lv_obj_set_style_bg_color(pill, UIManager::rgb(0xE4F2FC), 0); // Light blue
                UIManager::styleLabel(pill_lbl, 0x1565C0, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
            } else if (db[i].action_type == 4) {
                lv_label_set_text(pill_lbl, "OT out");
                lv_obj_set_style_bg_color(pill, UIManager::rgb(0xFCF2E4), 0); // Light orange
                UIManager::styleLabel(pill_lbl, 0xEF6C00, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
            }
            lv_obj_center(pill_lbl);
        }
        current_idx++;
    }
}

void uiShowLogs() {
    if (scr_logs == NULL) buildLogsScreen();
    lv_scr_load(scr_logs);
    UIManager::updateHeaderWifi(DataManager::isWifiConnected());
    
    if (ta_search_name) lv_textarea_set_text(ta_search_name, "");
    if (ta_search_date) lv_textarea_set_text(ta_search_date, "");
    if (dd_log_filter) { lv_dropdown_set_selected(dd_log_filter, 0); lv_dropdown_set_text(dd_log_filter, "Status"); }
    g_log_status_filter = 0;
    current_page_logs = 0;
    populate_logs_list("", "");
}
