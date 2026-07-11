#include "ui_enroll.h"
#include "ui_manager.h"
#include "data_manager.h"
#include "comm_manager.h"

lv_obj_t *scr_enroll = NULL;
static lv_obj_t *lbl_enroll_title  = NULL;
static lv_obj_t *lbl_enroll_msg    = NULL;
static lv_obj_t *lbl_enroll_step   = NULL;
static lv_obj_t *bar_enroll        = NULL;

lv_obj_t *scr_emp_list = NULL;
static lv_obj_t *emp_list_obj = NULL;
static lv_obj_t *ta_search = NULL;
static lv_obj_t *kb_search = NULL;

extern void uiShowIdle();
extern lv_timer_t *returnTimer;
extern const lv_img_dsc_t icon_people;
extern const lv_img_dsc_t icon_people_small;

static void btn_back_cb(lv_event_t * e) {
  UIManager::showMainMenu();
}

static void btn_emp_click_cb(lv_event_t * e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  char buf[32];
  snprintf(buf, sizeof(buf), "ENROLL:%d", id);
  CommManager::sendCommand(String(buf));
}

static void populate_emp_list(const char* name_filter, const char* dept_filter) {
  lv_obj_clean(emp_list_obj);

  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();

  String nFilt = name_filter ? String(name_filter) : "";
  String dFilt = dept_filter ? String(dept_filter) : "";
  nFilt.toLowerCase();
  dFilt.toLowerCase();

  for (int i = 0; i < count; i++) {
    String nStr = db[i].name; nStr.toLowerCase();
    String dStr = db[i].dept; dStr.toLowerCase();

    if (nFilt.length() > 0 && nStr.indexOf(nFilt) == -1) continue;
    if (dFilt.length() > 0 && dStr.indexOf(dFilt) == -1) continue;

    bool enrolled = (db[i].id > 0); // Replace with real enrollment check if available

    // Row container
    lv_obj_t *row = lv_obj_create(emp_list_obj);
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_set_style_bg_color(row, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, UIManager::rgb(0xE0E0E0), 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, btn_emp_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)db[i].id);

    // EMP ID
    char emp_id_buf[16];
    snprintf(emp_id_buf, sizeof(emp_id_buf), "EMP-%04d", db[i].id);
    lv_obj_t *lbl_id = lv_label_create(row);
    lv_label_set_text(lbl_id, emp_id_buf);
    UIManager::styleLabel(lbl_id, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_id, LV_ALIGN_LEFT_MID, 20, 0);

    // Name
    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, db[i].name.c_str());
    UIManager::styleLabel(lbl_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 180, 0);

    // Status badge
    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 110, 30);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(badge, UIManager::rgb(enrolled ? 0xD4EDDA : 0xF8D7DA), 0);

    lv_obj_t *lbl_status = lv_label_create(badge);
    lv_label_set_text(lbl_status, enrolled ? "Enrolled" : "Not Enrolled");
    UIManager::styleLabel(lbl_status, enrolled ? 0x155724 : 0x721C24, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_status);

    // Chevron >
    lv_obj_t *lbl_chevron = lv_label_create(row);
    lv_label_set_text(lbl_chevron, ">");
    UIManager::styleLabel(lbl_chevron, 0x888888, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(lbl_chevron, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

static lv_obj_t *ta_dept_search = NULL;

static void search_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        const char *n = lv_textarea_get_text(ta_search);
        const char *d = ta_dept_search ? lv_textarea_get_text(ta_dept_search) : "";
        populate_emp_list(n, d);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Dismiss keyboard, restore full list height
        lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(emp_list_obj, 270);
        lv_obj_align(emp_list_obj, LV_ALIGN_BOTTOM_MID, 0, -10);
    } else if (code == LV_EVENT_FOCUSED) {
        // Show keyboard but keep list visible above it (keyboard ~200px tall)
        lv_keyboard_set_textarea(kb_search, (lv_obj_t*)lv_event_get_target(e));
        lv_obj_clear_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
        // Shrink list so it fits above the keyboard
        lv_obj_set_height(emp_list_obj, 140);
        lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 200);
    }
}


