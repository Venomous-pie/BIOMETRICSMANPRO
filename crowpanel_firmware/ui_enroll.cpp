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
lv_obj_t *emp_list_obj = NULL;
lv_obj_t *ta_search = NULL;
lv_obj_t *kb_search = NULL;
lv_timer_t *search_debounce_timer = NULL;  // fires 400 ms after last keystroke

lv_obj_t *scr_choose_finger = NULL;
static int selected_emp_id = 0;
static int selected_finger_index = -1;
static lv_obj_t *btn_start_scan = NULL;
static lv_obj_t *lbl_choose_info = NULL;
static lv_obj_t *finger_objs[10];

extern void uiShowIdle();
extern lv_timer_t *returnTimer;
extern const lv_img_dsc_t icon_people;
extern const lv_img_dsc_t icon_people_small;

static void btn_back_cb(lv_event_t * e) {
  UIManager::showMainMenu();
}

static void btn_emp_click_cb(lv_event_t * e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  String name = "", dept = "";
  for (int i=0; i<count; i++) {
    if (db[i].id == id) {
      name = db[i].name;
      dept = db[i].dept;
      break;
    }
  }
  uiShowChooseFinger(id, name.c_str(), dept.c_str());
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

    bool enrolled = db[i].fp_enrolled;

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

    // Name
    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, db[i].name.c_str());
    UIManager::styleLabel(lbl_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(lbl_name, 170, 52);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_CLIP);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 20, 0);

    // Job Title
    lv_obj_t *lbl_job = lv_label_create(row);
    lv_label_set_text(lbl_job, db[i].job_title.c_str());
    UIManager::styleLabel(lbl_job, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(lbl_job, 170, 52);
    lv_label_set_long_mode(lbl_job, LV_LABEL_LONG_CLIP);
    lv_obj_align(lbl_job, LV_ALIGN_LEFT_MID, 200, 0);

    // Branch
    lv_obj_t *lbl_branch = lv_label_create(row);
    lv_label_set_text(lbl_branch, db[i].branch.c_str());
    UIManager::styleLabel(lbl_branch, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(lbl_branch, 130, 52);
    lv_label_set_long_mode(lbl_branch, LV_LABEL_LONG_CLIP);
    lv_obj_align(lbl_branch, LV_ALIGN_LEFT_MID, 380, 0);

    // Dept
    lv_obj_t *lbl_dept = lv_label_create(row);
    lv_label_set_text(lbl_dept, db[i].dept.c_str());
    UIManager::styleLabel(lbl_dept, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(lbl_dept, 120, 52);
    lv_label_set_long_mode(lbl_dept, LV_LABEL_LONG_CLIP);
    lv_obj_align(lbl_dept, LV_ALIGN_LEFT_MID, 520, 0);

    // Status badge
    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 100, 30);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(badge, UIManager::rgb(enrolled ? 0xD4EDDA : 0xE0E0E0), 0);

    lv_obj_t *lbl_status = lv_label_create(badge);
    lv_label_set_text(lbl_status, enrolled ? "Enrolled" : "Unenrolled");
    UIManager::styleLabel(lbl_status, enrolled ? 0x155724 : 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(lbl_status);
  }
}

static lv_obj_t *ta_dept_search = NULL;

// Debounce timer callback — runs 400 ms after the last keystroke
static void search_debounce_cb(lv_timer_t *t) {
    lv_timer_del(search_debounce_timer);
    search_debounce_timer = NULL;
    const char *n = ta_search       ? lv_textarea_get_text(ta_search)       : "";
    const char *d = ta_dept_search  ? lv_textarea_get_text(ta_dept_search)  : "";
    populate_emp_list(n, d);
}

static void search_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Debounce: reset 400 ms timer on every keystroke instead of
        // calling populate_emp_list() (which does lv_obj_clean + full
        // widget recreate) on every single character typed.
        if (search_debounce_timer) {
            lv_timer_reset(search_debounce_timer);
        } else {
            search_debounce_timer = lv_timer_create(search_debounce_cb, 400, NULL);
            lv_timer_set_repeat_count(search_debounce_timer, 1);
        }
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
  if (scr_emp_list != NULL) return;  // Already built, skip
  scr_emp_list = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_emp_list, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_emp_list, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_emp_list, LV_SCROLLBAR_MODE_OFF);

  // â”€â”€ Header â”€â”€
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

  // Center: "Employees" title
  lv_obj_t *lbl_title = lv_label_create(scr_emp_list);
  lv_label_set_text(lbl_title, "Employees");
  UIManager::styleLabel(lbl_title, COLOR_TEXT_MAIN, &lv_font_montserrat_28, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 18);

  // Horizontal separator
  lv_obj_t *sep = lv_obj_create(scr_emp_list);
  lv_obj_set_size(sep, 760, 1);
  lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_bg_color(sep, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(sep, 0, 0);

  // â”€â”€ Search Row â”€â”€
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

  // â”€â”€ Column Headers â”€â”€
  lv_obj_t *col_hdr = lv_obj_create(scr_emp_list);
  lv_obj_set_size(col_hdr, 760, 36);
  lv_obj_align(col_hdr, LV_ALIGN_TOP_MID, 0, 148);
  lv_obj_set_style_bg_color(col_hdr, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_width(col_hdr, 0, 0);
  lv_obj_set_style_pad_all(col_hdr, 0, 0);
  lv_obj_clear_flag(col_hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ch_name = lv_label_create(col_hdr);
  lv_label_set_text(ch_name, "Name");
  UIManager::styleLabel(ch_name, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_name, LV_ALIGN_LEFT_MID, 20, 0);

  lv_obj_t *ch_job = lv_label_create(col_hdr);
  lv_label_set_text(ch_job, "Job Title");
  UIManager::styleLabel(ch_job, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_job, LV_ALIGN_LEFT_MID, 200, 0);

  lv_obj_t *ch_branch = lv_label_create(col_hdr);
  lv_label_set_text(ch_branch, "Branch");
  UIManager::styleLabel(ch_branch, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_branch, LV_ALIGN_LEFT_MID, 380, 0);

  lv_obj_t *ch_dept = lv_label_create(col_hdr);
  lv_label_set_text(ch_dept, "Department");
  UIManager::styleLabel(ch_dept, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_dept, LV_ALIGN_LEFT_MID, 520, 0);

  lv_obj_t *ch_status = lv_label_create(col_hdr);
  lv_label_set_text(ch_status, "Status");
  UIManager::styleLabel(ch_status, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
  lv_obj_align(ch_status, LV_ALIGN_RIGHT_MID, -30, 0);

  // â”€â”€ Employee List Container â”€â”€
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
  // NOTE: do NOT call populate_emp_list() here.
  // Ownership of initial population belongs to uiShowEmpList(), which defers
  // it to after the screen is active so it never runs inside an event callback.
}

void buildEnrollScreen() {
  if (scr_enroll != NULL) return;  // Already built, skip
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
  if (scr_enroll == NULL) buildEnrollScreen();  // Lazy build on first use
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
    // Persist fp_enrolled=true for the employee that was just enrolled so the
    // badge shows "Enrolled" when the user returns to the employee list.
    DataManager::updateEmployeeFpEnrolled(selected_emp_id, true);

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

static void choose_back_cb(lv_event_t * e) {
  uiShowEmpList();
}

static void finger_click_cb(lv_event_t * e) {
  int f_idx = (int)(intptr_t)lv_event_get_user_data(e);
  selected_finger_index = f_idx;
  
  for (int i=0; i<10; i++) {
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE8F5E9), 0); // Light green
  }
  lv_obj_set_style_bg_color(finger_objs[f_idx], UIManager::rgb(0x2E7D32), 0); // Dark green
  
  if (btn_start_scan) {
    lv_obj_clear_state(btn_start_scan, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2E7D32), 0);
  }
}

static void start_scan_cb(lv_event_t * e) {
  if (selected_finger_index < 0 || selected_emp_id <= 0) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "ENROLL:%d:%d", selected_emp_id, selected_finger_index);
  CommManager::sendCommand(String(buf));
  
  String n = "";
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  for (int i=0; i<count; i++) {
    if (db[i].id == selected_emp_id) { n = db[i].name; break; }
  }
  uiShowEnrollStart(n.c_str());
}


