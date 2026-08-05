#include "ui_enroll.h"
#include "ui_manager.h"
#include "ui_main_menu.h"
#include "../core/data_manager.h"
#include "../core/comm_manager.h"
#include <mbedtls/base64.h>

LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_36);
LV_FONT_DECLARE(lv_font_montserrat_48);

lv_obj_t *scr_enroll = NULL;
bool g_is_fallback = false;
static lv_obj_t *btn_enroll_back = NULL;
static lv_obj_t *btn_enroll_done = NULL;
static lv_obj_t *lbl_enroll_main_title = NULL;
static lv_obj_t *lbl_enroll_sub_title = NULL;
static lv_obj_t *lbl_enroll_instruction = NULL;
static lv_obj_t *box_scan = NULL;
static lv_obj_t *img_scan_icon = NULL;
static lv_obj_t *lbl_scan_text = NULL;
static lv_obj_t *lbl_scan_subtext = NULL;
static lv_obj_t *dot_1 = NULL;
static lv_obj_t *dot_2 = NULL;
static lv_obj_t *dot_3 = NULL;

// Deferred state for Choose Finger screen (moved up so enroll can use it)
static String defer_emp_id = "";
static String defer_name = "";
static String defer_dept = "";

extern const lv_img_dsc_t icon_biometrics;
extern const lv_img_dsc_t icon_check;

const char* getFingerName(int index) {
  switch(index) {
    case 0: return "left pinky";
    case 1: return "left ring";
    case 2: return "left middle";
    case 3: return "left index";
    case 4: return "left thumb";
    case 5: return "right thumb";
    case 6: return "right index";
    case 7: return "right middle";
    case 8: return "right ring";
    case 9: return "right pinky";
    default: return "unknown";
  }
}

static void enroll_back_cb(lv_event_t * e) {
  // Tell the WROOM to abort the active enrollment scan
  CommManager::sendCommand("CANCEL_ENROLL");
  uiShowChooseFinger(defer_emp_id, defer_name.c_str(), defer_dept.c_str());
}

static void enroll_done_cb(lv_event_t * e) {
  uiShowEmpList();
}

lv_obj_t *scr_emp_list = NULL;
lv_obj_t *emp_list_obj = NULL;
static lv_obj_t *ta_search = NULL;
static lv_obj_t *dd_status_filter = NULL;  // Enrollment status dropdown
static lv_obj_t *dd_dept_filter = NULL;
static lv_obj_t *dd_branch_filter = NULL;
lv_timer_t *search_debounce_timer = NULL;  // fires 400 ms after last keystroke

static int g_status_filter = 0;  // 0=All, 1=Enrolled, 2=Unenrolled
static String g_dept_filter_str = "All";
static String g_branch_filter_str = "All";

static int current_page = 0;
static lv_obj_t *lbl_page_info = NULL;
static lv_obj_t *btn_prev_page = NULL;
static lv_obj_t *btn_next_page = NULL;

static void populate_emp_list(const char* name_filter, const char* dept_filter);

static void prev_page_cb(lv_event_t * e) {
    if (current_page > 0) {
        current_page--;
        const char *n = ta_search ? lv_textarea_get_text(ta_search) : "";
        populate_emp_list(n, "");
    }
}

static void next_page_cb(lv_event_t * e) {
    current_page++;
    const char *n = ta_search ? lv_textarea_get_text(ta_search) : "";
    populate_emp_list(n, "");
}

lv_obj_t *scr_choose_finger = NULL;
static String selected_emp_id = "";
static int selected_finger_index = -1;
static lv_obj_t *btn_start_scan = NULL;
static lv_obj_t *btn_delete_scan = NULL;
static lv_obj_t *lbl_start_scan_text = NULL;
static lv_obj_t *lbl_choose_info = NULL;
static lv_obj_t *finger_objs[10];

// Navigation guard: prevents stacked deferred timers when user taps quickly.
// Set to true when a deferred screen transition starts; cleared when it fully
// completes (after populate). Any tap during that window is ignored.
static bool s_nav_busy = false;

// Watchdog: if WROOM never echoes ENROLL_START (dropped packet), return to
// choose-finger after this timeout and restore the Start Scan button.
static lv_timer_t *enrollStartWatchdog = NULL;

extern void uiShowIdle();
extern lv_timer_t *returnTimer;
extern const lv_img_dsc_t icon_people;
extern const lv_img_dsc_t icon_people_small;

static void btn_back_cb(lv_event_t *e) {
  if (g_is_fallback) {
    uiShowIdle();
  } else {
    UIManager::showMainMenu();
  }
}

static unsigned long last_emp_sync_click = 0;

static void btn_emp_sync_cb(lv_event_t *e) {
  if (millis() - last_emp_sync_click < 2000) return; // 2-second debounce
  last_emp_sync_click = millis();

  if (Serial) Serial.println("UI EmpList: Sync button clicked");
  UIManager::showToast("Syncing via WROOM...", false);
  String syncCmd = "{\"cmd\":\"SYNC_EMP\",\"token\":\"" + DataManager::getActivationCode() + "\"}";
  CommManager::sendCommand(syncCmd);
}

static void delete_finger_cb(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  const Employee* db = DataManager::getEmployees();
  
  if (index >= 0 && index < DataManager::getEmployeeCount()) {
      uiShowChooseFinger(db[index].id, db[index].name.c_str(), db[index].dept.c_str());
  }
}

static void btn_emp_click_cb(lv_event_t * e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  const Employee* db = DataManager::getEmployees();
  
  if (index >= 0 && index < DataManager::getEmployeeCount()) {
      uiShowChooseFinger(db[index].id, db[index].name.c_str(), db[index].dept.c_str(), g_is_fallback);
  }
}