void buildEmpListScreen() {
  scr_emp_list = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_emp_list, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_emp_list, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_emp_list, LV_SCROLLBAR_MODE_OFF);

  // ── Header ──
  // Back button (green, left)
  lv_obj_t *btn_back = lv_btn_create(scr_emp_list);
  lv_obj_set_size(btn_back, 56, 44);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 20, 18);
  lv_obj_set_style_bg_color(btn_back, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_set_style_radius(btn_back, 10, 0);
  lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_back);

  // Center: people icon + "Employees" title
  lv_obj_t *hdr_icon = lv_img_create(scr_emp_list);
  lv_img_set_src(hdr_icon, &icon_people_small);
  lv_obj_set_style_img_recolor(hdr_icon, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_set_style_img_recolor_opa(hdr_icon, LV_OPA_COVER, 0);
  lv_obj_align(hdr_icon, LV_ALIGN_TOP_MID, -80, 18);

  lv_obj_t *lbl_title = lv_label_create(scr_emp_list);
  lv_label_set_text(lbl_title, "Employees");
  UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, -30, 18);

  // Add employee button (green, right)
  lv_obj_t *btn_add = lv_btn_create(scr_emp_list);
  lv_obj_set_size(btn_add, 170, 44);
  lv_obj_align(btn_add, LV_ALIGN_TOP_RIGHT, -20, 18);
  lv_obj_set_style_bg_color(btn_add, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_set_style_radius(btn_add, 10, 0);
  lv_obj_t *lbl_add = lv_label_create(btn_add);
  lv_label_set_text(lbl_add, "Add employee +");
  UIManager::styleLabel(lbl_add, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_add);

  // Horizontal separator
  lv_obj_t *sep = lv_obj_create(scr_emp_list);
  lv_obj_set_size(sep, 760, 1);
  lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_bg_color(sep, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(sep, 0, 0);

  // ── Search Row ──
  ta_search = lv_textarea_create(scr_emp_list);
  lv_textarea_set_one_line(ta_search, true);
  lv_textarea_set_placeholder_text(ta_search, "Search by name");
  lv_obj_set_size(ta_search, 360, 44);
  lv_obj_align(ta_search, LV_ALIGN_TOP_LEFT, 20, 90);
  lv_obj_set_style_radius(ta_search, 8, 0);
  lv_obj_set_style_border_color(ta_search, UIManager::rgb(0xCCCCCC), 0);
  lv_obj_set_style_bg_color(ta_search, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_add_event_cb(ta_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  ta_dept_search = lv_textarea_create(scr_emp_list);
  lv_textarea_set_one_line(ta_dept_search, true);
  lv_textarea_set_placeholder_text(ta_dept_search, "Search by department");
  lv_obj_set_size(ta_dept_search, 360, 44);
  lv_obj_align(ta_dept_search, LV_ALIGN_TOP_RIGHT, -20, 90);
  lv_obj_set_style_radius(ta_dept_search, 8, 0);
  lv_obj_set_style_border_color(ta_dept_search, UIManager::rgb(0xCCCCCC), 0);
  lv_obj_set_style_bg_color(ta_dept_search, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_add_event_cb(ta_dept_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  // ── Column Headers ──
  lv_obj_t *col_hdr = lv_obj_create(scr_emp_list);
  lv_obj_set_size(col_hdr, 760, 36);
  lv_obj_align(col_hdr, LV_ALIGN_TOP_MID, 0, 148);
  lv_obj_set_style_bg_color(col_hdr, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_width(col_hdr, 0, 0);
  lv_obj_set_style_pad_all(col_hdr, 0, 0);
  lv_obj_clear_flag(col_hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ch_id = lv_label_create(col_hdr);
  lv_label_set_text(ch_id, "ID");
  UIManager::styleLabel(ch_id, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_id, LV_ALIGN_LEFT_MID, 20, 0);

  lv_obj_t *ch_name = lv_label_create(col_hdr);
  lv_label_set_text(ch_name, "Name");
  UIManager::styleLabel(ch_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_name, LV_ALIGN_LEFT_MID, 180, 0);

  lv_obj_t *ch_status = lv_label_create(col_hdr);
  lv_label_set_text(ch_status, "Status");
  UIManager::styleLabel(ch_status, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
  lv_obj_align(ch_status, LV_ALIGN_RIGHT_MID, -50, 0);

  // ── Employee List Container ──
  emp_list_obj = lv_obj_create(scr_emp_list);
  lv_obj_set_size(emp_list_obj, 760, 270);
  lv_obj_align(emp_list_obj, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(emp_list_obj, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(emp_list_obj, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(emp_list_obj, 1, 0);
  lv_obj_set_style_radius(emp_list_obj, 10, 0);
  lv_obj_set_style_pad_all(emp_list_obj, 0, 0);
  lv_obj_set_flex_flow(emp_list_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_layout(emp_list_obj, LV_LAYOUT_FLEX);
  lv_obj_set_scrollbar_mode(emp_list_obj, LV_SCROLLBAR_MODE_AUTO);

  // Keyboard (hidden initially)
  kb_search = lv_keyboard_create(scr_emp_list);
  lv_keyboard_set_textarea(kb_search, ta_search);
  lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);

  // Initial population
  populate_emp_list("", "");
}

void buildEnrollScreen() {
  scr_enroll = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr_enroll, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_enroll, LV_SCROLLBAR_MODE_OFF);

  // Title
  lbl_enroll_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_title, "FINGERPRINT ENROLLMENT");
  UIManager::styleLabel(lbl_enroll_title, COLOR_ACCENT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_title, LV_ALIGN_TOP_MID, 0, 40);

  // Employee name
  lbl_enroll_msg = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_msg, "---");
  UIManager::styleLabel(lbl_enroll_msg, COLOR_TEXT, &lv_font_montserrat_36, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_msg, LV_ALIGN_CENTER, 0, -40);

  // Step instruction
  lbl_enroll_step = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_step, "");
  UIManager::styleLabel(lbl_enroll_step, COLOR_SUBTEXT, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align_to(lbl_enroll_step, lbl_enroll_msg, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

  // Progress bar
  bar_enroll = lv_bar_create(scr_enroll);
  lv_obj_set_size(bar_enroll, 400, 20);
  lv_obj_align(bar_enroll, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(bar_enroll, UIManager::rgb(COLOR_DIM), 0);
  lv_obj_set_style_bg_color(bar_enroll, UIManager::rgb(COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar_enroll, 10, 0);
  lv_obj_set_style_radius(bar_enroll, 10, LV_PART_INDICATOR);
  lv_bar_set_range(bar_enroll, 0, 3);
  lv_bar_set_value(bar_enroll, 0, LV_ANIM_OFF);
}

void uiShowEnrollStart(const char *name) {
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_BG), 0);
  lv_label_set_text(lbl_enroll_title, "FINGERPRINT ENROLLMENT");
  UIManager::styleLabel(lbl_enroll_title, COLOR_ACCENT, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);

  lv_label_set_text(lbl_enroll_msg, name ? name : "");
  lv_label_set_text(lbl_enroll_step, "Preparing...");
  lv_bar_set_value(bar_enroll, 0, LV_ANIM_OFF);
  lv_scr_load_anim(scr_enroll, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void uiShowEnrollStep(int step, const char *msg) {
  lv_label_set_text(lbl_enroll_step, msg ? msg : "");
  lv_bar_set_value(bar_enroll, step, LV_ANIM_ON);
}

void uiShowEnrollResult(bool ok, const char *name) {
  if (ok) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Enrolled: %s", name ? name : "");
    lv_label_set_text(lbl_enroll_step, buf);
    lv_bar_set_value(bar_enroll, 3, LV_ANIM_ON);
    
    lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_IN), 0);
    lv_label_set_text(lbl_enroll_title, "SUCCESS!");
    UIManager::styleLabel(lbl_enroll_title, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  } else {
    lv_label_set_text(lbl_enroll_step, "Enrollment failed. Try again.");
    
    lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(COLOR_DANGER), 0);
    lv_label_set_text(lbl_enroll_title, "FAILED!");
    UIManager::styleLabel(lbl_enroll_title, COLOR_TEXT, &lv_font_montserrat_48, LV_TEXT_ALIGN_CENTER);
  }

  if (returnTimer) lv_timer_del(returnTimer);
  returnTimer = lv_timer_create([](lv_timer_t *t) {
    uiShowIdle();
    returnTimer = NULL;
  }, 3000, NULL);
  lv_timer_set_repeat_count(returnTimer, 1);
}