void buildChooseFingerScreen() {
  if (scr_choose_finger != NULL) return;
  scr_choose_finger = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_choose_finger, UIManager::rgb(0x1A1A1A), 0);

  lv_obj_t *lbl_top_title = lv_label_create(scr_choose_finger);
  lv_label_set_text(lbl_top_title, "Choose finger");
  UIManager::styleLabel(lbl_top_title, 0x6B7280, &lv_font_montserrat_20, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_top_title, LV_ALIGN_TOP_LEFT, 30, 20);

  lv_obj_t *main_panel = lv_obj_create(scr_choose_finger);
  lv_obj_set_size(main_panel, 740, 380);
  lv_obj_align(main_panel, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(main_panel, UIManager::rgb(0xF8FAF9), 0);
  lv_obj_set_style_radius(main_panel, 12, 0);
  lv_obj_set_style_border_width(main_panel, 0, 0);
  lv_obj_clear_flag(main_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_back = lv_btn_create(main_panel);
  lv_obj_set_size(btn_back, 60, 44);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_set_style_bg_color(btn_back, UIManager::rgb(0x2A800F), 0);
  lv_obj_set_style_radius(btn_back, 8, 0);
  lv_obj_add_event_cb(btn_back, choose_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_back);

  lv_obj_t *lbl_title = lv_label_create(main_panel);
  lv_label_set_text(lbl_title, "Enroll fingerprint");
  UIManager::styleLabel(lbl_title, 0x1A1A1A, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 15);

  lv_obj_t *lbl_subtitle = lv_label_create(main_panel);
  lv_label_set_text(lbl_subtitle, "Select finger");
  UIManager::styleLabel(lbl_subtitle, 0x6B7280, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_subtitle, LV_ALIGN_TOP_MID, 0, 45);

  btn_start_scan = lv_btn_create(main_panel);
  lv_obj_set_size(btn_start_scan, 140, 44);
  lv_obj_align(btn_start_scan, LV_ALIGN_TOP_RIGHT, -20, 20);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x6B7280), LV_STATE_DISABLED);
  lv_obj_set_style_radius(btn_start_scan, 8, 0);
  lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
  lv_obj_add_event_cb(btn_start_scan, start_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_start = lv_label_create(btn_start_scan);
  lv_label_set_text(lbl_start, "Start scan " LV_SYMBOL_RIGHT);
  UIManager::styleLabel(lbl_start, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_start);

  lbl_choose_info = lv_label_create(main_panel);
  lv_label_set_text(lbl_choose_info, "");
  UIManager::styleLabel(lbl_choose_info, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_choose_info, LV_ALIGN_TOP_LEFT, 30, 90);

  // Draw Hands
  lv_obj_t *left_palm = lv_obj_create(main_panel);
  lv_obj_set_size(left_palm, 210, 100);
  lv_obj_align(left_palm, LV_ALIGN_BOTTOM_LEFT, 40, -20);
  lv_obj_set_style_bg_color(left_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(left_palm, 16, 0);
  lv_obj_set_style_border_width(left_palm, 0, 0);
  lv_obj_clear_flag(left_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_lp = lv_label_create(left_palm);
  lv_label_set_text(lbl_lp, "Left hand");
  UIManager::styleLabel(lbl_lp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_lp, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_obj_t *right_palm = lv_obj_create(main_panel);
  lv_obj_set_size(right_palm, 210, 100);
  lv_obj_align(right_palm, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
  lv_obj_set_style_bg_color(right_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(right_palm, 16, 0);
  lv_obj_set_style_border_width(right_palm, 0, 0);
  lv_obj_clear_flag(right_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_rp = lv_label_create(right_palm);
  lv_label_set_text(lbl_rp, "Right hand");
  UIManager::styleLabel(lbl_rp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_rp, LV_ALIGN_BOTTOM_MID, 0, -20);

  // Fingers Left Hand: LP(0), LR(1), LM(2), LI(3), LT(4)
  int lx[5] = {40, 90, 140, 190, 260};
  int ly[5] = {190, 130, 120, 130, 230};
  int lh[5] = {100, 160, 170, 160, 90};
  const char* l_labels[5] = {"LP", "LR", "LM", "LI", "LT"};

  // Right Hand: RT(5), RI(6), RM(7), RR(8), RP(9)
  int rx[5] = {440, 510, 560, 610, 660};
  int ry[5] = {230, 130, 120, 130, 190};
  int rh[5] = {90, 160, 170, 160, 100};
  const char* r_labels[5] = {"RT", "RI", "RM", "RR", "RP"};

  for (int i = 0; i < 10; i++) {
    finger_objs[i] = lv_obj_create(main_panel);
    lv_obj_set_size(finger_objs[i], 36, i < 5 ? lh[i] : rh[i-5]);
    lv_obj_align(finger_objs[i], LV_ALIGN_TOP_LEFT, i < 5 ? lx[i] : rx[i-5], i < 5 ? ly[i] : ry[i-5]);
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0);
    lv_obj_set_style_radius(finger_objs[i], 18, 0);
    lv_obj_set_style_border_width(finger_objs[i], 0, 0);
    lv_obj_clear_flag(finger_objs[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(finger_objs[i], finger_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t *l = lv_label_create(finger_objs[i]);
    lv_label_set_text(l, i < 5 ? l_labels[i] : r_labels[i-5]);
    UIManager::styleLabel(l, 0x166534, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);
  }
}

// Deferred state for Choose Finger screen
static int defer_emp_id = 0;
static String defer_name = "";
static String defer_dept = "";

void uiShowChooseFinger(int emp_id, const char *name, const char *dept) {
  defer_emp_id = emp_id;
  defer_name = name;
  defer_dept = dept;

  lv_timer_t *defer_timer = lv_timer_create([](lv_timer_t *t) {
    // 1. Create a tiny temporary screen and make it active.
    // This allows us to safely delete the heavy Employee screen without crashing LVGL.
    lv_obj_t *temp_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
    lv_scr_load_anim(temp_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // 2. Synchronously delete the heavy Employee List screen to completely free RAM.
    if (scr_emp_list != NULL) {
      if (search_debounce_timer) {
        lv_timer_del(search_debounce_timer);
        search_debounce_timer = NULL;
      }
      lv_obj_del(scr_emp_list);
      scr_emp_list = NULL;
      emp_list_obj = NULL;
      ta_search = NULL;
      kb_search = NULL;
    }

    // 3. Now that we have plenty of RAM, build the new Choose Finger screen.
    if (scr_choose_finger == NULL) {
      buildChooseFingerScreen();
    }

    selected_emp_id = defer_emp_id;
    selected_finger_index = -1;

    char buf[256];
    snprintf(buf, sizeof(buf), "Enrolling %s from %s -- pick a finger", defer_name.c_str(), defer_dept.c_str());
    lv_label_set_text(lbl_choose_info, buf);

    if (btn_start_scan) {
      lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), 0);
    }

    for (int i = 0; i < 10; i++) {
      if (finger_objs[i]) {
        lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0);
      }
    }

    // 4. Load the new screen and auto-delete the temporary screen.
    lv_scr_load_anim(scr_choose_finger, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
  }, 10, NULL);
  
  lv_timer_set_repeat_count(defer_timer, 1);
}

// Show the employee list screen with a clean, up-to-date state.
// Called on first entry from Main Menu, and when navigating back from Choose Finger.
void uiShowEmpList() {
  if (search_debounce_timer) {
    lv_timer_del(search_debounce_timer);
    search_debounce_timer = NULL;
  }

  lv_timer_t *defer = lv_timer_create([](lv_timer_t *t) {
    // 1. Create a tiny temporary screen and make it active.
    lv_obj_t *temp_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
    lv_scr_load_anim(temp_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    // 2. Synchronously delete the heavy Choose Finger screen to completely free RAM.
    if (scr_choose_finger != NULL) {
      lv_obj_del(scr_choose_finger);
      scr_choose_finger = NULL;
      btn_start_scan = NULL;
      lbl_choose_info = NULL;
      for (int i = 0; i < 10; i++) finger_objs[i] = NULL;
    }

    // 3. Now that we have plenty of RAM, build the Employee List screen.
    if (scr_emp_list == NULL) {
      buildEmpListScreen();
    }

    if (ta_search)      { lv_obj_clear_state(ta_search, LV_STATE_FOCUSED);      lv_textarea_set_text(ta_search, ""); }
    if (ta_dept_search) { lv_obj_clear_state(ta_dept_search, LV_STATE_FOCUSED); lv_textarea_set_text(ta_dept_search, ""); }

    if (kb_search) lv_obj_add_flag(kb_search, LV_OBJ_FLAG_HIDDEN);
    if (emp_list_obj) {
      lv_obj_set_height(emp_list_obj, 270);
      lv_obj_align(emp_list_obj, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    // 4. Load the Employee List screen and auto-delete the temporary screen.
    lv_scr_load_anim(scr_emp_list, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    
    populate_emp_list("", "");
  }, 10, NULL);
  
  lv_timer_set_repeat_count(defer, 1);
}