static bool containsIgnoreCase(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    int hlen = strlen(haystack);
    int nlen = strlen(needle);
    if (nlen == 0) return true;
    if (hlen < nlen) return false;
    for (int i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (int j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i+j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static void populate_emp_list(const char* name_filter, const char* dept_filter) {
  if (!emp_list_obj) return; // Prevent crash if screen creation failed due to OOM
  lv_obj_clean(emp_list_obj);

  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();

  String nFilt = name_filter ? String(name_filter) : "";
  nFilt.toLowerCase();

  int items_per_page = 4;
  int filtered_count = 0;

  for (int i = 0; i < count; i++) {
    const char* n = db[i].name.c_str();
    const char* d = db[i].dept.c_str();
    const char* j = db[i].job_title.c_str();
    const char* b = db[i].branch.c_str();
    
    // Hide Admin from the UI list
    if (containsIgnoreCase(n, "admin") || containsIgnoreCase(d, "admin") || containsIgnoreCase(j, "admin")) continue;

    if (name_filter && strlen(name_filter) > 0 && !containsIgnoreCase(n, name_filter)) continue;
    // Apply enrollment status filter
    if (g_status_filter == 1 && !db[i].fp_enrolled) continue;
    if (g_status_filter == 2 && db[i].fp_enrolled) continue;
    // Apply dept & branch filters
    if (g_dept_filter_str != "All" && !equalsIgnoreCase(d, g_dept_filter_str.c_str())) continue;
    if (g_branch_filter_str != "All" && !equalsIgnoreCase(b, g_branch_filter_str.c_str())) continue;
    filtered_count++;
  }

  int total_pages = (filtered_count + items_per_page - 1) / items_per_page;
  if (total_pages == 0) total_pages = 1;
  if (current_page >= total_pages) current_page = total_pages - 1;

  if (lbl_page_info) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Page %d of %d", current_page + 1, total_pages);
    lv_label_set_text(lbl_page_info, buf);
  }
  if (btn_prev_page) {
    if (current_page == 0) lv_obj_add_state(btn_prev_page, LV_STATE_DISABLED);
    else lv_obj_clear_state(btn_prev_page, LV_STATE_DISABLED);
  }
  if (btn_next_page) {
    if (current_page >= total_pages - 1) lv_obj_add_state(btn_next_page, LV_STATE_DISABLED);
    else lv_obj_clear_state(btn_next_page, LV_STATE_DISABLED);
  }

  int start_idx = current_page * items_per_page;
  int end_idx = start_idx + items_per_page;
  int current_idx = 0;

  if (filtered_count == 0) {
    lv_obj_t *empty_label = lv_label_create(emp_list_obj);
    lv_label_set_text(empty_label, "No employees found");
    UIManager::styleLabel(empty_label, 0x999999, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(empty_label, 760);
    lv_obj_set_style_pad_top(empty_label, 80, 0);
    return;
  }

  for (int i = 0; i < count; i++) {
    const char* n = db[i].name.c_str();
    const char* d = db[i].dept.c_str();
    const char* j = db[i].job_title.c_str();
    const char* b = db[i].branch.c_str();

    // Hide Admin from the UI list
    if (containsIgnoreCase(n, "admin") || containsIgnoreCase(d, "admin") || containsIgnoreCase(j, "admin")) continue;

    if (name_filter && strlen(name_filter) > 0 && !containsIgnoreCase(n, name_filter)) continue;
    if (g_status_filter == 1 && !db[i].fp_enrolled) continue;
    if (g_status_filter == 2 && db[i].fp_enrolled) continue;
    if (g_dept_filter_str != "All" && !equalsIgnoreCase(d, g_dept_filter_str.c_str())) continue;
    if (g_branch_filter_str != "All" && !equalsIgnoreCase(b, g_branch_filter_str.c_str())) continue;

    if (current_idx >= start_idx && current_idx < end_idx) {
      bool enrolled = db[i].fp_enrolled;

      // Row container
    lv_obj_t *row = lv_obj_create(emp_list_obj);
    if (!row) break; // If OOM, stop creating rows instead of crashing
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_set_style_bg_color(row, UIManager::rgb(0xFFFFFF), 0);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, UIManager::rgb(0xE0E0E0), 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, btn_emp_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

      // Name
      lv_obj_t *lbl_name = lv_label_create(row);
      lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_name, 175);
      lv_label_set_text(lbl_name, db[i].name.c_str());
      UIManager::styleLabel(lbl_name, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 20, 0);

      // Job Title
      lv_obj_t *lbl_job = lv_label_create(row);
      lv_label_set_long_mode(lbl_job, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_job, 165);
      lv_label_set_text(lbl_job, db[i].job_title.c_str());
      UIManager::styleLabel(lbl_job, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_job, LV_ALIGN_LEFT_MID, 200, 0);

      // Branch
      lv_obj_t *lbl_branch = lv_label_create(row);
      lv_label_set_long_mode(lbl_branch, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_branch, 135);
      lv_label_set_text(lbl_branch, db[i].branch.c_str());
      UIManager::styleLabel(lbl_branch, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_branch, LV_ALIGN_LEFT_MID, 370, 0);

      // Dept
      lv_obj_t *lbl_dept = lv_label_create(row);
      lv_label_set_long_mode(lbl_dept, LV_LABEL_LONG_DOT);
      lv_obj_set_width(lbl_dept, 125);
      lv_label_set_text(lbl_dept, db[i].dept.c_str());
      UIManager::styleLabel(lbl_dept, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
      lv_obj_align(lbl_dept, LV_ALIGN_LEFT_MID, 510, 0);

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
    current_idx++;
  }
}

// Debounce timer callback — runs 400 ms after the last keystroke
static void search_debounce_cb(lv_timer_t *t) {
    search_debounce_timer = NULL;
    current_page = 0;
    const char *n = ta_search ? lv_textarea_get_text(ta_search) : "";
    populate_emp_list(n, "");
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
    } else if (code == LV_EVENT_FOCUSED) {
        UIManager::openKeyboardFor((lv_obj_t*)lv_event_get_current_target(e));
    }
}


void buildEmpListScreen() {
  if (scr_emp_list != NULL) return;
  scr_emp_list = lv_obj_create(NULL);
  if (!scr_emp_list) return; // Prevent LoadProhibited crash on OOM

  lv_obj_set_style_bg_color(scr_emp_list, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_emp_list, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_emp_list, LV_SCROLLBAR_MODE_OFF);

  // ─── Header ───
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
  lv_obj_set_size(ta_search, 260, 44);
  lv_obj_align(ta_search, LV_ALIGN_TOP_LEFT, 20, 90);
  UIManager::styleTextArea(ta_search);
  lv_obj_add_event_cb(ta_search, search_ta_event_cb, LV_EVENT_ALL, NULL);

  auto dd_filter_cb = [](lv_event_t *e) {
      lv_obj_t *dd = (lv_obj_t*)lv_event_get_current_target(e);
      char buf[64];
      lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
      if (lv_dropdown_get_selected(dd) == 0) {
          if (dd == dd_status_filter) lv_dropdown_set_text(dd, "Status");
          else if (dd == dd_dept_filter) lv_dropdown_set_text(dd, "Department");
          else if (dd == dd_branch_filter) lv_dropdown_set_text(dd, "Branch");
      } else {
          lv_dropdown_set_text(dd, NULL);
      }

      if (dd == dd_status_filter) g_status_filter = lv_dropdown_get_selected(dd);
      else if (dd == dd_dept_filter) g_dept_filter_str = buf;
      else if (dd == dd_branch_filter) g_branch_filter_str = buf;

      current_page = 0;
      // Reuse the search debounce timer so rapid filter changes don't
      // trigger a synchronous lv_obj_clean + full widget rebuild each time.
      if (search_debounce_timer) {
          lv_timer_reset(search_debounce_timer);
      } else {
          search_debounce_timer = lv_timer_create(search_debounce_cb, 150, NULL);
          lv_timer_set_repeat_count(search_debounce_timer, 1);
      }
  };

  dd_dept_filter = lv_dropdown_create(scr_emp_list);
  lv_obj_set_size(dd_dept_filter, 135, 44);
  lv_obj_align(dd_dept_filter, LV_ALIGN_TOP_LEFT, 290, 90);
  lv_obj_set_style_bg_color(dd_dept_filter, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(dd_dept_filter, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(dd_dept_filter, 1, 0);
  lv_obj_set_style_radius(dd_dept_filter, 8, 0);
  lv_obj_set_style_text_color(dd_dept_filter, UIManager::rgb(COLOR_TEXT_MAIN), 0);
  lv_obj_add_event_cb(dd_dept_filter, dd_filter_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_dropdown_set_text(dd_dept_filter, "Department");

  dd_branch_filter = lv_dropdown_create(scr_emp_list);
  lv_obj_set_size(dd_branch_filter, 135, 44);
  lv_obj_align(dd_branch_filter, LV_ALIGN_TOP_LEFT, 435, 90);
  lv_obj_set_style_bg_color(dd_branch_filter, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(dd_branch_filter, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(dd_branch_filter, 1, 0);
  lv_obj_set_style_radius(dd_branch_filter, 8, 0);
  lv_obj_set_style_text_color(dd_branch_filter, UIManager::rgb(COLOR_TEXT_MAIN), 0);
  lv_obj_add_event_cb(dd_branch_filter, dd_filter_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_dropdown_set_text(dd_branch_filter, "Branch");

  // Enrollment status filter dropdown
  dd_status_filter = lv_dropdown_create(scr_emp_list);
  lv_dropdown_set_options(dd_status_filter, "All\nEnrolled\nNot Enrolled");
  lv_obj_set_size(dd_status_filter, 135, 44);
  lv_obj_align(dd_status_filter, LV_ALIGN_TOP_LEFT, 580, 90);
  lv_obj_set_style_bg_color(dd_status_filter, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(dd_status_filter, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(dd_status_filter, 1, 0);
  lv_obj_set_style_radius(dd_status_filter, 8, 0);
  lv_obj_set_style_text_color(dd_status_filter, UIManager::rgb(COLOR_TEXT_MAIN), 0);
  lv_obj_add_event_cb(dd_status_filter, dd_filter_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_dropdown_set_text(dd_status_filter, "Status");

  // Sync button
  lv_obj_t *btn_sync = lv_btn_create(scr_emp_list);
  lv_obj_set_size(btn_sync, 44, 44);
  lv_obj_align(btn_sync, LV_ALIGN_TOP_LEFT, 725, 90);
  lv_obj_set_style_bg_color(btn_sync, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(btn_sync, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(btn_sync, 1, 0);
  lv_obj_set_style_radius(btn_sync, 8, 0);
  lv_obj_set_style_shadow_width(btn_sync, 0, 0);
  lv_obj_add_event_cb(btn_sync, btn_emp_sync_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_sync = lv_label_create(btn_sync);
  lv_label_set_text(lbl_sync, LV_SYMBOL_REFRESH);
  UIManager::styleLabel(lbl_sync, COLOR_TEXT_MAIN, &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_sync);

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
  UIManager::styleLabel(ch_name, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_name, LV_ALIGN_LEFT_MID, 20, 0);

  lv_obj_t *ch_job = lv_label_create(col_hdr);
  lv_label_set_text(ch_job, "Role");
  UIManager::styleLabel(ch_job, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_job, LV_ALIGN_LEFT_MID, 200, 0);

  lv_obj_t *ch_branch = lv_label_create(col_hdr);
  lv_label_set_text(ch_branch, "Branch");
  UIManager::styleLabel(ch_branch, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_branch, LV_ALIGN_LEFT_MID, 370, 0);

  lv_obj_t *ch_dept = lv_label_create(col_hdr);
  lv_label_set_text(ch_dept, "Department");
  UIManager::styleLabel(ch_dept, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(ch_dept, LV_ALIGN_LEFT_MID, 510, 0);

  lv_obj_t *ch_status = lv_label_create(col_hdr);
  lv_label_set_text(ch_status, "Status");
  UIManager::styleLabel(ch_status, COLOR_TEXT_MAIN, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
  lv_obj_align(ch_status, LV_ALIGN_RIGHT_MID, -30, 0);

  // ─── Employee List Container ───
  emp_list_obj = lv_obj_create(scr_emp_list);
  lv_obj_set_size(emp_list_obj, 760, 210); // 4 rows perfectly (4*52 + 2 border)
  lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
  lv_obj_set_style_bg_color(emp_list_obj, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_color(emp_list_obj, UIManager::rgb(0xE0E0E0), 0);
  lv_obj_set_style_border_width(emp_list_obj, 1, 0);
  lv_obj_set_style_radius(emp_list_obj, 10, 0);
  lv_obj_set_style_pad_all(emp_list_obj, 0, 0);
  lv_obj_set_style_pad_row(emp_list_obj, 0, 0);
  lv_obj_set_flex_flow(emp_list_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_layout(emp_list_obj, LV_LAYOUT_FLEX);
  lv_obj_set_scrollbar_mode(emp_list_obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(emp_list_obj, LV_OBJ_FLAG_SCROLLABLE);

  // Pagination container
  lv_obj_t *pagination_cont = lv_obj_create(scr_emp_list);
  lv_obj_set_size(pagination_cont, 760, 50);
  lv_obj_align(pagination_cont, LV_ALIGN_TOP_MID, 0, 410); // Center in empty bottom gap
  lv_obj_set_style_bg_color(pagination_cont, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_border_width(pagination_cont, 0, 0);
  lv_obj_clear_flag(pagination_cont, LV_OBJ_FLAG_SCROLLABLE);

  btn_prev_page = lv_btn_create(pagination_cont);
  lv_obj_set_size(btn_prev_page, 120, 40);
  lv_obj_align(btn_prev_page, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_prev_page, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_add_event_cb(btn_prev_page, prev_page_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_prev = lv_label_create(btn_prev_page);
  lv_label_set_text(lbl_prev, "Previous");
  lv_obj_center(lbl_prev);

  btn_next_page = lv_btn_create(pagination_cont);
  lv_obj_set_size(btn_next_page, 120, 40);
  lv_obj_align(btn_next_page, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_next_page, UIManager::rgb(COLOR_GREEN_MAIN), 0);
  lv_obj_add_event_cb(btn_next_page, next_page_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_next = lv_label_create(btn_next_page);
  lv_label_set_text(lbl_next, "Next");
  lv_obj_center(lbl_next);

  lbl_page_info = lv_label_create(pagination_cont);
  lv_label_set_text(lbl_page_info, "Page 1 of 1");
  UIManager::styleLabel(lbl_page_info, COLOR_TEXT_MAIN, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_page_info);

  // NOTE: do NOT call populate_emp_list() here.
  // Ownership of initial population belongs to uiShowEmpList(), which defers
  // it to after the screen is active so it never runs inside an event callback.
}

void buildEnrollScreen() {
  if (scr_enroll != NULL) return;
  scr_enroll = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_enroll, UIManager::rgb(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr_enroll, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(scr_enroll, LV_SCROLLBAR_MODE_OFF);

  // Bottom Left Cancel Button
  btn_enroll_back = lv_btn_create(scr_enroll);
  lv_obj_set_size(btn_enroll_back, 120, 44);
  lv_obj_align(btn_enroll_back, LV_ALIGN_BOTTOM_LEFT, 20, -30);
  lv_obj_set_style_bg_color(btn_enroll_back, UIManager::rgb(0x6B7280), 0); // Gray for cancel
  lv_obj_set_style_radius(btn_enroll_back, 10, 0);
  lv_obj_add_event_cb(btn_enroll_back, enroll_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_enroll_back);
  lv_label_set_text(lbl_back, "Cancel");
  UIManager::styleLabel(lbl_back, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_back);

  // Bottom Right Done Button (Hidden by default)
  btn_enroll_done = lv_btn_create(scr_enroll);
  lv_obj_set_size(btn_enroll_done, 120, 44);
  lv_obj_align(btn_enroll_done, LV_ALIGN_BOTTOM_RIGHT, -20, -30);
  lv_obj_set_style_bg_color(btn_enroll_done, UIManager::rgb(0x2A800F), 0); // Green
  lv_obj_set_style_radius(btn_enroll_done, 10, 0);
  lv_obj_add_event_cb(btn_enroll_done, enroll_done_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_done = lv_label_create(btn_enroll_done);
  lv_label_set_text(lbl_done, "Done " LV_SYMBOL_RIGHT);
  UIManager::styleLabel(lbl_done, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_done);
  lv_obj_add_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

  // Top Center Titles
  lbl_enroll_main_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_main_title, "Enroll fingerprint");
  UIManager::styleLabel(lbl_enroll_main_title, 0x1A1A1A, &lv_font_montserrat_24, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_main_title, LV_ALIGN_TOP_MID, 0, 15);

  lbl_enroll_sub_title = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_sub_title, "• • • Scan finger");
  UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_enroll_sub_title, LV_ALIGN_TOP_MID, 0, 45);

  // Instruction Label
  lbl_enroll_instruction = lv_label_create(scr_enroll);
  lv_label_set_text(lbl_enroll_instruction, "");
  UIManager::styleLabel(lbl_enroll_instruction, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_label_set_long_mode(lbl_enroll_instruction, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_enroll_instruction, 700);
  lv_obj_align(lbl_enroll_instruction, LV_ALIGN_TOP_MID, 0, 100);

  // Central Gray Box
  box_scan = lv_obj_create(scr_enroll);
  lv_obj_set_size(box_scan, 320, 240);
  lv_obj_align(box_scan, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_color(box_scan, UIManager::rgb(0xD9D9D9), 0);
  lv_obj_set_style_border_width(box_scan, 0, 0);
  lv_obj_set_style_radius(box_scan, 16, 0);
  lv_obj_set_style_pad_all(box_scan, 0, 0);
  lv_obj_clear_flag(box_scan, LV_OBJ_FLAG_SCROLLABLE);

  // Elements inside box
  img_scan_icon = lv_img_create(box_scan);
  lv_img_set_src(img_scan_icon, &icon_biometrics);
  lv_obj_align(img_scan_icon, LV_ALIGN_TOP_MID, 0, 30);

  lbl_scan_text = lv_label_create(box_scan);
  lv_label_set_text(lbl_scan_text, "Scan 1 of 3");
  UIManager::styleLabel(lbl_scan_text, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_scan_text, LV_ALIGN_TOP_MID, 0, 130);

  lbl_scan_subtext = lv_label_create(box_scan);
  lv_label_set_text(lbl_scan_subtext, "");
  UIManager::styleLabel(lbl_scan_subtext, 0x666666, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_scan_subtext, LV_ALIGN_TOP_MID, 0, 160);
  lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);

  // Dots
  dot_1 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_1, 6, 6);
  lv_obj_align(dot_1, LV_ALIGN_BOTTOM_MID, -12, -25);
  lv_obj_set_style_radius(dot_1, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_1, 0, 0);

  dot_2 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_2, 6, 6);
  lv_obj_align(dot_2, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_set_style_radius(dot_2, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_2, 0, 0);

  dot_3 = lv_obj_create(box_scan);
  lv_obj_set_size(dot_3, 6, 6);
  lv_obj_align(dot_3, LV_ALIGN_BOTTOM_MID, 12, -25);
  lv_obj_set_style_radius(dot_3, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot_3, 0, 0);
}

void uiShowEnrollStart(const char *name) {
  // Cancel the watchdog — WROOM confirmed it received the ENROLL command.
  if (enrollStartWatchdog) { lv_timer_del(enrollStartWatchdog); enrollStartWatchdog = NULL; }

  if (scr_enroll == NULL) buildEnrollScreen();
  if (returnTimer) { lv_timer_del(returnTimer); returnTimer = NULL; }
  
  lv_obj_clear_flag(btn_enroll_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(lbl_enroll_sub_title, "• • • Scan finger");
  UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

  String inst = "Have ";
  inst += defer_name;
  inst += " from ";
  inst += defer_dept;
  inst += " place their ";
  inst += getFingerName(selected_finger_index);
  inst += " finger on the device sensor";
  lv_label_set_text(lbl_enroll_instruction, inst.c_str());

  lv_img_set_src(img_scan_icon, &icon_biometrics);
  
  lv_label_set_text(lbl_scan_text, "Scan 1 of 3");
  UIManager::styleLabel(lbl_scan_text, 0x333333, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  
  lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  
  lv_obj_clear_flag(dot_1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dot_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dot_3, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_bg_color(dot_1, UIManager::rgb(0x00A3FF), 0); // Blue
  lv_obj_set_style_bg_color(dot_2, UIManager::rgb(0x999999), 0); // Gray
  lv_obj_set_style_bg_color(dot_3, UIManager::rgb(0x999999), 0); // Gray

  lv_scr_load(scr_enroll);
}

void uiShowEnrollStep(int step, const char *msg) {
  char buf[32];
  snprintf(buf, sizeof(buf), "Scan %d of 3", step);
  lv_label_set_text(lbl_scan_text, buf);

  if (msg && strlen(msg) > 0) {
    lv_label_set_text(lbl_scan_subtext, msg);
    lv_obj_clear_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_bg_color(dot_1, UIManager::rgb(step >= 1 ? 0x00A3FF : 0x999999), 0);
  lv_obj_set_style_bg_color(dot_2, UIManager::rgb(step >= 2 ? 0x00A3FF : 0x999999), 0);
  lv_obj_set_style_bg_color(dot_3, UIManager::rgb(step >= 3 ? 0x00A3FF : 0x999999), 0);
}

void uiShowEnrollResult(bool ok, const char *name) {
  lv_obj_add_flag(dot_1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dot_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dot_3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(lbl_scan_subtext, LV_OBJ_FLAG_HIDDEN);

  if (ok) {
    DataManager::updateEmployeeFpEnrolled(selected_emp_id, true, selected_finger_index);

    lv_obj_add_flag(btn_enroll_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_enroll_done, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(lbl_enroll_sub_title, "• • • Done");
    UIManager::styleLabel(lbl_enroll_sub_title, 0x666666, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    lv_img_set_src(img_scan_icon, &icon_check);
    
    lv_label_set_text(lbl_scan_text, "Fingerprint Enrolled");
    UIManager::styleLabel(lbl_scan_text, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    
    String sub = defer_name;
    sub += " - ";
    sub += getFingerName(selected_finger_index);
    lv_label_set_text(lbl_scan_subtext, sub.c_str());

  } else {
    // If you have a failure icon, use it here. For now, keep fallback symbol or just hide it.
    // Assuming we fallback to the wifi icon or hide it since we don't have icon_close image?
    // Actually, we can use the same check icon or hide it. Let's just use icon_biometrics for fail.
    lv_img_set_src(img_scan_icon, &icon_biometrics);
    
    lv_label_set_text(lbl_scan_text, "Enrollment Failed");
    UIManager::styleLabel(lbl_scan_text, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    
    lv_label_set_text(lbl_scan_subtext, "Please try again.");
    
    if (returnTimer) lv_timer_del(returnTimer);
    returnTimer = lv_timer_create([](lv_timer_t *t) {
      uiShowChooseFinger(defer_emp_id, defer_name.c_str(), defer_dept.c_str());
      returnTimer = NULL;
    }, 3000, NULL);
    lv_timer_set_repeat_count(returnTimer, 1);
  }
}

static void choose_back_cb(lv_event_t * e) {
  // Cancel any pending watchdog before leaving the screen
  if (enrollStartWatchdog) { lv_timer_del(enrollStartWatchdog); enrollStartWatchdog = NULL; }
  uiShowEmpList();
}

static void finger_click_cb(lv_event_t * e) {
  int f_idx = (int)(intptr_t)lv_event_get_user_data(e);
  selected_finger_index = f_idx;
  
  uint16_t enrolled_mask = 0;
  const Employee* db = DataManager::getEmployees();
  for (int i = 0; i < DataManager::getEmployeeCount(); i++) {
    if (db[i].id == selected_emp_id) {
      enrolled_mask = db[i].enrolled_fingers;
      break;
    }
  }

  for (int i = 0; i < 10; i++) {
    if (!finger_objs[i]) continue;
    lv_obj_t *lbl = lv_obj_get_child(finger_objs[i], 0);
    if (i == f_idx) {
      lv_obj_add_state(finger_objs[i], LV_STATE_CHECKED);
      lv_obj_set_style_text_color(lbl, UIManager::rgb(0xFFFFFF), 0); // White text on selected
    } else {
      lv_obj_clear_state(finger_objs[i], LV_STATE_CHECKED);
      bool is_enrolled = (enrolled_mask >> i) & 1;
      if (is_enrolled) {
        lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0x60A5FA), 0); // Blue (enrolled)
        lv_obj_set_style_text_color(lbl, UIManager::rgb(0xFFFFFF), 0); // White text
      } else {
        lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0); // Light green (unenrolled)
        lv_obj_set_style_text_color(lbl, UIManager::rgb(0x166534), 0); // Dark green text
      }
    }
  }
  
  if (btn_start_scan) {
    lv_obj_clear_state(btn_start_scan, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2E7D32), 0);
    
    if (g_is_fallback) {
        lv_label_set_text(lbl_start_scan_text, "Load Fingerprint " LV_SYMBOL_UPLOAD);
        if (btn_delete_scan) lv_obj_add_flag(btn_delete_scan, LV_OBJ_FLAG_HIDDEN);
    } else {
        bool f_is_enrolled = (enrolled_mask >> f_idx) & 1;
        if (f_is_enrolled) {
            lv_label_set_text(lbl_start_scan_text, "Overwrite " LV_SYMBOL_RIGHT);
            if (btn_delete_scan) lv_obj_clear_flag(btn_delete_scan, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(lbl_start_scan_text, "Start scan " LV_SYMBOL_RIGHT);
            if (btn_delete_scan) lv_obj_add_flag(btn_delete_scan, LV_OBJ_FLAG_HIDDEN);
        }
    }
  }
}

static void start_scan_cb(lv_event_t * e) {
  if (selected_finger_index < 0 || selected_emp_id.length() == 0) return;
  
  if (g_is_fallback) {
      uint8_t tplData[768];
      size_t tplLen = 0;
      bool success = DataManager::loadTemplate(selected_emp_id, selected_finger_index, tplData, sizeof(tplData), &tplLen);
      
      if (success && tplLen == 512) {
          unsigned char b64[1024];
          size_t b64Len = 0;
          mbedtls_base64_encode(b64, sizeof(b64), &b64Len, tplData, tplLen);
          String b64Str = String((char*)b64);
          
          int chunkSize = 140;
          int totalChunks = (b64Str.length() + chunkSize - 1) / chunkSize;
          
          for (int i=0; i<totalChunks; i++) {
              StaticJsonDocument<512> doc;
              doc["type"] = "CACHE_CHUNK";
              doc["emp_id"] = selected_emp_id.toInt();
              doc["f_idx"] = selected_finger_index;
              doc["c"] = i;
              doc["t"] = totalChunks;
              doc["d"] = b64Str.substring(i*chunkSize, (i+1)*chunkSize);
              
              String out;
              serializeJson(doc, out);
              CommManager::sendCommand(out);
              delay(40);
          }
          UIManager::showToast("Finger loaded! Please scan.", true);
          uiShowIdle();
      } else {
          UIManager::showToast("Fingerprint not found on SD card.", false);
      }
      return;
  }
  
  String n = "";
  const Employee* db = DataManager::getEmployees();
  int count = DataManager::getEmployeeCount();
  for (int i=0; i<count; i++) {
    if (db[i].id == selected_emp_id) { n = db[i].name; break; }
  }

  // Disable the button and show 'Sending...' while we wait for ENROLL_START
  // echo from the WROOM. This prevents double-sends and shows clear feedback.
  if (btn_start_scan) {
    lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
    if (lbl_start_scan_text) lv_label_set_text(lbl_start_scan_text, "Sending... " LV_SYMBOL_UPLOAD);
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "ENROLL:%s:%d:%s", selected_emp_id.c_str(), selected_finger_index, n.c_str());
  CommManager::sendCommand(String(buf));

  // Do NOT call uiShowEnrollStart here. The WROOM echoes ENROLL_START back,
  // which triggers uiShowEnrollStart via CommManager::dispatchJson.
  // Set a 4-second watchdog: if the packet was dropped, restore the button
  // so the user can try again instead of being stuck forever.
  if (enrollStartWatchdog) { lv_timer_del(enrollStartWatchdog); enrollStartWatchdog = NULL; }
  enrollStartWatchdog = lv_timer_create([](lv_timer_t *t) {
    enrollStartWatchdog = NULL;
    // Packet was dropped — restore the Start Scan button so user can retry.
    if (btn_start_scan && lbl_start_scan_text) {
      lv_obj_clear_state(btn_start_scan, LV_STATE_DISABLED);
      lv_label_set_text(lbl_start_scan_text, "Start scan " LV_SYMBOL_RIGHT);
    }
    UIManager::showToast("No response from scanner. Tap again.", false);
  }, 4000, NULL);
  lv_timer_set_repeat_count(enrollStartWatchdog, 1);
}

static void msgbox_event_cb(lv_event_t * e) {
    lv_obj_t * msgbox = lv_event_get_current_target(e);
    if(lv_msgbox_get_active_btn(msgbox) == 0) { // Yes
        if (selected_finger_index >= 0 && selected_emp_id.length() > 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "DELETE:%s:%d", selected_emp_id.c_str(), selected_finger_index);
            CommManager::sendCommand(String(buf));
            // Clear only this specific finger bit
            DataManager::updateEmployeeFpEnrolled(selected_emp_id, false, selected_finger_index);
            uiShowChooseFinger(selected_emp_id, defer_name.c_str(), defer_dept.c_str());
        }
    }
    lv_msgbox_close(msgbox);
}

static void delete_scan_cb(lv_event_t * e) {
    static const char * btns[] = {"Yes", "No", ""};
    lv_obj_t * mbox1 = lv_msgbox_create(NULL, "Delete Fingerprint", "Are you sure you want to delete this fingerprint?", btns, false);
    lv_obj_add_event_cb(mbox1, msgbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox1);
}

void buildChooseFingerScreen() {
  if (scr_choose_finger != NULL) return;
  
  scr_choose_finger = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_choose_finger, UIManager::rgb(0xF8FAF9), 0); // Standard light background

  // 1. Standard Header (Handles title, subtitle, back button, and Wifi/Battery pill!)
  UIManager::buildHeader(scr_choose_finger, "Enroll fingerprint", "Select finger", choose_back_cb, true);

  // 2. Start Scan Button
  btn_start_scan = lv_btn_create(scr_choose_finger);
  lv_obj_set_size(btn_start_scan, 140, 44);
  lv_obj_align(btn_start_scan, LV_ALIGN_TOP_RIGHT, -30, 95);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x6B7280), LV_STATE_DISABLED);
  lv_obj_set_style_radius(btn_start_scan, 8, 0);
  lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
  lv_obj_add_event_cb(btn_start_scan, start_scan_cb, LV_EVENT_CLICKED, NULL);
  lbl_start_scan_text = lv_label_create(btn_start_scan);
  lv_label_set_text(lbl_start_scan_text, "Start scan " LV_SYMBOL_RIGHT);
  UIManager::styleLabel(lbl_start_scan_text, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_start_scan_text);

  // 2b. Delete Scan Button (Hidden by default)
  btn_delete_scan = lv_btn_create(scr_choose_finger);
  lv_obj_set_size(btn_delete_scan, 110, 44);
  lv_obj_align_to(btn_delete_scan, btn_start_scan, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  lv_obj_set_style_bg_color(btn_delete_scan, UIManager::rgb(0xDC2626), LV_STATE_DEFAULT); // Red
  lv_obj_set_style_radius(btn_delete_scan, 8, 0);
  lv_obj_add_flag(btn_delete_scan, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(btn_delete_scan, delete_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_del = lv_label_create(btn_delete_scan);
  lv_label_set_text(lbl_del, LV_SYMBOL_TRASH " Delete");
  UIManager::styleLabel(lbl_del, 0xFFFFFF, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lbl_del);

  // 3. Dynamic Info Label (who we are enrolling)
  lbl_choose_info = lv_label_create(scr_choose_finger);
  lv_label_set_text(lbl_choose_info, "");
  UIManager::styleLabel(lbl_choose_info, 0x1A1A1A, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
  lv_obj_align(lbl_choose_info, LV_ALIGN_TOP_LEFT, 50, 105);

  // 4. Draw Hands
  lv_obj_t *left_palm = lv_obj_create(scr_choose_finger);
  lv_obj_set_size(left_palm, 210, 100);
  lv_obj_align(left_palm, LV_ALIGN_BOTTOM_LEFT, 70, -40);
  lv_obj_set_style_bg_color(left_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(left_palm, 16, 0);
  lv_obj_set_style_border_width(left_palm, 0, 0);
  lv_obj_clear_flag(left_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_lp = lv_label_create(left_palm);
  lv_label_set_text(lbl_lp, "Left hand");
  UIManager::styleLabel(lbl_lp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_lp, LV_ALIGN_BOTTOM_MID, 0, -20);

  lv_obj_t *right_palm = lv_obj_create(scr_choose_finger);
  lv_obj_set_size(right_palm, 210, 100);
  lv_obj_align(right_palm, LV_ALIGN_BOTTOM_RIGHT, -70, -40);
  lv_obj_set_style_bg_color(right_palm, UIManager::rgb(0xE4F3E7), 0);
  lv_obj_set_style_radius(right_palm, 16, 0);
  lv_obj_set_style_border_width(right_palm, 0, 0);
  lv_obj_clear_flag(right_palm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_rp = lv_label_create(right_palm);
  lv_label_set_text(lbl_rp, "Right hand");
  UIManager::styleLabel(lbl_rp, 0x166534, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
  lv_obj_align(lbl_rp, LV_ALIGN_BOTTOM_MID, 0, -20);

  // 5. Fingers
  // Translated +30 X and +80 Y to match the old main_panel offset relative to the full screen.
  int lx[5] = {70, 120, 170, 220, 290};
  int ly[5] = {270, 210, 200, 210, 310};
  int lh[5] = {100, 160, 170, 160, 90};
  const char* l_labels[5] = {"LP", "LR", "LM", "LI", "LT"};

  int rx[5] = {470, 540, 590, 640, 690};
  int ry[5] = {310, 210, 200, 210, 270};
  int rh[5] = {90, 160, 170, 160, 100};
  const char* r_labels[5] = {"RT", "RI", "RM", "RR", "RP"};

  for (int i = 0; i < 10; i++) {
    finger_objs[i] = lv_obj_create(scr_choose_finger);
    lv_obj_set_size(finger_objs[i], 36, i < 5 ? lh[i] : rh[i-5]);
    lv_obj_align(finger_objs[i], LV_ALIGN_TOP_LEFT, i < 5 ? lx[i] : rx[i-5], i < 5 ? ly[i] : ry[i-5]);
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0);
    lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0x2E7D32), LV_STATE_CHECKED);
    lv_obj_set_style_radius(finger_objs[i], 18, 0);
    lv_obj_set_style_border_width(finger_objs[i], 0, 0);
    lv_obj_clear_flag(finger_objs[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(finger_objs[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(finger_objs[i], finger_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t *l = lv_label_create(finger_objs[i]);
    lv_label_set_text(l, i < 5 ? l_labels[i] : r_labels[i-5]);
    UIManager::styleLabel(l, 0x166534, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);
  }
}

// (Moved deferred state for Choose Finger screen to top)

void uiShowChooseFinger(String emp_id, const char *name, const char *dept, bool isFallback) {
  if (s_nav_busy) return; // Ignore taps while a screen transition is in flight
  s_nav_busy = true;
  g_is_fallback = isFallback;
  defer_emp_id = emp_id;
  defer_name = name;
  defer_dept = dept;

  lv_timer_t *defer_timer = lv_timer_create([](lv_timer_t *t) {
    // 1. Create a tiny temporary screen and make it active.
    // This allows us to safely delete the heavy Employee screen without crashing LVGL.
    lv_obj_t *temp_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
    lv_scr_load(temp_scr);

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
    }

    // 3. Now that we have plenty of RAM, build the new Choose Finger screen.
    if (scr_choose_finger == NULL) {
      buildChooseFingerScreen();
    }

    selected_emp_id = defer_emp_id;
    selected_finger_index = -1;

    char buf[256];
    snprintf(buf, sizeof(buf), "Enrolling %s from %s", defer_name.c_str(), defer_dept.c_str());
    lv_label_set_text(lbl_choose_info, buf);

    if (btn_start_scan) {
      lv_obj_add_state(btn_start_scan, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btn_start_scan, UIManager::rgb(0x2A800F), 0);
      lv_label_set_text(lbl_start_scan_text, "Start scan " LV_SYMBOL_RIGHT);
    }
    if (btn_delete_scan) {
      lv_obj_add_flag(btn_delete_scan, LV_OBJ_FLAG_HIDDEN);
    }

    uint16_t enrolled_mask = 0;
    const Employee* db = DataManager::getEmployees();
    for (int i = 0; i < DataManager::getEmployeeCount(); i++) {
      if (db[i].id == selected_emp_id) {
        enrolled_mask = db[i].enrolled_fingers;
        break;
      }
    }

    for (int i = 0; i < 10; i++) {
      if (finger_objs[i]) {
        lv_obj_clear_state(finger_objs[i], LV_STATE_CHECKED);
        lv_obj_t *lbl = lv_obj_get_child(finger_objs[i], 0);
        bool is_enrolled = (enrolled_mask >> i) & 1;
        if (is_enrolled) {
          lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0x60A5FA), 0); // Blue for enrolled
          lv_obj_set_style_text_color(lbl, UIManager::rgb(0xFFFFFF), 0); // White text
        } else {
          lv_obj_set_style_bg_color(finger_objs[i], UIManager::rgb(0xE4F3E7), 0); // Light green for unenrolled
          lv_obj_set_style_text_color(lbl, UIManager::rgb(0x166534), 0); // Dark green text
        }
      }
    }

    // 4. Load the new screen and auto-delete the temporary screen.
    lv_scr_load(scr_choose_finger);
    lv_obj_del_async(temp_scr);
    s_nav_busy = false; // Navigation complete — allow next tap
  }, 10, NULL);

  lv_timer_set_repeat_count(defer_timer, 1);
}

// Show the employee list screen with a clean, up-to-date state.
// Called on first entry from Main Menu, and when navigating back from Choose Finger.
void uiShowEmpList(bool isFallback) {
  if (s_nav_busy) return; // Ignore taps while a screen transition is in flight
  s_nav_busy = true;
  g_is_fallback = isFallback;
  if (search_debounce_timer) {
    lv_timer_del(search_debounce_timer);
    search_debounce_timer = NULL;
  }

  lv_timer_t *defer = lv_timer_create([](lv_timer_t *t) {
    lv_obj_t *temp_scr = NULL;

    // 1. Delete Choose Finger if transitioning from it
    if (scr_choose_finger != NULL) {
      temp_scr = lv_obj_create(NULL);
      if (temp_scr) {
        lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
        lv_scr_load(temp_scr);
      } else {
        extern void uiShowIdle();
        uiShowIdle();
      }

      lv_obj_del(scr_choose_finger);
      scr_choose_finger = NULL;
      btn_start_scan = NULL;
      btn_delete_scan = NULL;
      lbl_start_scan_text = NULL;
      lbl_choose_info = NULL;
      for (int i = 0; i < 10; i++) finger_objs[i] = NULL;
    }

    // 2. Delete Main Menu if transitioning from it to save RAM
    if (!temp_scr) {
      temp_scr = lv_obj_create(NULL);
      if (temp_scr) {
        lv_obj_set_style_bg_color(temp_scr, UIManager::rgb(0x1A1A1A), 0);
        lv_scr_load(temp_scr);
      } else {
        extern void uiShowIdle();
        uiShowIdle();
      }
    }
    uiDestroyMainMenu();

    // 3. Now that we have RAM, build the Employee List screen.
    if (scr_emp_list == NULL) {
      buildEmpListScreen();
    }
    
    // If it STILL failed to build due to OOM, abort gracefully
    if (scr_emp_list == NULL || emp_list_obj == NULL) {
      UIManager::showToast("Memory Full!", true);
      if (temp_scr) lv_obj_del_async(temp_scr);
      
      // Since we deleted main menu, force fallback to Idle or rebuild Main Menu
      extern void uiShowMainMenu();
      uiShowMainMenu();
      return;
    }

    // Pre-calculate unique departments and branches dynamically
    const int MAX_UNIQUE = 30;
    const char* unique_depts[MAX_UNIQUE];
    int num_depts = 0;
    const char* unique_branches[MAX_UNIQUE];
    int num_branches = 0;

    const Employee* db = DataManager::getEmployees();
    int count = DataManager::getEmployeeCount();

    for (int i = 0; i < count; i++) {
      const char* d = db[i].dept.c_str();
      const char* b = db[i].branch.c_str();
      if (strlen(d) > 0 && !equalsIgnoreCase(d, "admin") && !equalsIgnoreCase(d, "All")) {
          bool found = false;
          for (int j = 0; j < num_depts; j++) if (equalsIgnoreCase(unique_depts[j], d)) { found = true; break; }
          if (!found && num_depts < MAX_UNIQUE) unique_depts[num_depts++] = d;
      }
      if (strlen(b) > 0 && !equalsIgnoreCase(b, "All")) {
          bool found = false;
          for (int j = 0; j < num_branches; j++) if (equalsIgnoreCase(unique_branches[j], b)) { found = true; break; }
          if (!found && num_branches < MAX_UNIQUE) unique_branches[num_branches++] = b;
      }
    }

    String dept_opts = "All";
    for (int i = 0; i < num_depts; i++) { dept_opts += "\n"; dept_opts += unique_depts[i]; }
    String branch_opts = "All";
    for (int i = 0; i < num_branches; i++) { branch_opts += "\n"; branch_opts += unique_branches[i]; }

    if (dd_dept_filter) lv_dropdown_set_options(dd_dept_filter, dept_opts.c_str());
    if (dd_branch_filter) lv_dropdown_set_options(dd_branch_filter, branch_opts.c_str());

    if (ta_search) { lv_obj_clear_state(ta_search, LV_STATE_FOCUSED); lv_textarea_set_text(ta_search, ""); }
    if (dd_status_filter) { lv_dropdown_set_selected(dd_status_filter, 0); lv_dropdown_set_text(dd_status_filter, "Status"); }
    if (dd_dept_filter) { lv_dropdown_set_selected(dd_dept_filter, 0); lv_dropdown_set_text(dd_dept_filter, "Department"); }
    if (dd_branch_filter) { lv_dropdown_set_selected(dd_branch_filter, 0); lv_dropdown_set_text(dd_branch_filter, "Branch"); }
    g_status_filter = 0;
    g_dept_filter_str = "All";
    g_branch_filter_str = "All";
    current_page = 0;
    if (emp_list_obj) {
      lv_obj_set_height(emp_list_obj, 208);
      lv_obj_align(emp_list_obj, LV_ALIGN_TOP_MID, 0, 185);
    }

    // 4. Load the Employee List screen and auto-delete the temporary screen.
    lv_scr_load(scr_emp_list);
    if (temp_scr) {
      lv_obj_del_async(temp_scr);
    }

    // 5. Populate the list in a SECOND deferred timer so it runs AFTER LVGL
    //    has fully committed the screen load (prevents freeze from running
    //    lv_obj_clean + widget creation inside the same callback as lv_scr_load).
    lv_timer_t *pop_timer = lv_timer_create([](lv_timer_t *t) {
      populate_emp_list("", "");
      s_nav_busy = false; // Navigation fully complete — list is now visible
    }, 20, NULL);
    lv_timer_set_repeat_count(pop_timer, 1);
  }, 10, NULL);

  lv_timer_set_repeat_count(defer, 1);
}

